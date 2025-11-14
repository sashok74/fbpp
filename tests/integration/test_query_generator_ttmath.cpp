// Integration test for query_generator with TTMath adapter support
// This test verifies that generated code with TTMath adapters compiles and works correctly

#include <gtest/gtest.h>
#include "../test_base.hpp"

// Include the generated headers
// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_ttmath.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"

using namespace fbpp::core;
using namespace fbpp::test;
using namespace generated::queries;

class QueryGeneratorTTMathTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Вызвать базовый SetUp - создаст временную БД
        TempDatabaseTest::SetUp();

        // Создать схему для тестов TTMath
        createTestSchema();
    }

    void createTestSchema() override {
        try {
            connection_->ExecuteDDL(
                "CREATE TABLE TTMATH_TEST ("
                "    F_ID INTEGER NOT NULL PRIMARY KEY,"
                "    F_INT128_PURE INT128,"
                "    F_INT128_NULLABLE INT128,"
                "    F_NUMERIC38_2 NUMERIC(38, 2),"
                "    F_NUMERIC38_4 NUMERIC(38, 4),"
                "    F_NUMERIC38_8 NUMERIC(38, 8),"
                "    F_NUMERIC38_18 NUMERIC(38, 18),"
                "    F_NUMERIC18_2 NUMERIC(18, 2),"
                "    F_NUMERIC18_6 NUMERIC(18, 6),"
                "    F_NAME VARCHAR(100)"
                ")"
            );
        } catch (...) {
            // Table already exists; expected if previous test already created it.
        }
    }

    // connection_ и db_params_ наследуются из TempDatabaseTest
    // TearDown() не нужен - базовый класс удалит временную БД
};

TEST_F(QueryGeneratorTTMathTest, GeneratedTypesCompile) {
    // This test verifies that all generated types compile correctly

    // Test type aliases exist
    [[maybe_unused]] Numeric18_2 n18_2;
    [[maybe_unused]] Numeric18_6 n18_6;
    [[maybe_unused]] Numeric38_2 n38_2;
    [[maybe_unused]] Numeric38_4 n38_4;
    [[maybe_unused]] Numeric38_8 n38_8;
    [[maybe_unused]] Numeric38_18 n38_18;

    // Test struct instantiation
    [[maybe_unused]] InsertTTMathTypesIn insertInput;
    [[maybe_unused]] SelectAllTTMathOut selectOutput;
    [[maybe_unused]] UpdateNumeric38In updateInput;

    SUCCEED();
}

TEST_F(QueryGeneratorTTMathTest, InsertAndSelectTTMathTypes) {
    auto txn = connection_->StartTransaction();

    // Prepare insert statement
    auto stmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertTTMathTypes>::positionalSql)
    );

    // Create input data using TTMath adapters
    InsertTTMathTypesIn input;
    input.param1 = 1;  // F_ID
    input.param2 = fbpp::adapters::Int128("123456789012345678901234567890");  // F_INT128_PURE
    input.param3 = fbpp::adapters::Int128("987654321098765432109876543210");  // F_INT128_NULLABLE
    input.param4 = fbpp::adapters::TTNumeric<2, -2>("12345678901234567890.12");  // F_NUMERIC38_2
    input.param5 = fbpp::adapters::TTNumeric<2, -4>("1234567890123456.1234");    // F_NUMERIC38_4
    input.param6 = fbpp::adapters::TTNumeric<2, -8>("123456789012.12345678");    // F_NUMERIC38_8
    input.param7 = fbpp::adapters::TTNumeric<2, -18>("12.123456789012345678");   // F_NUMERIC38_18
    input.param8 = fbpp::adapters::TTNumeric<1, -2>("1234567890123456.12");      // F_NUMERIC18_2
    input.param9 = fbpp::adapters::TTNumeric<1, -6>("123456789012.123456");      // F_NUMERIC18_6
    input.param10 = "Test TTMath";  // F_NAME

    // Execute insert
    auto affected = txn->execute(stmt, input);
    EXPECT_EQ(affected, 1);
    txn->Commit();

    // Select back the data
    auto selectTxn = connection_->StartTransaction();
    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectTTMathById>::positionalSql)
    );

    SelectTTMathByIdIn selectInput;
    selectInput.param1 = 1;

    auto resultSet = selectTxn->openCursor(selectStmt, selectInput);
    ASSERT_TRUE(resultSet) << "Failed to open cursor";

    // Fetch and verify
    SelectTTMathByIdOut output{};
    ASSERT_TRUE(resultSet->fetch(output));

    // Verify INT128 values
    ASSERT_TRUE(output.fInt128Pure.has_value());
    EXPECT_EQ(output.fInt128Pure->ToString(), "123456789012345678901234567890");
    ASSERT_TRUE(output.fInt128Nullable.has_value());
    EXPECT_EQ(output.fInt128Nullable->ToString(), "987654321098765432109876543210");

    // Verify NUMERIC38 values
    ASSERT_TRUE(output.fNumeric382.has_value());
    EXPECT_EQ(output.fNumeric382->to_string(), "12345678901234567890.12");
    ASSERT_TRUE(output.fNumeric384.has_value());
    EXPECT_EQ(output.fNumeric384->to_string(), "1234567890123456.1234");
    ASSERT_TRUE(output.fNumeric388.has_value());
    EXPECT_EQ(output.fNumeric388->to_string(), "123456789012.12345678");

    // Verify NUMERIC18 values
    ASSERT_TRUE(output.fNumeric182.has_value());
    EXPECT_EQ(output.fNumeric182->to_string(), "1234567890123456.12");
    ASSERT_TRUE(output.fNumeric186.has_value());
    EXPECT_EQ(output.fNumeric186->to_string(), "123456789012.123456");

    // Verify name
    ASSERT_TRUE(output.fName.has_value());
    EXPECT_EQ(*output.fName, "Test TTMath");

    selectTxn->Commit();
}

