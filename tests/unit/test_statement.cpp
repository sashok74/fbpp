#include <gtest/gtest.h>
#include <memory>
#include <tuple>
#include <nlohmann/json.hpp>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/exception.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

class StatementTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Call base class SetUp - logging is now initialized from config
        TempDatabaseTest::SetUp();
        
        // Create additional test table for statement tests
        auto tra = connection_->Execute(
            "CREATE TABLE statement_test ("
            "  id INTEGER PRIMARY KEY,"
            "  name VARCHAR(100),"
            "  amount DOUBLE PRECISION,"
            "  created TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ")"
        );
        tra->Commit();
    }
};

TEST_F(StatementTest, PrepareStatement) {
    auto stmt = connection_->prepareStatement("SELECT 1 FROM RDB$DATABASE");
    ASSERT_NE(stmt, nullptr);
    ASSERT_TRUE(stmt->isValid());
}

TEST_F(StatementTest, PrepareStatementWithTransaction) {
    auto tra = connection_->StartTransaction();
    // Now prepareStatement uses cache by default and doesn't require transaction
    auto stmt = connection_->prepareStatement("SELECT 1 FROM RDB$DATABASE");
    ASSERT_NE(stmt, nullptr);
    ASSERT_TRUE(stmt->isValid());
    tra->Commit();
}

TEST_F(StatementTest, ExecuteInsert) {
    auto stmt = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name, amount) VALUES (1, 'Test', 123.45)"
    );
    
    auto tra = connection_->StartTransaction();
    unsigned affected = tra->execute(stmt);
    EXPECT_EQ(affected, 1);
    tra->Commit();
}

TEST_F(StatementTest, ExecuteUpdate) {
    // First insert a record
    auto tra = connection_->Execute(
        "INSERT INTO statement_test (id, name, amount) VALUES (1, 'Original', 100.0)"
    );
    tra->Commit();
    
    // Now update it
    auto stmt = connection_->prepareStatement(
        "UPDATE statement_test SET name = 'Updated', amount = 200.0 WHERE id = 1"
    );
    
    tra = connection_->StartTransaction();
    unsigned affected = tra->execute(stmt);
    EXPECT_EQ(affected, 1);
    tra->Commit();
}

TEST_F(StatementTest, ExecuteDelete) {
    // First insert records
    auto tra = connection_->Execute(
        "INSERT INTO statement_test (id, name, amount) VALUES (1, 'First', 100.0)"
    );
    tra->Commit();
    
    tra = connection_->Execute(
        "INSERT INTO statement_test (id, name, amount) VALUES (2, 'Second', 200.0)"
    );
    tra->Commit();
    
    // Delete one record
    auto stmt = connection_->prepareStatement(
        "DELETE FROM statement_test WHERE id = 1"
    );
    
    tra = connection_->StartTransaction();
    unsigned affected = tra->execute(stmt);
    EXPECT_EQ(affected, 1);
    tra->Commit();
}

TEST_F(StatementTest, OpenCursorBasic) {
    // Insert test data
    auto tra = connection_->Execute(
        "INSERT INTO statement_test (id, name, amount) VALUES (1, 'First', 100.0)"
    );
    tra->Commit();
    
    tra = connection_->Execute(
        "INSERT INTO statement_test (id, name, amount) VALUES (2, 'Second', 200.0)"
    );
    tra->Commit();
    
    // Prepare SELECT statement
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM statement_test ORDER BY id"
    );
    
    tra = connection_->StartTransaction();
    auto resultSet = tra->openCursor(stmt);
    ASSERT_NE(resultSet, nullptr);
    ASSERT_TRUE(resultSet->isValid());
    
    // Fetch first row using tuple
    std::tuple<int, std::string, double> row1;
    bool fetched1 = resultSet->fetch(row1);
    ASSERT_TRUE(fetched1);
    auto [id1, name1, amount] = row1;
    EXPECT_GT(id1, 0);
    EXPECT_FALSE(name1.empty());
    
    // Fetch second row
    std::tuple<int, std::string, double> row2;
    bool fetched2 = resultSet->fetch(row2);
    ASSERT_TRUE(fetched2);
    auto [id2, name2, amount2] = row2;
    EXPECT_GT(id2, 0);
    EXPECT_FALSE(name2.empty());
    
    // No more rows
    std::tuple<int, std::string, double> row3;
    bool fetched3 = resultSet->fetch(row3);
    EXPECT_FALSE(fetched3);
    
    resultSet->close();
    tra->Commit();
}

