/**
 * Integration test for query_generator with std::chrono adapters
 *
 * This test verifies the full E2E workflow:
 * 1. query_generator reads SQL queries from JSON file
 * 2. Generates C++ header with type-safe structs using std::chrono adapters
 * 3. Generated code compiles successfully
 * 4. Generated code works with actual database operations
 */

#include <gtest/gtest.h>
#include "../test_base.hpp"

// Include the generated headers
// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_datetime.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/adapters/chrono_datetime.hpp"

using namespace fbpp::core;
using namespace fbpp::test;
using namespace generated::queries;

/**
 * Integration test fixture for std::chrono adapters
 *
 * Uses TempDatabaseTest pattern:
 * - Creates unique temporary database for each test
 * - Automatically cleans up database after test completes
 * - Each test is isolated and independent
 */
class QueryGeneratorDateTimeTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        TempDatabaseTest::SetUp();
        createTestSchema();
    }

    /**
     * Create test table schema
     *
     * Table: DATETIME_TEST
     * - F_ID: INTEGER PRIMARY KEY
     * - F_DATE: DATE
     * - F_DATE_NULLABLE: DATE (nullable)
     * - F_TIME: TIME
     * - F_TIMESTAMP: TIMESTAMP
     * - F_TIMESTAMP_NULLABLE: TIMESTAMP (nullable)
     * - F_NAME: VARCHAR(100)
     */
    void createTestSchema() override {
        try {
            connection_->ExecuteDDL(
                "CREATE TABLE DATETIME_TEST ("
                "    F_ID INTEGER NOT NULL PRIMARY KEY,"
                "    F_DATE DATE,"
                "    F_DATE_NULLABLE DATE,"
                "    F_TIME TIME,"
                "    F_TIMESTAMP TIMESTAMP,"
                "    F_TIMESTAMP_NULLABLE TIMESTAMP,"
                "    F_NAME VARCHAR(100)"
                ")"
            );
        } catch (...) {
            // Table already exists, ignore
        }
    }
};

/**
 * Test: INSERT + SELECT with std::chrono types
 *
 * Verifies:
 * - InsertDateTimeTypes query works with std::chrono adapters
 * - SelectDateTimeById query retrieves correct values
 * - DATE, TIME, TIMESTAMP values round-trip correctly
 */
