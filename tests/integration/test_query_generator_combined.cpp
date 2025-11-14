/**
 * Integration test for query_generator with ALL adapters combined
 *
 * This test verifies that:
 * 1. query_generator can handle multiple adapters simultaneously
 * 2. TTMath, CppDecimal, and std::chrono adapters work together
 * 3. Generated code compiles with mixed adapter types
 * 4. All adapters function correctly in the same database operation
 */

#include <gtest/gtest.h>
#include "../test_base.hpp"

// Include the generated headers
// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_combined.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"
#include "fbpp/adapters/cppdecimal_decfloat.hpp"
#include "fbpp/adapters/chrono_datetime.hpp"

using namespace fbpp::core;
using namespace fbpp::test;
using namespace generated::queries;

/**
 * Integration test fixture for combined adapters
 *
 * Tests all adapters working together in a single operation
 */
class QueryGeneratorCombinedTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        TempDatabaseTest::SetUp();
        createTestSchema();
    }

    /**
     * Create test table schema with all adapter types
     *
     * Table: COMBINED_TEST
     * - F_ID: INTEGER PRIMARY KEY
     * - F_INT128: INT128 (TTMath adapter)
     * - F_NUMERIC38_2: NUMERIC(38, 2) (TTMath adapter)
     * - F_DECFLOAT16: DECFLOAT(16) (CppDecimal adapter)
     * - F_DECFLOAT34: DECFLOAT(34) (CppDecimal adapter)
     * - F_DATE: DATE (std::chrono adapter)
     * - F_TIMESTAMP: TIMESTAMP (std::chrono adapter)
     * - F_NAME: VARCHAR(100)
     */
    void createTestSchema() override {
        try {
            connection_->ExecuteDDL(
                "CREATE TABLE COMBINED_TEST ("
                "    F_ID INTEGER NOT NULL PRIMARY KEY,"
                "    F_INT128 INT128,"
                "    F_NUMERIC38_2 NUMERIC(38, 2),"
                "    F_DECFLOAT16 DECFLOAT(16),"
                "    F_DECFLOAT34 DECFLOAT(34),"
                "    F_DATE DATE,"
                "    F_TIMESTAMP TIMESTAMP,"
                "    F_NAME VARCHAR(100)"
                ")"
            );
        } catch (...) {
            // Table already exists; ignore.
        }
    }
};

/**
 * Test: INSERT + SELECT with all adapter types
 *
 * Verifies:
 * - All adapters can be used together in one INSERT
 * - All adapters can be used together in one SELECT
 * - All values round-trip correctly
 */
TEST_F(QueryGeneratorCombinedTest, InsertAndSelectAllAdapters) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertAllTypes>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectAllTypesById>::positionalSql)
    );

    // 1. INSERT test data with all adapter types
    auto insertTxn = connection_->StartTransaction();

    InsertAllTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID

    // TTMath Int128
    insertInput.param2 = fbpp::adapters::Int128("123456789012345678901234567890");

    // TTMath Numeric(38,2)
    insertInput.param3 = fbpp::adapters::TTNumeric<2, -2>("99999999999999999999999999999999999.99");

    // CppDecimal DecFloat16
    insertInput.param4 = fbpp::adapters::DecFloat16("123.456");

    // CppDecimal DecFloat34
    insertInput.param5 = fbpp::adapters::DecFloat34("123456789012345678901234567890.1234");

    // std::chrono date
    insertInput.param6 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::November, std::chrono::day{13}
    };

    // std::chrono timestamp
    insertInput.param7 = std::chrono::system_clock::from_time_t(1699900800);  // 2023-11-13 16:00:00 UTC

    insertInput.param8 = "Combined Test";  // F_NAME

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // 2. SELECT and verify all adapter types
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));

    // Expected tuple type with all adapter types
    std::tuple<
        fbpp::adapters::Int128,                         // F_INT128 (TTMath)
        fbpp::adapters::TTNumeric<2, -2>,               // F_NUMERIC38_2 (TTMath)
        fbpp::adapters::DecFloat16,                     // F_DECFLOAT16 (CppDecimal)
        fbpp::adapters::DecFloat34,                     // F_DECFLOAT34 (CppDecimal)
        std::chrono::year_month_day,                    // F_DATE (std::chrono)
        std::chrono::system_clock::time_point,          // F_TIMESTAMP (std::chrono)
        std::string                                     // F_NAME
    > result;

    ASSERT_TRUE(resultSet->fetch(result));

    // Verify TTMath Int128
    EXPECT_EQ(std::get<0>(result).ToString(), "123456789012345678901234567890");

    // Verify TTMath Numeric(38,2)
    EXPECT_EQ(std::get<1>(result).to_string(), "99999999999999999999999999999999999.99");

    // Verify CppDecimal DecFloat16
    EXPECT_EQ(std::get<2>(result).toString(), "123.456");

    // Verify CppDecimal DecFloat34
    EXPECT_EQ(std::get<3>(result).toString(), "123456789012345678901234567890.1234");

    // Verify std::chrono date
    auto date = std::get<4>(result);
    EXPECT_EQ(static_cast<int>(date.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date.month()), 11u);  // November
    EXPECT_EQ(static_cast<unsigned>(date.day()), 13u);

    // Verify std::chrono timestamp
    auto ts = std::get<5>(result);
    auto ts_time_t = std::chrono::system_clock::to_time_t(ts);
    EXPECT_NEAR(ts_time_t, 1699900800, 1);  // Allow 1 second tolerance

    // Verify name
    EXPECT_EQ(std::get<6>(result), "Combined Test");

    // Verify no more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: SELECT ALL with all adapter types
 *
 * Verifies:
 * - Query without parameters works with all adapters
 * - Multiple rows can be fetched with all adapter types
 * - All adapters maintain data integrity across multiple rows
 */
