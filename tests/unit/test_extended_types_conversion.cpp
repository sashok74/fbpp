#include "../test_base.hpp"
#include "fbpp/core/json_packer.hpp"
#include "fbpp/core/json_unpacker.hpp"
#include "fbpp/core/tuple_packer.hpp"
#include "fbpp/core/tuple_unpacker.hpp"
#include <gtest/gtest.h>

using namespace fbpp::core;
using namespace fbpp::test;

class ExtendedTypesConversionTest : public TempDatabaseTest {
protected:
    std::vector<SchemaProfile> schemaProfiles() const override {
        return {SchemaProfile::ExtendedTypes};
    }
};

TEST_F(ExtendedTypesConversionTest, JsonPackUnpackExtendedTypes) {
    if (!connection_) GTEST_SKIP() << "Database not available";

    auto transaction = connection_->StartTransaction();
    
    // Prepare insert statement
    std::string insertSql = "INSERT INTO TEST_EXTENDED_TYPES VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    auto stmt = connection_->prepareStatement(insertSql);
    
    // Test data as strings
    nlohmann::json inputJson = {
        1,                                      // ID
        "123456789012345678901234567890",       // INT128 (huge number)
        "1.2345E+10",                           // DEC16
        "1.23456789012345678901234567890E+20",  // DEC34
        "2023-10-27T10:30:00.0000",             // TIMESTAMP
        "2023-10-27T10:30:00.0000+02:00",       // TIMESTAMP_TZ
        "10:30:00.0000",                        // TIME
        "10:30:00.0000+02:00",                  // TIME_TZ
        "2023-10-27",                           // DATE
        "1234.5678"                             // NUMERIC(18, 4)
    };
    
    // Execute insert using JSON
    transaction->execute(stmt, inputJson);
    
    // Select back
    std::string selectSql = "SELECT * FROM TEST_EXTENDED_TYPES WHERE ID = ?";
    auto selectStmt = connection_->prepareStatement(selectSql);
    
    // Execute query using JSON for parameters
    nlohmann::json idJson = {1};
    auto resultSet = transaction->openCursor(selectStmt, idJson);
    
    // Unpack result into JSON
    nlohmann::json resultJson;
    ASSERT_TRUE(resultSet->fetch(resultJson));
    
    // Verify results
    // Note: resultJson will be an array or object depending on how unpack is implemented for JSON.
    // Usually unpack<json> produces an object if field names are available, or array if not?
    // JsonUnpacker usually produces an object with field names.
    
    if (resultJson.is_array()) {
        // If it returns an array, access by index
        EXPECT_EQ(resultJson[0], 1);
        EXPECT_EQ(resultJson[1], "123456789012345678901234567890");
        // Firebird formats DECFLOAT without scientific notation if it fits well
        EXPECT_EQ(resultJson[2], "12345000000"); // 1.2345E+10
        EXPECT_EQ(resultJson[3], "123456789012345678901.234567890"); // 1.23456789012345678901234567890E+20
        EXPECT_EQ(resultJson[4], "2023-10-27T10:30:00.0000");
        EXPECT_EQ(resultJson[5], "2023-10-27T10:30:00.0000+02:00");
        EXPECT_EQ(resultJson[6], "10:30:00.0000");
        EXPECT_EQ(resultJson[7], "10:30:00.0000+02:00");
        EXPECT_EQ(resultJson[8], "2023-10-27");
        EXPECT_EQ(resultJson[9], "1234.5678");
    } else {
        // If it returns an object, access by name (uppercase usually)
        EXPECT_EQ(resultJson["ID"], 1);
        EXPECT_EQ(resultJson["VAL_INT128"], "123456789012345678901234567890");
        EXPECT_EQ(resultJson["VAL_DEC16"], "12345000000"); 
        EXPECT_EQ(resultJson["VAL_DEC34"], "123456789012345678901.234567890");
        EXPECT_EQ(resultJson["VAL_TIMESTAMP"], "2023-10-27T10:30:00.0000");
        EXPECT_EQ(resultJson["VAL_TIMESTAMP_TZ"], "2023-10-27T10:30:00.0000+02:00");
        EXPECT_EQ(resultJson["VAL_TIME"], "10:30:00.0000");
        EXPECT_EQ(resultJson["VAL_TIME_TZ"], "10:30:00.0000+02:00");
        EXPECT_EQ(resultJson["VAL_DATE"], "2023-10-27");
        EXPECT_EQ(resultJson["VAL_NUMERIC_SCALE"], "1234.5678");
    }
    
    transaction->Commit();
}