TEST_F(QueryGeneratorDateTimeTest, InsertAndSelectDateTimeTypes) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::InsertDateTimeTypes>::sql
    );

    auto selectStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::SelectDateTimeById>::sql
    );

    // 1. INSERT test data
    auto insertTxn = connection_->StartTransaction();

    InsertDateTimeTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID

    // F_DATE - year_month_day
    insertInput.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{15}
    };

    // F_DATE_NULLABLE
    insertInput.param3 = std::chrono::year_month_day{
        std::chrono::year{2025}, std::chrono::March, std::chrono::day{20}
    };

    // F_TIME - hh_mm_ss
    insertInput.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{
        std::chrono::hours{14} + std::chrono::minutes{30} +
        std::chrono::seconds{45} + std::chrono::microseconds{123456}
    };

    // F_TIMESTAMP - system_clock::time_point
    auto tp = std::chrono::system_clock::from_time_t(1705320645);  // 2024-01-15 12:30:45 UTC
    insertInput.param5 = tp;

    // F_TIMESTAMP_NULLABLE
    auto tp_null = std::chrono::system_clock::from_time_t(1709215200);  // 2024-02-29 12:00:00 UTC
    insertInput.param6 = tp_null;

    insertInput.param7 = "Test DateTime";  // F_NAME

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // 2. SELECT and verify
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));

    // Expected tuple type for SELECT result
    std::tuple<
        std::chrono::year_month_day,                           // F_DATE
        std::optional<std::chrono::year_month_day>,            // F_DATE_NULLABLE
        std::chrono::hh_mm_ss<std::chrono::microseconds>,      // F_TIME
        std::chrono::system_clock::time_point,                 // F_TIMESTAMP
        std::optional<std::chrono::system_clock::time_point>,  // F_TIMESTAMP_NULLABLE
        std::string                                            // F_NAME
    > result;

    ASSERT_TRUE(resultSet->fetch(result));

    // Verify DATE
    auto date = std::get<0>(result);
    EXPECT_EQ(static_cast<int>(date.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date.month()), 1u);  // January
    EXPECT_EQ(static_cast<unsigned>(date.day()), 15u);

    // Verify nullable DATE
    ASSERT_TRUE(std::get<1>(result).has_value());
    auto date_null = std::get<1>(result).value();
    EXPECT_EQ(static_cast<int>(date_null.year()), 2025);
    EXPECT_EQ(static_cast<unsigned>(date_null.month()), 3u);  // March
    EXPECT_EQ(static_cast<unsigned>(date_null.day()), 20u);

    // Verify TIME
    auto time = std::get<2>(result);
    EXPECT_EQ(time.hours().count(), 14);
    EXPECT_EQ(time.minutes().count(), 30);
    EXPECT_EQ(time.seconds().count(), 45);
    // Note: Firebird TIME precision is 1/10000 second, so microseconds might be rounded

    // Verify TIMESTAMP
    auto ts = std::get<3>(result);
    auto ts_time_t = std::chrono::system_clock::to_time_t(ts);
    // Allow some tolerance for rounding (within 1 second)
    EXPECT_NEAR(ts_time_t, 1705320645, 1);

    // Verify nullable TIMESTAMP
    ASSERT_TRUE(std::get<4>(result).has_value());
    auto ts_null = std::get<4>(result).value();
    auto ts_null_time_t = std::chrono::system_clock::to_time_t(ts_null);
    EXPECT_NEAR(ts_null_time_t, 1709215200, 1);

    // Verify name
    EXPECT_EQ(std::get<5>(result), "Test DateTime");

    // Verify no more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: UPDATE + SELECT with TIMESTAMP
 *
 * Verifies:
 * - UpdateTimestamp query works correctly
 * - TIMESTAMP values update properly
 * - Can SELECT updated value back
 */
TEST_F(QueryGeneratorDateTimeTest, UpdateTimestamp) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::InsertDateTimeTypes>::sql
    );

    auto updateStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::UpdateTimestamp>::sql
    );

    auto selectStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::SelectDateTimeById>::sql
    );

    // 1. INSERT test data
    auto insertTxn = connection_->StartTransaction();

    InsertDateTimeTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID
    insertInput.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    insertInput.param3 = std::nullopt;  // NULL
    insertInput.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{
        std::chrono::hours{0}
    };
    insertInput.param5 = std::chrono::system_clock::from_time_t(1704067200);  // 2024-01-01 00:00:00
    insertInput.param6 = std::nullopt;  // NULL
    insertInput.param7 = "Update Test";

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // 2. UPDATE TIMESTAMP value
    auto updateTxn = connection_->StartTransaction();

    UpdateTimestampIn updateInput;
    updateInput.param1 = std::chrono::system_clock::from_time_t(1719792000);  // 2024-07-01 00:00:00
    updateInput.param2 = 1;  // WHERE F_ID = 1

    updateTxn->execute(updateStmt, updateInput);
    updateTxn->Commit();

    // 3. SELECT and verify updated value
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));

    std::tuple<
        std::chrono::year_month_day,
        std::optional<std::chrono::year_month_day>,
        std::chrono::hh_mm_ss<std::chrono::microseconds>,
        std::chrono::system_clock::time_point,
        std::optional<std::chrono::system_clock::time_point>,
        std::string
    > result;

    ASSERT_TRUE(resultSet->fetch(result));

    // Verify TIMESTAMP was updated
    auto ts = std::get<3>(result);
    auto ts_time_t = std::chrono::system_clock::to_time_t(ts);
    EXPECT_NEAR(ts_time_t, 1719792000, 1);

    // Other fields should remain unchanged
    auto date = std::get<0>(result);
    EXPECT_EQ(static_cast<int>(date.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date.month()), 1u);
    EXPECT_FALSE(std::get<1>(result).has_value());  // Still NULL
    EXPECT_EQ(std::get<5>(result), "Update Test");
}

