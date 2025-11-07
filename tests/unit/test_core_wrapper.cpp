#include <gtest/gtest.h>
#include <memory>
#include "../test_base.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/exception.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

class CoreWrapperTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Call base class SetUp - logging was initialized previously
        TempDatabaseTest::SetUp();
    }
    
    void TearDown() override {
        // Call base class TearDown
        TempDatabaseTest::TearDown();
    }
};

TEST_F(CoreWrapperTest, EnvironmentSingleton) {
    Environment& env1 = Environment::getInstance();
    Environment& env2 = Environment::getInstance();
    
    ASSERT_EQ(&env1, &env2);
}

TEST_F(CoreWrapperTest, EnvironmentInterfaces) {
    Environment& env = Environment::getInstance();
    
    ASSERT_NE(env.getMaster(), nullptr);
    ASSERT_NE(env.getProvider(), nullptr);
    ASSERT_NE(env.getUtil(), nullptr);
}

TEST_F(CoreWrapperTest, ConnectionCreate) {
    // connection_ is already created by base class
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());
}

TEST_F(CoreWrapperTest, ConnectionExecute) {
    auto tra = connection_->Execute("INSERT INTO test_table (id, name, amount) VALUES (1, 'test', 3.14)");
    ASSERT_NE(tra, nullptr);
    
    tra->Commit();
}

TEST_F(CoreWrapperTest, TransactionCommit) {
    auto tra = connection_->StartTransaction();
    ASSERT_TRUE(tra->isActive());
    
    tra->Commit();
    ASSERT_FALSE(tra->isActive());
}

TEST_F(CoreWrapperTest, TransactionRollback) {
    auto tra = connection_->StartTransaction();
    ASSERT_TRUE(tra->isActive());
    
    tra->Rollback();
    ASSERT_FALSE(tra->isActive());
}

TEST_F(CoreWrapperTest, TransactionAutoRollback) {
    {
        auto tra = connection_->StartTransaction();
        ASSERT_TRUE(tra->isActive());
        // Transaction goes out of scope without commit - should auto-rollback
    }
    
    // Create new transaction to verify
    auto tra2 = connection_->StartTransaction();
    ASSERT_TRUE(tra2->isActive());
    tra2->Commit();
}

TEST_F(CoreWrapperTest, MultipleOperations) {
    // First insert
    auto tra1 = connection_->Execute("INSERT INTO test_table (id, name, amount) VALUES (1, 'first', 1.0)");
    tra1->Commit();
    
    // Second insert
    auto tra2 = connection_->Execute("INSERT INTO test_table (id, name, amount) VALUES (2, 'second', 2.0)");
    tra2->Commit();
    
    // Update
    auto tra3 = connection_->Execute("UPDATE test_table SET amount = amount * 2 WHERE id = 1");
    tra3->Commit();
    
    // Delete
    auto tra4 = connection_->Execute("DELETE FROM test_table WHERE id = 2");
    tra4->Commit();
}

TEST_F(CoreWrapperTest, ErrorHandling) {
    // Invalid SQL should throw
    ASSERT_THROW({
        auto tra = connection_->Execute("INVALID SQL STATEMENT");
    }, FirebirdException);
    
    // Connection should still be valid
    ASSERT_TRUE(connection_->isConnected());
    
    // Should work after error
    ASSERT_NO_THROW({
        auto tra = connection_->Execute("DELETE FROM test_table WHERE id = 999");
        tra->Commit();
    });
}

TEST_F(CoreWrapperTest, IsConnectedCheck) {
    // Connection should be valid after creation
    ASSERT_TRUE(connection_->isConnected());
    
    // Create a second connection to test
    ConnectionParams params;
    params.database = db_params_.database;
    params.user = db_params_.user;
    params.password = db_params_.password;
    params.charset = db_params_.charset;
    
    {
        Connection conn2(params);
        ASSERT_TRUE(conn2.isConnected());
        
        // Execute something to ensure connection is really active
        auto tra = conn2.Execute("DELETE FROM test_table WHERE id = 9999");
        tra->Commit();
        
        // Should still be connected
        ASSERT_TRUE(conn2.isConnected());
        
        // Multiple ping checks should work
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(conn2.isConnected());
        }
    }
    // conn2 destroyed here, should have disconnected
    
    // Original connection should still be valid
    ASSERT_TRUE(connection_->isConnected());
    
    // Test multiple pings on main connection
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(connection_->isConnected());
    }
}
