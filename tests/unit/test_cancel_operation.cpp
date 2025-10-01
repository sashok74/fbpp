#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"

using namespace fbpp::core;
using namespace fbpp::test;

class CancelOperationTest : public TempDatabaseTest {
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

    // No additional schema needed for basic tests
};

// Test basic enable/disable operations
TEST_F(CancelOperationTest, EnableDisableCancelOperations) {
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());

    // Test disabling cancel operations
    ASSERT_NO_THROW({
        connection_->cancelOperation(CancelOperation::DISABLE);
    });

    // Test enabling cancel operations
    ASSERT_NO_THROW({
        connection_->cancelOperation(CancelOperation::ENABLE);
    });

    // Multiple enable/disable should work
    ASSERT_NO_THROW({
        connection_->cancelOperation(CancelOperation::DISABLE);
        connection_->cancelOperation(CancelOperation::DISABLE);
        connection_->cancelOperation(CancelOperation::ENABLE);
        connection_->cancelOperation(CancelOperation::ENABLE);
    });
}

// Test cancel raise operation - basic version
TEST_F(CancelOperationTest, CancelRaiseOperation) {
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());

    // When there's nothing to cancel, RAISE will throw an error
    // This is expected behavior from Firebird
    try {
        connection_->cancelOperation(CancelOperation::RAISE);
        // If no exception, that's also acceptable
    } catch (const FirebirdException& e) {
        // Expected - "nothing to cancel"
        auto logger = util::Logging::get();
        if (logger) {
            logger->info("Expected exception when nothing to cancel: {}", e.what());
        }
    }

    // Connection should still be valid
    ASSERT_TRUE(connection_->isConnected());
}

// Test abort operation
TEST_F(CancelOperationTest, CancelAbortOperation) {
    ASSERT_NE(connection_, nullptr);
    ASSERT_TRUE(connection_->isConnected());

    // Abort is more forceful than raise
    ASSERT_NO_THROW({
        connection_->cancelOperation(CancelOperation::ABORT);
    });

    // Connection might be broken after abort, check status
    // Note: The behavior after ABORT may vary depending on what was happening
    // at the time. The connection might still be valid or might be broken.
    auto logger = util::Logging::get();
    if (logger) {
        bool still_connected = connection_->isConnected();
        logger->info("Connection status after ABORT: {}",
                    still_connected ? "connected" : "disconnected");
    }
}

// Test cancel operation on disconnected connection
TEST_F(CancelOperationTest, CancelOperationOnDisconnected) {
    // Test by calling ABORT which disconnects the connection
    ASSERT_TRUE(connection_->isConnected());

    // ABORT should disconnect the connection
    ASSERT_NO_THROW({
        connection_->cancelOperation(CancelOperation::ABORT);
    });

    // Connection should now be disconnected
    ASSERT_FALSE(connection_->isConnected());

    // Try to cancel operation on disconnected connection - should throw
    ASSERT_THROW({
        connection_->cancelOperation(CancelOperation::RAISE);
    }, FirebirdException);
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}