/**
 * Test: SELECT with WHERE clause on DATE
 *
 * Verifies:
 * - SelectDateGreater query filters correctly
 * - WHERE clause works with DATE comparisons
 * - Can SELECT subset of columns
 */
TEST_F(QueryGeneratorDateTimeTest, SelectDateGreater) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::InsertDateTimeTypes>::sql
    );

    auto selectStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::SelectDateGreater>::sql
    );

    // 1. INSERT test data - 3 rows with different dates
    auto insertTxn = connection_->StartTransaction();

    // Row 1: 2024-01-01
    InsertDateTimeTypesIn input1;
    input1.param1 = 1;
    input1.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    input1.param3 = std::nullopt;
    input1.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{std::chrono::hours{12}};
    input1.param5 = std::chrono::system_clock::now();
    input1.param6 = std::nullopt;
    input1.param7 = "Row 1";
    insertTxn->execute(insertStmt, input1);

    // Row 2: 2024-06-15
    InsertDateTimeTypesIn input2;
    input2.param1 = 2;
    input2.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::June, std::chrono::day{15}
    };
    input2.param3 = std::nullopt;
    input2.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{std::chrono::hours{12}};
    input2.param5 = std::chrono::system_clock::now();
    input2.param6 = std::nullopt;
    input2.param7 = "Row 2";
    insertTxn->execute(insertStmt, input2);

    // Row 3: 2024-12-31
    InsertDateTimeTypesIn input3;
    input3.param1 = 3;
    input3.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::December, std::chrono::day{31}
    };
    input3.param3 = std::nullopt;
    input3.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{std::chrono::hours{12}};
    input3.param5 = std::chrono::system_clock::now();
    input3.param6 = std::nullopt;
    input3.param7 = "Row 3";
    insertTxn->execute(insertStmt, input3);

    insertTxn->Commit();

    // 2. SELECT rows with DATE > 2024-03-01 (should get rows 2 and 3)
    auto selectTxn = connection_->StartTransaction();
    auto filterDate = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::March, std::chrono::day{1}
    };
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(filterDate));

    std::tuple<
        std::chrono::year_month_day,  // F_DATE
        std::string                   // F_NAME
    > result;

    // First result: Row 2 (2024-06-15)
    ASSERT_TRUE(resultSet->fetch(result));
    auto date1 = std::get<0>(result);
    EXPECT_EQ(static_cast<int>(date1.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date1.month()), 6u);  // June
    EXPECT_EQ(static_cast<unsigned>(date1.day()), 15u);
    EXPECT_EQ(std::get<1>(result), "Row 2");

    // Second result: Row 3 (2024-12-31)
    ASSERT_TRUE(resultSet->fetch(result));
    auto date2 = std::get<0>(result);
    EXPECT_EQ(static_cast<int>(date2.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date2.month()), 12u);  // December
    EXPECT_EQ(static_cast<unsigned>(date2.day()), 31u);
    EXPECT_EQ(std::get<1>(result), "Row 3");

    // No more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: SELECT ALL rows
 *
 * Verifies:
 * - SelectAllDateTime query returns all rows
 * - Query without parameters works correctly
 * - All columns retrieved correctly
 */
