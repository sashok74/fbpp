#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/named_param_parser.hpp"
#include "fbpp/core/named_param_helper.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp_util/logging.h"
#include <nlohmann/json.hpp>

using namespace fbpp::core;
using namespace fbpp::test;
using json = nlohmann::json;

// Test class for tests that don't need database
class NamedParametersTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging if needed
        auto logger = util::Logging::get();
        // Logger initialization is already done by library
    }

    void TearDown() override {
    }
};

// Test class for tests that need database connection
class NamedParametersDbTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Call base class SetUp
        TempDatabaseTest::SetUp();

        auto logger = util::Logging::get();
        if (logger) {
            logger->info("=== Starting test: {} ===",
                        ::testing::UnitTest::GetInstance()->current_test_info()->name());
        }
    }

    void TearDown() override {
        auto logger = util::Logging::get();
        if (logger) {
            logger->info("=== Ending test: {} ===",
                        ::testing::UnitTest::GetInstance()->current_test_info()->name());
        }

        // Call base class TearDown
        TempDatabaseTest::TearDown();
    }

    void createTestSchema() override {
        // Base class creates test_table
        TempDatabaseTest::createTestSchema();

        // Add additional table for named params tests
        connection_->ExecuteDDL(R"(
            CREATE TABLE test_named_params (
                id INTEGER,
                name VARCHAR(100),
                status VARCHAR(20)
            )
        )");
    }
};

// Test parser with simple named parameters
TEST_F(NamedParametersTest, ParseSimpleNamedParams) {
    std::string sql = "SELECT * FROM users WHERE id = :user_id AND status = :status";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "SELECT * FROM users WHERE id = ? AND status = ?");
    EXPECT_EQ(result.parameters.size(), 2);

    EXPECT_EQ(result.parameters[0].name, "user_id");
    EXPECT_EQ(result.parameters[0].position, 0);

    EXPECT_EQ(result.parameters[1].name, "status");
    EXPECT_EQ(result.parameters[1].position, 1);
}

// Test parser with @ prefix
TEST_F(NamedParametersTest, ParseAtSignParams) {
    std::string sql = "UPDATE users SET name = @name WHERE id = @id";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "UPDATE users SET name = ? WHERE id = ?");
    EXPECT_EQ(result.parameters.size(), 2);

    EXPECT_EQ(result.parameters[0].name, "name");
    EXPECT_EQ(result.parameters[1].name, "id");
}

// Test parser with repeated parameters
TEST_F(NamedParametersTest, ParseRepeatedParams) {
    std::string sql = "SELECT * FROM orders WHERE user_id = :user_id OR manager_id = :user_id";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "SELECT * FROM orders WHERE user_id = ? OR manager_id = ?");
    EXPECT_EQ(result.parameters.size(), 2);

    // Both should have the same name but different positions
    EXPECT_EQ(result.parameters[0].name, "user_id");
    EXPECT_EQ(result.parameters[0].position, 0);
    EXPECT_EQ(result.parameters[1].name, "user_id");
    EXPECT_EQ(result.parameters[1].position, 1);

    // Check name-to-positions mapping
    EXPECT_EQ(result.nameToPositions["user_id"].size(), 2);
    EXPECT_EQ(result.nameToPositions["user_id"][0], 0);
    EXPECT_EQ(result.nameToPositions["user_id"][1], 1);
}

// Test parser with mixed named and positional
TEST_F(NamedParametersTest, ParseMixedParams) {
    std::string sql = "INSERT INTO logs (user_id, message, timestamp) VALUES (:user_id, ?, :timestamp)";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "INSERT INTO logs (user_id, message, timestamp) VALUES (?, ?, ?)");
    EXPECT_EQ(result.parameters.size(), 2);  // Only named params

    EXPECT_EQ(result.parameters[0].name, "user_id");
    EXPECT_EQ(result.parameters[0].position, 0);

    EXPECT_EQ(result.parameters[1].name, "timestamp");
    EXPECT_EQ(result.parameters[1].position, 2);  // Position 1 is the positional ?
}

// Test parser with string literals
TEST_F(NamedParametersTest, ParseWithStringLiterals) {
    std::string sql = "SELECT * FROM users WHERE name = ':not_a_param' AND id = :user_id";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "SELECT * FROM users WHERE name = ':not_a_param' AND id = ?");
    EXPECT_EQ(result.parameters.size(), 1);

    EXPECT_EQ(result.parameters[0].name, "user_id");
}

// Test parser with comments
TEST_F(NamedParametersTest, ParseWithComments) {
    std::string sql = "-- Comment with :fake_param\nSELECT * FROM users WHERE id = :user_id /* :another_fake */";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.convertedSql, "-- Comment with :fake_param\nSELECT * FROM users WHERE id = ? /* :another_fake */");
    EXPECT_EQ(result.parameters.size(), 1);

    EXPECT_EQ(result.parameters[0].name, "user_id");
}

