/**
 * Integration test for query_generator with CppDecimal adapters
 *
 * This test verifies the full E2E workflow:
 * 1. query_generator reads SQL queries from JSON file
 * 2. Generates C++ header with type-safe structs using CppDecimal adapters
 * 3. Generated code compiles successfully
 * 4. Generated code works with actual database operations
 */

#include <gtest/gtest.h>
#include "../test_base.hpp"

// Include the generated headers
// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_decfloat.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/adapters/cppdecimal_decfloat.hpp"

using namespace fbpp::core;
using namespace fbpp::test;
using namespace generated::queries;

/**
 * Integration test fixture for CppDecimal adapters
 *
 * Uses TempDatabaseTest pattern:
 * - Creates unique temporary database for each test
 * - Automatically cleans up database after test completes
 * - Each test is isolated and independent
 */
class QueryGeneratorDecFloatTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        TempDatabaseTest::SetUp();
        createTestSchema();
    }

    /**
     * Create test table schema
     *
     * Table: DECFLOAT_TEST
     * - F_ID: INTEGER PRIMARY KEY
     * - F_DECFLOAT16: DECFLOAT(16) - IEEE 754-2008 decimal64 (8 bytes)
     * - F_DECFLOAT16_NULLABLE: DECFLOAT(16) - nullable variant
     * - F_DECFLOAT34: DECFLOAT(34) - IEEE 754-2008 decimal128 (16 bytes)
     * - F_DECFLOAT34_NULLABLE: DECFLOAT(34) - nullable variant
     * - F_NAME: VARCHAR(100)
     */
    void createTestSchema() override {
        try {
            connection_->ExecuteDDL(
                "CREATE TABLE DECFLOAT_TEST ("
                "    F_ID INTEGER NOT NULL PRIMARY KEY,"
                "    F_DECFLOAT16 DECFLOAT(16),"
                "    F_DECFLOAT16_NULLABLE DECFLOAT(16),"
                "    F_DECFLOAT34 DECFLOAT(34),"
                "    F_DECFLOAT34_NULLABLE DECFLOAT(34),"
                "    F_NAME VARCHAR(100)"
                ")"
            );
        } catch (...) {
            // Table already exists, ignore
        }
    }
};

/**
 * Test: INSERT + SELECT with CppDecimal types
 *
 * Verifies:
 * - InsertDecFloatTypes query works with CppDecimal adapters
 * - SelectDecFloatById query retrieves correct values
 * - DecFloat16 and DecFloat34 values round-trip correctly
 */
TEST_F(QueryGeneratorDecFloatTest, InsertAndSelectDecFloatTypes) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertDecFloatTypes>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectDecFloatById>::positionalSql)
    );

    // 1. INSERT test data
    auto insertTxn = connection_->StartTransaction();

    InsertDecFloatTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID
    insertInput.param2 = fbpp::adapters::DecFloat16("123.456");  // F_DECFLOAT16
    insertInput.param3 = fbpp::adapters::DecFloat16("789.012");  // F_DECFLOAT16_NULLABLE
    insertInput.param4 = fbpp::adapters::DecFloat34("123456789012345678901234567890.1234");  // F_DECFLOAT34
    insertInput.param5 = fbpp::adapters::DecFloat34("987654321098765432109876543210.9876");  // F_DECFLOAT34_NULLABLE
    insertInput.param6 = "Test DecFloat";  // F_NAME

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // 2. SELECT and verify
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));

    // Expected tuple type for SELECT result
    std::tuple<
        fbpp::adapters::DecFloat16,               // F_DECFLOAT16
        std::optional<fbpp::adapters::DecFloat16>,  // F_DECFLOAT16_NULLABLE
        fbpp::adapters::DecFloat34,               // F_DECFLOAT34
        std::optional<fbpp::adapters::DecFloat34>,  // F_DECFLOAT34_NULLABLE
        std::string                               // F_NAME
    > result;

    ASSERT_TRUE(resultSet->fetch(result));

    // Verify DECFLOAT16 values
    EXPECT_EQ(std::get<0>(result).to_string(), "123.456");

    // Verify nullable DECFLOAT16
    ASSERT_TRUE(std::get<1>(result).has_value());
    EXPECT_EQ(std::get<1>(result)->to_string(), "789.012");

    // Verify DECFLOAT34 values
    EXPECT_EQ(std::get<2>(result).to_string(), "123456789012345678901234567890.1234");

    // Verify nullable DECFLOAT34
    ASSERT_TRUE(std::get<3>(result).has_value());
    EXPECT_EQ(std::get<3>(result)->to_string(), "987654321098765432109876543210.9876");

    // Verify name
    EXPECT_EQ(std::get<4>(result), "Test DecFloat");

    // Verify no more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: UPDATE + SELECT with DECFLOAT34
 *
 * Verifies:
 * - UpdateDecFloat34 query works correctly
 * - DECFLOAT34 values update properly
 * - Can SELECT updated value back
 */