TEST_F(QueryGeneratorDateTimeTest, SelectAllDateTime) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::InsertDateTimeTypes>::sql
    );

    auto selectStmt = connection_->prepareStatement(
        QueryDescriptor<QueryId::SelectAllDateTime>::sql
    );

    // 1. INSERT test data - 2 rows
    auto insertTxn = connection_->StartTransaction();

    // Row 1
    InsertDateTimeTypesIn input1;
    input1.param1 = 1;
    input1.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{15}
    };
    input1.param3 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::February, std::chrono::day{20}
    };
    input1.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{
        std::chrono::hours{10} + std::chrono::minutes{30}
    };
    input1.param5 = std::chrono::system_clock::from_time_t(1705320000);
    input1.param6 = std::chrono::system_clock::from_time_t(1708430400);
    input1.param7 = "First";
    insertTxn->execute(insertStmt, input1);

    // Row 2
    InsertDateTimeTypesIn input2;
    input2.param1 = 2;
    input2.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::July, std::chrono::day{4}
    };
    input2.param3 = std::nullopt;  // NULL
    input2.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{
        std::chrono::hours{15} + std::chrono::minutes{45}
    };
    input2.param5 = std::chrono::system_clock::from_time_t(1720051200);
    input2.param6 = std::nullopt;  // NULL
    input2.param7 = "Second";
    insertTxn->execute(insertStmt, input2);

    insertTxn->Commit();

    // 2. SELECT ALL rows (no parameters)
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt);  // No parameters

    std::tuple<
        int32_t,                                               // F_ID
        std::chrono::year_month_day,                           // F_DATE
        std::optional<std::chrono::year_month_day>,            // F_DATE_NULLABLE
        std::chrono::hh_mm_ss<std::chrono::microseconds>,      // F_TIME
        std::chrono::system_clock::time_point,                 // F_TIMESTAMP
        std::optional<std::chrono::system_clock::time_point>,  // F_TIMESTAMP_NULLABLE
        std::string                                            // F_NAME
    > result;

    // First row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 1);

    auto date1 = std::get<1>(result);
    EXPECT_EQ(static_cast<int>(date1.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date1.month()), 1u);
    EXPECT_EQ(static_cast<unsigned>(date1.day()), 15u);

    ASSERT_TRUE(std::get<2>(result).has_value());
    auto date1_null = std::get<2>(result).value();
    EXPECT_EQ(static_cast<int>(date1_null.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date1_null.month()), 2u);

    auto time1 = std::get<3>(result);
    EXPECT_EQ(time1.hours().count(), 10);
    EXPECT_EQ(time1.minutes().count(), 30);

    ASSERT_TRUE(std::get<5>(result).has_value());
    EXPECT_EQ(std::get<6>(result), "First");

    // Second row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 2);

    auto date2 = std::get<1>(result);
    EXPECT_EQ(static_cast<int>(date2.year()), 2024);
    EXPECT_EQ(static_cast<unsigned>(date2.month()), 7u);

    EXPECT_FALSE(std::get<2>(result).has_value());  // NULL
    EXPECT_FALSE(std::get<5>(result).has_value());  // NULL
    EXPECT_EQ(std::get<6>(result), "Second");

    // No more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: Verify generated types compile
 *
 * This is a compile-time test that verifies:
 * - Type aliases are generated correctly
 * - Input/Output structs compile
 * - QueryDescriptor specializations work
 */
TEST_F(QueryGeneratorDateTimeTest, GeneratedTypesCompile) {
    // Test QueryDescriptor access
    static_assert(QueryDescriptor<QueryId::InsertDateTimeTypes>::id == QueryId::InsertDateTimeTypes);

    auto queryName = QueryDescriptor<QueryId::InsertDateTimeTypes>::name;
    EXPECT_STREQ(queryName, "InsertDateTimeTypes");

    // Test struct instantiation
    InsertDateTimeTypesIn input;
    input.param1 = 1;
    input.param2 = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    input.param3 = std::nullopt;
    input.param4 = std::chrono::hh_mm_ss<std::chrono::microseconds>{std::chrono::hours{12}};
    input.param5 = std::chrono::system_clock::now();
    input.param6 = std::nullopt;
    input.param7 = "Test";

    EXPECT_EQ(input.param1, 1);
    EXPECT_EQ(input.param7, "Test");

    // Test output struct
    SelectAllDateTimeOut output;
    output.fId = 42;
    output.fDate = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{1}
    };
    output.fTime = std::chrono::hh_mm_ss<std::chrono::microseconds>{std::chrono::hours{12}};
    output.fTimestamp = std::chrono::system_clock::now();
    output.fName = "Output test";

    EXPECT_EQ(output.fId, 42);
    EXPECT_EQ(output.fName, "Output test");
}