TEST_F(QueryGeneratorTTMathTest, UpdateNumericTypes) {
    // First, insert a row to update
    auto insertTxn = connection_->StartTransaction();
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertTTMathTypes>::positionalSql)
    );

    InsertTTMathTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID
    insertInput.param2 = fbpp::adapters::Int128("100");
    insertInput.param3 = fbpp::adapters::Int128("200");
    insertInput.param4 = fbpp::adapters::TTNumeric<2, -2>("100.00");
    insertInput.param5 = fbpp::adapters::TTNumeric<2, -4>("200.0000");
    insertInput.param6 = fbpp::adapters::TTNumeric<2, -8>("300.00000000");
    insertInput.param7 = fbpp::adapters::TTNumeric<2, -18>("400.000000000000000000");
    insertInput.param8 = fbpp::adapters::TTNumeric<1, -2>("500.00");
    insertInput.param9 = fbpp::adapters::TTNumeric<1, -6>("600.000000");
    insertInput.param10 = "Initial";

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // Now update the row
    auto txn = connection_->StartTransaction();

    auto updateStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::UpdateNumeric38>::positionalSql)
    );

    UpdateNumeric38In updateInput;
    updateInput.param1 = fbpp::adapters::TTNumeric<2, -2>("99999999999999999999.99");  // F_NUMERIC38_2
    updateInput.param2 = fbpp::adapters::TTNumeric<2, -8>("999999999999.99999999");    // F_NUMERIC38_8
    updateInput.param3 = 1;  // F_ID

    auto affected = txn->execute(updateStmt, updateInput);
    EXPECT_EQ(affected, 1);
    txn->Commit();

    // Verify update
    auto selectTxn = connection_->StartTransaction();
    auto selectStmt = connection_->prepareStatement(
        "SELECT F_NUMERIC38_2, F_NUMERIC38_8 FROM TTMATH_TEST WHERE F_ID = ?"
    );

    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));
    ASSERT_TRUE(resultSet) << "Failed to open cursor";

    std::tuple<fbpp::adapters::TTNumeric<2, -2>, fbpp::adapters::TTNumeric<2, -8>> result;
    ASSERT_TRUE(resultSet->fetch(result));

    auto [numeric38_2, numeric38_8] = result;

    EXPECT_EQ(numeric38_2.to_string(), "99999999999999999999.99");
    EXPECT_EQ(numeric38_8.to_string(), "999999999999.99999999");

    selectTxn->Commit();
}