// Перекрёстная проверка TZ-типов с движком. Round-trip через сам fbpp
// раньше «проходил» из-за взаимокомпенсирующихся ошибок записи и чтения:
// zone id кодировался как сырой offset (а не displacement+1439), в UTC-слот
// писалось локальное время, а offset при чтении брался из padding-байтов.
TEST_F(ExtendedTypesConversionTest, TimestampTzCrossCheckedWithEngine) {
    if (!connection_) GTEST_SKIP() << "Database not available";

    auto tx = connection_->StartTransaction();

    // 1) Значение, записанное СЕРВЕРОМ (литерал), fbpp должен прочитать верно.
    auto ins = connection_->prepareStatement(
        "INSERT INTO TEST_EXTENDED_TYPES (ID, VAL_TIMESTAMP_TZ, VAL_TIME_TZ) VALUES "
        "(100, TIMESTAMP '2025-06-10 12:00:00.0000 +05:00', TIME '12:00:00.0000 +05:00')");
    tx->execute(ins);

    auto sel = connection_->prepareStatement(
        "SELECT VAL_TIMESTAMP_TZ, VAL_TIME_TZ FROM TEST_EXTENDED_TYPES WHERE ID = 100");
    auto rs = tx->openCursor(sel);
    std::tuple<std::string, std::string> row;
    ASSERT_TRUE(rs->fetch(row));
    EXPECT_EQ(std::get<0>(row), "2025-06-10T12:00:00.0000+05:00");
    EXPECT_EQ(std::get<1>(row), "12:00:00.0000+05:00");
    rs->close();

    // 2) Значение, записанное fbpp-параметром, СЕРВЕР должен видеть как тот же
    //    момент и ту же зону (сравнение TZ-типов в Firebird идёт по UTC).
    auto ins2 = connection_->prepareStatement(
        "INSERT INTO TEST_EXTENDED_TYPES (ID, VAL_TIMESTAMP_TZ) VALUES (?, ?)");
    tx->execute(ins2, std::make_tuple(101, std::string("2025-06-10T12:00:00.0000+05:00")));

    auto chk = connection_->prepareStatement(
        "SELECT COUNT(*), MIN(CAST(VAL_TIMESTAMP_TZ AS VARCHAR(60))) "
        "FROM TEST_EXTENDED_TYPES WHERE ID = 101 AND "
        "VAL_TIMESTAMP_TZ = TIMESTAMP '2025-06-10 12:00:00.0000 +05:00'");
    auto rs2 = tx->openCursor(chk);
    std::tuple<int64_t, std::string> cntRow;
    ASSERT_TRUE(rs2->fetch(cntRow));
    EXPECT_EQ(std::get<0>(cntRow), 1)
        << "Stored instant differs from what the engine stores for the same literal";
    EXPECT_NE(std::get<1>(cntRow).find("+05:00"), std::string::npos)
        << "Engine-rendered value lost the timezone: " << std::get<1>(cntRow);
    rs2->close();

    // 3) Суффикс 'Z' и регион-зона по имени должны приниматься на запись.
    auto ins3 = connection_->prepareStatement(
        "INSERT INTO TEST_EXTENDED_TYPES (ID, VAL_TIMESTAMP_TZ) VALUES (?, ?)");
    tx->execute(ins3, std::make_tuple(102, std::string("2025-06-10T07:00:00.0000Z")));

    auto chk3 = connection_->prepareStatement(
        "SELECT COUNT(*) FROM TEST_EXTENDED_TYPES WHERE ID = 102 AND "
        "VAL_TIMESTAMP_TZ = TIMESTAMP '2025-06-10 12:00:00.0000 +05:00'");
    auto rs3 = tx->openCursor(chk3);
    std::tuple<int64_t> cnt3;
    ASSERT_TRUE(rs3->fetch(cnt3));
    EXPECT_EQ(std::get<0>(cnt3), 1) << "'Z' input did not store the correct UTC instant";
    rs3->close();

    tx->Commit();
}

TEST_F(ExtendedTypesConversionTest, TuplePackUnpackExtendedTypesAsString) {
    if (!connection_) GTEST_SKIP() << "Database not available";

    auto transaction = connection_->StartTransaction();
    
    // Prepare insert statement
    std::string insertSql = "INSERT INTO TEST_EXTENDED_TYPES VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    auto stmt = connection_->prepareStatement(insertSql);
    
    // Test data as strings in tuple
    using InputTuple = std::tuple<int, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string>;
    InputTuple inputData = {
        2,
        "987654321098765432109876543210",
        "5.4321E-5",
        "9.87654321098765432109876543210E-10",
        "2024-01-01T12:00:00.0000",
        "2024-01-01T12:00:00.0000-05:00",
        "12:00:00.0000",
        "12:00:00.0000-05:00",
        "2024-01-01",
        "-9876.5432"
    };
    
    // Execute insert using Tuple
    transaction->execute(stmt, inputData);
    
    // Select back
    std::string selectSql = "SELECT * FROM TEST_EXTENDED_TYPES WHERE ID = ?";
    auto selectStmt = connection_->prepareStatement(selectSql);
    
    // Execute query using Tuple for parameters
    std::tuple<int> idTuple = {2};
    auto resultSet = transaction->openCursor(selectStmt, idTuple);
    
    // Unpack result into Tuple
    InputTuple resultData;
    ASSERT_TRUE(resultSet->fetch(resultData));
    
    // Verify results
    EXPECT_EQ(std::get<0>(resultData), 2);
    EXPECT_EQ(std::get<1>(resultData), "987654321098765432109876543210");
    EXPECT_EQ(std::get<2>(resultData), "0.000054321"); // 5.4321E-5
    EXPECT_EQ(std::get<3>(resultData), "0.000000000987654321098765432109876543210"); // 9.87654321098765432109876543210E-10
    EXPECT_EQ(std::get<4>(resultData), "2024-01-01T12:00:00.0000");
    EXPECT_EQ(std::get<5>(resultData), "2024-01-01T12:00:00.0000-05:00");
    EXPECT_EQ(std::get<6>(resultData), "12:00:00.0000");
    EXPECT_EQ(std::get<7>(resultData), "12:00:00.0000-05:00");
    EXPECT_EQ(std::get<8>(resultData), "2024-01-01");
    EXPECT_EQ(std::get<9>(resultData), "-9876.5432");
    
    transaction->Commit();
}
