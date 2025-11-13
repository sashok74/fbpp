// Integration test for query_generator with TTMath adapter support
// This test verifies that generated code with TTMath adapters compiles and works correctly

#include <gtest/gtest.h>

// Include the generated headers
#include "/tmp/generated_queries_ttmath.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"

using namespace fbpp::core;
using namespace generated::queries;

class QueryGeneratorTTMathTest : public ::testing::Test {
protected:
    void SetUp() override {
        ConnectionParams params;
        params.database = "192.168.7.248:/mnt/test/binding_test.fdb";
        params.user = "SYSDBA";
        params.password = "planomer";
        params.charset = "UTF8";

        connection_ = std::make_unique<Connection>(params);
    }

    void TearDown() override {
        connection_.reset();
    }

    std::unique_ptr<Connection> connection_;
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
    Transaction txn(*connection_);

    // Prepare insert statement
    auto stmt = connection_->prepare(
        QueryDescriptor<QueryId::InsertTTMathTypes>::positionalSql
    );

    // Create input data using TTMath adapters
    InsertTTMathTypesIn input;
    input.param1 = 1;  // F_ID
    input.param2 = fbpp::adapters::TTInt128("123456789012345678901234567890");  // F_INT128_PURE
    input.param3 = fbpp::adapters::TTInt128("987654321098765432109876543210");  // F_INT128_NULLABLE
    input.param4 = fbpp::adapters::TTNumeric<2, -2>("12345678901234567890.12");  // F_NUMERIC38_2
    input.param5 = fbpp::adapters::TTNumeric<2, -4>("1234567890123456.1234");    // F_NUMERIC38_4
    input.param6 = fbpp::adapters::TTNumeric<2, -8>("123456789012.12345678");    // F_NUMERIC38_8
    input.param7 = fbpp::adapters::TTNumeric<2, -18>("12.123456789012345678");   // F_NUMERIC38_18
    input.param8 = fbpp::adapters::TTNumeric<1, -2>("1234567890123456.12");      // F_NUMERIC18_2
    input.param9 = fbpp::adapters::TTNumeric<1, -6>("123456789012.123456");      // F_NUMERIC18_6
    input.param10 = "Test TTMath";  // F_NAME

    // Execute insert
    stmt.execute(txn, input);
    txn.commit();

    // Select back the data
    Transaction selectTxn(*connection_);
    auto selectStmt = connection_->prepare(
        QueryDescriptor<QueryId::SelectTTMathById>::positionalSql
    );

    SelectTTMathByIdIn selectInput;
    selectInput.param1 = 1;

    auto resultSet = selectStmt.executeQuery(selectTxn, selectInput);

    // Fetch and verify
    auto output = resultSet.fetchOne<SelectTTMathByIdOut>();
    ASSERT_TRUE(output.has_value());

    // Verify INT128 values
    EXPECT_EQ(output->fInt128Pure.toString(), "123456789012345678901234567890");
    ASSERT_TRUE(output->fInt128Nullable.has_value());
    EXPECT_EQ(output->fInt128Nullable->toString(), "987654321098765432109876543210");

    // Verify NUMERIC38 values
    EXPECT_EQ(output->fNumeric382.toString(), "12345678901234567890.12");
    ASSERT_TRUE(output->fNumeric384.has_value());
    EXPECT_EQ(output->fNumeric384->toString(), "1234567890123456.1234");
    EXPECT_EQ(output->fNumeric388.toString(), "123456789012.12345678");

    // Verify NUMERIC18 values
    EXPECT_EQ(output->fNumeric182.toString(), "1234567890123456.12");
    ASSERT_TRUE(output->fNumeric186.has_value());
    EXPECT_EQ(output->fNumeric186->toString(), "123456789012.123456");

    // Verify name
    ASSERT_TRUE(output->fName.has_value());
    EXPECT_EQ(*output->fName, "Test TTMath");

    selectTxn.commit();
}

TEST_F(QueryGeneratorTTMathTest, UpdateNumericTypes) {
    Transaction txn(*connection_);

    auto updateStmt = connection_->prepare(
        QueryDescriptor<QueryId::UpdateNumeric38>::positionalSql
    );

    UpdateNumeric38In updateInput;
    updateInput.param1 = fbpp::adapters::TTNumeric<2, -2>("99999999999999999999.99");  // F_NUMERIC38_2
    updateInput.param2 = fbpp::adapters::TTNumeric<2, -8>("999999999999.99999999");    // F_NUMERIC38_8
    updateInput.param3 = 1;  // F_ID

    updateStmt.execute(txn, updateInput);
    txn.commit();

    // Verify update
    Transaction selectTxn(*connection_);
    auto selectStmt = connection_->prepare(
        "SELECT F_NUMERIC38_2, F_NUMERIC38_8 FROM TTMATH_TEST WHERE F_ID = ?"
    );

    auto resultSet = selectStmt.executeQuery(selectTxn, std::make_tuple(1));
    auto result = resultSet.fetchOne<fbpp::adapters::TTNumeric<2, -2>, fbpp::adapters::TTNumeric<2, -8>>();

    ASSERT_TRUE(result.has_value());
    auto [numeric38_2, numeric38_8] = *result;

    EXPECT_EQ(numeric38_2.toString(), "99999999999999999999.99");
    EXPECT_EQ(numeric38_8.toString(), "999999999999.99999999");

    selectTxn.commit();
}

TEST_F(QueryGeneratorTTMathTest, SelectScaledTypesWithFilter) {
    Transaction txn(*connection_);

    auto stmt = connection_->prepare(
        QueryDescriptor<QueryId::SelectScaledTypes>::positionalSql
    );

    SelectScaledTypesIn input;
    input.param1 = fbpp::adapters::TTNumeric<2, -2>("0.00");  // Filter: F_NUMERIC38_2 > 0

    auto resultSet = stmt.executeQuery(txn, input);

    // Should return the row we inserted
    auto output = resultSet.fetchOne<SelectScaledTypesOut>();
    ASSERT_TRUE(output.has_value());

    // Verify scaled numeric types are present
    EXPECT_FALSE(output->fNumeric382.toString().empty());
    EXPECT_FALSE(output->fNumeric388.toString().empty());
    EXPECT_FALSE(output->fNumeric182.toString().empty());

    txn.commit();
}

TEST_F(QueryGeneratorTTMathTest, SelectAllTTMath) {
    Transaction txn(*connection_);

    auto stmt = connection_->prepare(
        QueryDescriptor<QueryId::SelectAllTTMath>::positionalSql
    );

    SelectAllTTMathIn input;  // No parameters

    auto resultSet = stmt.executeQuery(txn, input);

    // Fetch all rows
    int rowCount = 0;
    while (auto row = resultSet.fetchOne<SelectAllTTMathOut>()) {
        rowCount++;

        // Verify structure
        EXPECT_GT(row->fId, 0);
        EXPECT_FALSE(row->fInt128Pure.toString().empty());

        // All rows should have these required fields
        EXPECT_FALSE(row->fNumeric382.toString().empty());
        EXPECT_FALSE(row->fNumeric388.toString().empty());
        EXPECT_FALSE(row->fNumeric182.toString().empty());
    }

    EXPECT_GT(rowCount, 0) << "Should have at least one row";

    txn.commit();
}