TEST_F(QueryGeneratorDecFloatTest, UpdateDecFloat34) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertDecFloatTypes>::positionalSql)
    );

    auto updateStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::UpdateDecFloat34>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectDecFloatById>::positionalSql)
    );

    // 1. INSERT test data
    auto insertTxn = connection_->StartTransaction();

    InsertDecFloatTypesIn insertInput;
    insertInput.param1 = 1;  // F_ID
    insertInput.param2 = fbpp::adapters::DecFloat16("100.00");
    insertInput.param3 = std::nullopt;  // NULL
    insertInput.param4 = fbpp::adapters::DecFloat34("1000.0000");
    insertInput.param5 = std::nullopt;  // NULL
    insertInput.param6 = "Update Test";

    insertTxn->execute(insertStmt, insertInput);
    insertTxn->Commit();

    // 2. UPDATE DECFLOAT34 value
    auto updateTxn = connection_->StartTransaction();

    UpdateDecFloat34In updateInput;
    updateInput.param1 = fbpp::adapters::DecFloat34("999999999999999999999999999999.9999");  // New DECFLOAT34
    updateInput.param2 = 1;  // WHERE F_ID = 1

    updateTxn->execute(updateStmt, updateInput);
    updateTxn->Commit();

    // 3. SELECT and verify updated value
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(1));

    std::tuple<
        fbpp::adapters::DecFloat16,
        std::optional<fbpp::adapters::DecFloat16>,
        fbpp::adapters::DecFloat34,
        std::optional<fbpp::adapters::DecFloat34>,
        std::string
    > result;

    ASSERT_TRUE(resultSet->fetch(result));

    // Verify DECFLOAT34 was updated
    EXPECT_EQ(std::get<2>(result).to_string(), "999999999999999999999999999999.9999");

    // Other fields should remain unchanged
    EXPECT_EQ(std::get<0>(result).to_string(), "100.00");
    EXPECT_FALSE(std::get<1>(result).has_value());  // Still NULL
    EXPECT_EQ(std::get<4>(result), "Update Test");
}

/**
 * Test: SELECT with WHERE clause on DECFLOAT34
 *
 * Verifies:
 * - SelectDecFloat34Greater query filters correctly
 * - WHERE clause works with DECFLOAT34 comparisons
 * - Can SELECT subset of columns
 */
TEST_F(QueryGeneratorDecFloatTest, SelectDecFloat34Greater) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertDecFloatTypes>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectDecFloat34Greater>::positionalSql)
    );

    // 1. INSERT test data - 3 rows with different DECFLOAT34 values
    auto insertTxn = connection_->StartTransaction();

    // Row 1: DECFLOAT34 = 100
    InsertDecFloatTypesIn input1;
    input1.param1 = 1;
    input1.param2 = fbpp::adapters::DecFloat16("1.0");
    input1.param3 = std::nullopt;
    input1.param4 = fbpp::adapters::DecFloat34("100");
    input1.param5 = std::nullopt;
    input1.param6 = "Row 1";
    insertTxn->execute(insertStmt, input1);

    // Row 2: DECFLOAT34 = 500
    InsertDecFloatTypesIn input2;
    input2.param1 = 2;
    input2.param2 = fbpp::adapters::DecFloat16("2.0");
    input2.param3 = std::nullopt;
    input2.param4 = fbpp::adapters::DecFloat34("500");
    input2.param5 = std::nullopt;
    input2.param6 = "Row 2";
    insertTxn->execute(insertStmt, input2);

    // Row 3: DECFLOAT34 = 1000
    InsertDecFloatTypesIn input3;
    input3.param1 = 3;
    input3.param2 = fbpp::adapters::DecFloat16("3.0");
    input3.param3 = std::nullopt;
    input3.param4 = fbpp::adapters::DecFloat34("1000");
    input3.param5 = std::nullopt;
    input3.param6 = "Row 3";
    insertTxn->execute(insertStmt, input3);

    insertTxn->Commit();

    // 2. SELECT rows with DECFLOAT34 > 250 (should get rows 2 and 3)
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt, std::make_tuple(fbpp::adapters::DecFloat34("250")));

    std::tuple<
        fbpp::adapters::DecFloat34,  // F_DECFLOAT34
        std::string                  // F_NAME
    > result;

    // First result: Row 2 (DECFLOAT34 = 500)
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result).to_string(), "500");
    EXPECT_EQ(std::get<1>(result), "Row 2");

    // Second result: Row 3 (DECFLOAT34 = 1000)
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result).to_string(), "1000");
    EXPECT_EQ(std::get<1>(result), "Row 3");

    // No more results
    EXPECT_FALSE(resultSet->fetch(result));
}