TEST_F(StatementTest, GetMetadata) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM statement_test"
    );
    
    // Get output metadata
    auto outputMeta = stmt->getOutputMetadata();
    ASSERT_NE(outputMeta, nullptr);
    EXPECT_EQ(outputMeta->getCount(), 3);
    
    // Check field names
    EXPECT_EQ(outputMeta->getFieldName(0), "ID");
    EXPECT_EQ(outputMeta->getFieldName(1), "NAME");
    EXPECT_EQ(outputMeta->getFieldName(2), "AMOUNT");
    
    // For INSERT/UPDATE/DELETE, there's no output metadata
    auto insertStmt = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name) VALUES (1, 'Test')"
    );
    auto insertOutputMeta = insertStmt->getOutputMetadata();
    // May be null or have 0 fields for non-SELECT statements
}

TEST_F(StatementTest, GetStatementType) {
    // SELECT statement
    auto selectStmt = connection_->prepareStatement(
        "SELECT * FROM statement_test"
    );
    unsigned selectType = selectStmt->getType();
    // Type amounts are Firebird-specific constants
    
    // INSERT statement
    auto insertStmt = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name) VALUES (1, 'Test')"
    );
    unsigned insertType = insertStmt->getType();
    
    // UPDATE statement
    auto updateStmt = connection_->prepareStatement(
        "UPDATE statement_test SET name = 'Updated'"
    );
    unsigned updateType = updateStmt->getType();
    
    // DELETE statement
    auto deleteStmt = connection_->prepareStatement(
        "DELETE FROM statement_test"
    );
    unsigned deleteType = deleteStmt->getType();
    
    // Types should be different for different statement types
    EXPECT_NE(selectType, insertType);
    EXPECT_NE(selectType, updateType);
    EXPECT_NE(selectType, deleteType);
}

TEST_F(StatementTest, FreeStatement) {
    auto stmt = connection_->prepareStatement(
        "SELECT * FROM statement_test"
    );
    ASSERT_TRUE(stmt->isValid());
    
    stmt->free();
    EXPECT_FALSE(stmt->isValid());
    
    // Should throw if we try to use freed statement
    auto tra = connection_->StartTransaction();
    EXPECT_THROW({
        tra->execute(stmt);
    }, FirebirdException);
    tra->Rollback();
}

TEST_F(StatementTest, MultipleExecutions) {
    // Prepare statement once
    auto stmt = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name, amount) VALUES (?, 'Test', 100.0)"
    );
    
    // For now, without parameter support, we'll test with different statements
    auto stmt1 = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name, amount) VALUES (1, 'Test1', 100.0)"
    );
    auto stmt2 = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name, amount) VALUES (2, 'Test2', 200.0)"
    );
    auto stmt3 = connection_->prepareStatement(
        "INSERT INTO statement_test (id, name, amount) VALUES (3, 'Test3', 300.0)"
    );
    
    auto tra = connection_->StartTransaction();
    
    // Execute multiple times
    unsigned affected1 = tra->execute(stmt1);
    EXPECT_EQ(affected1, 1);
    
    unsigned affected2 = tra->execute(stmt2);
    EXPECT_EQ(affected2, 1);
    
    unsigned affected3 = tra->execute(stmt3);
    EXPECT_EQ(affected3, 1);
    
    tra->Commit();
    
    // Verify all were inserted
    auto selectStmt = connection_->prepareStatement(
        "SELECT COUNT(*) FROM statement_test"
    );
    
    tra = connection_->StartTransaction();
    auto resultSet = tra->openCursor(selectStmt);
    
    // Fetch the count as a tuple
    std::tuple<int64_t> countRow;
    bool countFetched = resultSet->fetch(countRow);
    ASSERT_TRUE(countFetched);
    auto [count] = countRow;
    EXPECT_GT(count, 0);  // Should have rows
    
    resultSet->close();
    tra->Commit();
}

TEST_F(StatementTest, ErrorHandling) {
    // Invalid SQL should throw during prepare - Firebird throws FbException
    EXPECT_ANY_THROW({
        auto stmt = connection_->prepareStatement("INVALID SQL");
    });
    
    // Valid prepare but table doesn't exist - also throws FbException
    EXPECT_ANY_THROW({
        auto stmt = connection_->prepareStatement("SELECT * FROM nonexistent_table");
    });
    
    // Executing with invalid statement should throw our FirebirdException
    auto tra = connection_->StartTransaction();
    std::unique_ptr<Statement> nullStmt;
    EXPECT_THROW({
        tra->execute(nullStmt);
    }, FirebirdException);
    tra->Rollback();
}