TEST_F(QueryGeneratorTTMathTest, SelectScaledTypesWithFilter) {
    // First, insert a row
    auto insertTxn = connection_->StartTransaction();
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertTTMathTypes>::positionalSql)
    );

    InsertTTMathTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID
    insertInput.param2 = fbpp::adapters::Int128("100");
    insertInput.param3 = fbpp::adapters::Int128("200");
    insertInput.param4 = fbpp::adapters::TTNumeric<2, -2>("12345.67");
    insertInput.param5 = fbpp::adapters::TTNumeric<2, -4>("98765.4321");
    insertInput.param6 = fbpp::adapters::TTNumeric<2, -8>("111.22334455");
    insertInput.param7 = fbpp::adapters::TTNumeric<2, -18>("0.123456789012345678");
    insertInput.param8 = fbpp::adapters::TTNumeric<1, -2>("5432.10");
    insertInput.param9 = fbpp::adapters::TTNumeric<1, -6>("9876.543210");
    insertInput.param10 = "Filter Test";

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // Now select with filter
    auto txn = connection_->StartTransaction();

    auto stmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectScaledTypes>::positionalSql)
    );

    SelectScaledTypesIn input;
    input.param1 = fbpp::adapters::TTNumeric<2, -2>("0.00");  // Filter: F_NUMERIC38_2 > 0

    auto resultSet = txn->openCursor(stmt, input);
    ASSERT_TRUE(resultSet) << "Failed to open cursor";

    // Should return the row we inserted
    SelectScaledTypesOut output{};
    ASSERT_TRUE(resultSet->fetch(output));

    // Verify scaled numeric types are present
    ASSERT_TRUE(output.fNumeric382.has_value());
    EXPECT_FALSE(output.fNumeric382->to_string().empty());
    ASSERT_TRUE(output.fNumeric388.has_value());
    EXPECT_FALSE(output.fNumeric388->to_string().empty());
    ASSERT_TRUE(output.fNumeric182.has_value());
    EXPECT_FALSE(output.fNumeric182->to_string().empty());

    txn->Commit();
}

TEST_F(QueryGeneratorTTMathTest, SelectAllTTMath) {
    // First, insert a couple of rows
    auto insertTxn = connection_->StartTransaction();
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertTTMathTypes>::positionalSql)
    );

    // Insert row 1
    InsertTTMathTypesIn input1;
    input1.param1 = 1;
    input1.param2 = fbpp::adapters::Int128("111");
    input1.param3 = fbpp::adapters::Int128("222");
    input1.param4 = fbpp::adapters::TTNumeric<2, -2>("111.11");
    input1.param5 = fbpp::adapters::TTNumeric<2, -4>("222.2222");
    input1.param6 = fbpp::adapters::TTNumeric<2, -8>("333.33333333");
    input1.param7 = fbpp::adapters::TTNumeric<2, -18>("0.444444444444444444");
    input1.param8 = fbpp::adapters::TTNumeric<1, -2>("555.55");
    input1.param9 = fbpp::adapters::TTNumeric<1, -6>("666.666666");
    input1.param10 = "Row 1";
    insertTxn->execute(insertStmt, input1);

    // Insert row 2
    InsertTTMathTypesIn input2;
    input2.param1 = 2;
    input2.param2 = fbpp::adapters::Int128("777");
    input2.param3 = fbpp::adapters::Int128("888");
    input2.param4 = fbpp::adapters::TTNumeric<2, -2>("777.77");
    input2.param5 = fbpp::adapters::TTNumeric<2, -4>("888.8888");
    input2.param6 = fbpp::adapters::TTNumeric<2, -8>("999.99999999");
    input2.param7 = fbpp::adapters::TTNumeric<2, -18>("0.101010101010101010");
    input2.param8 = fbpp::adapters::TTNumeric<1, -2>("121.21");
    input2.param9 = fbpp::adapters::TTNumeric<1, -6>("131.313131");
    input2.param10 = "Row 2";
    insertTxn->execute(insertStmt, input2);

    insertTxn->Commit();

    // Now select all rows
    auto txn = connection_->StartTransaction();

    auto stmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectAllTTMath>::positionalSql)
    );

    // SelectAllTTMath has no parameters, so call openCursor without arguments
    auto resultSet = txn->openCursor(stmt);
    ASSERT_TRUE(resultSet) << "Failed to open cursor";

    // Fetch all rows
    int rowCount = 0;
    SelectAllTTMathOut row{};
    while (resultSet->fetch(row)) {
        rowCount++;

        // Verify structure
        EXPECT_GT(row.fId, 0);

        if (row.fInt128Pure.has_value()) {
            EXPECT_FALSE(row.fInt128Pure->ToString().empty());
        }

        // All rows should have these required fields
        if (row.fNumeric382.has_value()) {
            EXPECT_FALSE(row.fNumeric382->to_string().empty());
        }
        if (row.fNumeric388.has_value()) {
            EXPECT_FALSE(row.fNumeric388->to_string().empty());
        }
        if (row.fNumeric182.has_value()) {
            EXPECT_FALSE(row.fNumeric182->to_string().empty());
        }
    }

    EXPECT_EQ(rowCount, 2) << "Should have exactly 2 rows";

    txn->Commit();
}