/**
 * Test: SELECT ALL rows
 *
 * Verifies:
 * - SelectAllDecFloat query returns all rows
 * - Query without parameters works correctly
 * - All columns retrieved correctly
 */
TEST_F(QueryGeneratorDecFloatTest, SelectAllDecFloat) {
    // Create prepared statements
    auto insertStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::InsertDecFloatTypes>::positionalSql)
    );

    auto selectStmt = connection_->prepareStatement(
        std::string(QueryDescriptor<QueryId::SelectAllDecFloat>::positionalSql)
    );

    // 1. INSERT test data - 2 rows
    auto insertTxn = connection_->StartTransaction();

    // Row 1
    InsertDecFloatTypesIn input1;
    input1.param1 = 1;
    input1.param2 = fbpp::adapters::DecFloat16("111.111");
    input1.param3 = fbpp::adapters::DecFloat16("222.222");
    input1.param4 = fbpp::adapters::DecFloat34("333333333333333333.333333");
    input1.param5 = fbpp::adapters::DecFloat34("444444444444444444.444444");
    input1.param6 = "First";
    insertTxn->execute(insertStmt, input1);

    // Row 2
    InsertDecFloatTypesIn input2;
    input2.param1 = 2;
    input2.param2 = fbpp::adapters::DecFloat16("555.555");
    input2.param3 = std::nullopt;  // NULL
    input2.param4 = fbpp::adapters::DecFloat34("666666666666666666.666666");
    input2.param5 = std::nullopt;  // NULL
    input2.param6 = "Second";
    insertTxn->execute(insertStmt, input2);

    insertTxn->Commit();

    // 2. SELECT ALL rows (no parameters)
    auto selectTxn = connection_->StartTransaction();
    auto resultSet = selectTxn->openCursor(selectStmt);  // No parameters

    std::tuple<
        int32_t,                                        // F_ID
        fbpp::adapters::DecFloat16,                     // F_DECFLOAT16
        std::optional<fbpp::adapters::DecFloat16>,      // F_DECFLOAT16_NULLABLE
        fbpp::adapters::DecFloat34,                     // F_DECFLOAT34
        std::optional<fbpp::adapters::DecFloat34>,      // F_DECFLOAT34_NULLABLE
        std::string                                     // F_NAME
    > result;

    // First row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 1);
    EXPECT_EQ(std::get<1>(result).to_string(), "111.111");
    ASSERT_TRUE(std::get<2>(result).has_value());
    EXPECT_EQ(std::get<2>(result)->to_string(), "222.222");
    EXPECT_EQ(std::get<3>(result).to_string(), "333333333333333333.333333");
    ASSERT_TRUE(std::get<4>(result).has_value());
    EXPECT_EQ(std::get<4>(result)->to_string(), "444444444444444444.444444");
    EXPECT_EQ(std::get<5>(result), "First");

    // Second row
    ASSERT_TRUE(resultSet->fetch(result));
    EXPECT_EQ(std::get<0>(result), 2);
    EXPECT_EQ(std::get<1>(result).to_string(), "555.555");
    EXPECT_FALSE(std::get<2>(result).has_value());  // NULL
    EXPECT_EQ(std::get<3>(result).to_string(), "666666666666666666.666666");
    EXPECT_FALSE(std::get<4>(result).has_value());  // NULL
    EXPECT_EQ(std::get<5>(result), "Second");

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
TEST_F(QueryGeneratorDecFloatTest, GeneratedTypesCompile) {
    // Test QueryDescriptor access
    static_assert(QueryDescriptor<QueryId::InsertDecFloatTypes>::id == QueryId::InsertDecFloatTypes);

    auto queryName = QueryDescriptor<QueryId::InsertDecFloatTypes>::name;
    EXPECT_STREQ(queryName, "InsertDecFloatTypes");

    // Test struct instantiation
    InsertDecFloatTypesIn input;
    input.param1 = 1;
    input.param2 = fbpp::adapters::DecFloat16("1.23");
    input.param3 = std::nullopt;
    input.param4 = fbpp::adapters::DecFloat34("4.56");
    input.param5 = std::nullopt;
    input.param6 = "Test";

    EXPECT_EQ(input.param1, 1);
    EXPECT_EQ(input.param6, "Test");

    // Test output struct
    SelectAllDecFloatOut output;
    output.fId = 42;
    output.fDecfloat16 = fbpp::adapters::DecFloat16("7.89");
    output.fDecfloat34 = fbpp::adapters::DecFloat34("1.23");
    output.fName = "Output test";

    EXPECT_EQ(output.fId, 42);
    EXPECT_EQ(output.fName, "Output test");
}