// Test case-insensitive parameter names
TEST_F(NamedParametersTest, CaseInsensitiveParams) {
    std::string sql = "SELECT * FROM users WHERE id = :UserId";
    auto result = NamedParamParser::parse(sql);

    EXPECT_TRUE(result.hasNamedParams);
    EXPECT_EQ(result.parameters[0].name, "userid");  // Should be lowercase
    EXPECT_TRUE(result.nameToPositions.find("userid") != result.nameToPositions.end());
}

// Test NamedParamHelper JSON conversion
TEST_F(NamedParametersTest, ConvertJsonToPositional) {
    std::unordered_map<std::string, std::vector<size_t>> mapping = {
        {"user_id", {0, 2}},
        {"status", {1}}
    };

    json input = json::object();
    input["user_id"] = 123;
    input["status"] = "active";

    json result = NamedParamHelper::convertToPositional(input, mapping, 3);

    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 123);
    EXPECT_EQ(result[1], "active");
    EXPECT_EQ(result[2], 123);  // Repeated parameter
}

// Test NamedParamHelper with missing parameters (should be null)
TEST_F(NamedParametersTest, ConvertJsonWithMissingParams) {
    std::unordered_map<std::string, std::vector<size_t>> mapping = {
        {"user_id", {0}},
        {"status", {1}},
        {"timestamp", {2}}
    };

    json input = json::object();
    input["user_id"] = 456;
    // status and timestamp not provided

    json result = NamedParamHelper::convertToPositional(input, mapping, 3);

    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 456);
    EXPECT_TRUE(result[1].is_null());
    EXPECT_TRUE(result[2].is_null());
}

// Test NamedParamHelper with case-insensitive keys
TEST_F(NamedParametersTest, ConvertJsonCaseInsensitive) {
    std::unordered_map<std::string, std::vector<size_t>> mapping = {
        {"user_id", {0}},  // Lowercase in mapping
        {"status", {1}}
    };

    json input = json::object();
    input["USER_ID"] = 789;  // Uppercase in JSON
    input["Status"] = "pending";  // Mixed case

    json result = NamedParamHelper::convertToPositional(input, mapping, 2);

    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 789);
    EXPECT_EQ(result[1], "pending");
}

// Integration test with real database connection
TEST_F(NamedParametersDbTest, ExecuteWithNamedParams) {
    // connection_ is already created by base class
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());

    auto transaction = connection_->StartTransaction();

    // Insert with named parameters (table already created in createTestSchema)
    auto stmt = connection_->prepareStatement("INSERT INTO test_named_params (id, name, status) VALUES (:id, :name, :status)");

    json insertParams = json::object();
    insertParams["id"] = 1;
    insertParams["name"] = "Test User";
    insertParams["status"] = "active";

    transaction->execute(stmt, insertParams);

    // Query with named parameters (repeated parameter)
    auto queryStmt = connection_->prepareStatement(
        "SELECT * FROM test_named_params WHERE id = :id OR status = :status OR id = :id");

    json queryParams = json::object();
    queryParams["id"] = 1;
    queryParams["status"] = "active";

    auto cursor = transaction->openCursor(queryStmt, queryParams);

    bool hasResults = false;
    json row;
    while (cursor->fetch(row)) {
        hasResults = true;
        // Row can be array or object depending on fetch mode
        if (row.is_array()) {
            EXPECT_EQ(row[0], 1);
            EXPECT_EQ(row[1], "Test User");
            EXPECT_EQ(row[2], "active");
        } else if (row.is_object()) {
            EXPECT_EQ(row["ID"], 1);
            EXPECT_EQ(row["NAME"], "Test User");
            EXPECT_EQ(row["STATUS"], "active");
        }
    }

    EXPECT_TRUE(hasResults);

    transaction->Commit();
}

// Test Statement's named parameter mapping getters
TEST_F(NamedParametersDbTest, StatementNamedParamGetters) {
    // connection_ is already created by base class
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());

    try {
        // Create statement with named parameters and explicit casts to give Firebird type information
        const char* sql =
            "SELECT * FROM rdb$database "
            "WHERE CAST(:first AS VARCHAR(20)) = CAST(:first AS VARCHAR(20)) "
            "AND CAST(:second AS INTEGER) = 1";

        auto stmt = connection_->prepareStatement(sql);

        // Check that statement has named parameters
        EXPECT_TRUE(stmt->hasNamedParameters());

        // Get mapping
        auto mapping = stmt->getNamedParamMapping();
        EXPECT_EQ(mapping.size(), 2);  // Two unique parameter names

        // Check param1 appears twice
        auto it = mapping.find("first");
        EXPECT_NE(it, mapping.end());
        EXPECT_EQ(it->second.size(), 2);  // Used twice

        // Check param2 appears once
        it = mapping.find("second");
        EXPECT_NE(it, mapping.end());
        EXPECT_EQ(it->second.size(), 1);  // Used once
    } catch (const Firebird::FbException& fbex) {
        fbpp::core::FirebirdException wrapped(fbex);
        FAIL() << "Firebird FbException: " << wrapped.what();
    } catch (const std::exception& e) {
        FAIL() << "Unexpected exception: " << e.what();
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