TEST_F(QueryGeneratorCombinedTest, SelectAllCombined) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertAllTypes>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectAllCombined>::positionalSql)
    );

    // 1. INSERT test data - 2 rows with different values
    auto insertTxn = connection_->StartTransaction();

    // Row 1
    InsertAllTypesIn input1;
    input1.param1 = 1;
    input1.param2 = fbpp::adapters::Int128("111111111111111111111111111111");
    input1.param3 = fbpp::adapters::TTNumeric<2, -2>("1111.11");
    input1.param4 = fbpp::adapters::DecFloat16("11.11");
    input1.param5 = fbpp::adapters::DecFloat34("111111111.1111");
    input1.param6 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    input1.param7 = std::chrono::system_clock::from_time_t(1704067200);
    input1.param8 = "Row 1";
    insertTxn->execute(insertStmt, input1);

    // Row 2
    InsertAllTypesIn input2;
    input2.param1 = 2;
    input2.param2 = fbpp::adapters::Int128("222222222222222222222222222222");
    input2.param3 = fbpp::adapters::TTNumeric<2, -2>("2222.22");
    input2.param4 = fbpp::adapters::DecFloat16("22.22");
    input2.param5 = fbpp::adapters::DecFloat34("222222222.2222");
    input2.param6 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::December, std::chrono::day{31}
    };
    input2.param7 = std::chrono::system_clock::from_time_t(1735689600);
    input2.param8 = "Row 2";
    insertTxn->execute(insertStmt, input2);

    insertTxn->Commit();

    // 2. SELECT ALL rows (no parameters)
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt);  // No parameters

    std::tuple<
        int32_t,                                        // F_ID
        fbpp::adapters::Int128,                         // F_INT128
        fbpp::adapters::TTNumeric<2, -2>,               // F_NUMERIC38_2
        fbpp::adapters::DecFloat16,                     // F_DECFLOAT16
        fbpp::adapters::DecFloat34,                     // F_DECFLOAT34
        std::chrono::year_month_day,                    // F_DATE
        std::chrono::system_clock::time_point,          // F_TIMESTAMP
        std::string                                     // F_NAME
    > result;

    // First row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 1);
    EXPECT_EQ(std::get<1>(result).ToString(), "111111111111111111111111111111");
    EXPECT_EQ(std::get<2>(result).to_string(), "1111.11");
    EXPECT_EQ(std::get<3>(result).toString(), "11.11");
    EXPECT_EQ(std::get<4>(result).toString(), "111111111.1111");

    auto date1 = std::get<5>(result);
    EXPECT_EQ(static_cast<int>(date1.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date1.month()), 1u);

    EXPECT_EQ(std::get<7>(result), "Row 1");

    // Second row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 2);
    EXPECT_EQ(std::get<1>(result).ToString(), "222222222222222222222222222222");
    EXPECT_EQ(std::get<2>(result).to_string(), "2222.22");
    EXPECT_EQ(std::get<3>(result).toString(), "22.22");
    EXPECT_EQ(std::get<4>(result).toString(), "222222222.2222");

    auto date2 = std::get<5>(result);
    EXPECT_EQ(static_cast<int>(date2.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date2.month()), 12u);

    EXPECT_EQ(std::get<7>(result), "Row 2");

    // No more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: Verify generated types compile with all adapters
 *
 * This is a compile-time test that verifies:
 * - All adapter type aliases are generated correctly
 * - Input/Output structs compile with all adapter types
 * - QueryDescriptor specializations work
 */
TEST_F(QueryGeneratorCombinedTest, GeneratedTypesCompile) {
    // Test QueryDescriptor access
    static_assert(QueryDescriptor<QueryId::InsertAllTypes>::id == QueryId::InsertAllTypes);

    auto queryName = QueryDescriptor<QueryId::InsertAllTypes>::name;
    EXPECT_EQ(std::string(queryName), "InsertAllTypes");

    // Test struct instantiation with all adapter types
    InsertAllTypesIn input;
    input.param1 = 1;
    input.param2 = fbpp::adapters::Int128("123");
    input.param3 = fbpp::adapters::TTNumeric<2, -2>("456.78");
    input.param4 = fbpp::adapters::DecFloat16("9.01");
    input.param5 = fbpp::adapters::DecFloat34("234.56");
    input.param6 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    input.param7 = std::chrono::system_clock::now();
    input.param8 = "Test";

    EXPECT_EQ(input.param1, 1);
    EXPECT_EQ(input.param8, "Test");

    // Test output struct
    SelectAllCombinedOut output;
    output.fId = 42;
    output.fInt128 = fbpp::adapters::Int128("789");
    output.fNumeric382 = fbpp::adapters::TTNumeric<2, -2>("123.45");
    output.fDecfloat16 = fbpp::adapters::DecFloat16("6.78");
    output.fDecfloat34 = fbpp::adapters::DecFloat34("901.23");
    output.fDate = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    output.fTimestamp = std::chrono::system_clock::now();
    output.fName = "Output test";

    EXPECT_EQ(output.fId, 42);
    EXPECT_EQ(output.fName, "Output test");
}
