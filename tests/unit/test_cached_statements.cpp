#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp_util/config.h"
#include "fbpp_util/logging.h"
#include <thread>
#include <chrono>

using namespace fbpp::core;
using namespace fbpp::test;

// Test fixture for cached statement tests
class CachedStatementsTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Call base class SetUp first
        TempDatabaseTest::SetUp();

        // Configure cache
        util::Config::cache().enabled = true;
        util::Config::cache().maxStatements = 10;
        util::Config::cache().ttlMinutes = 60;

        // Create test table specific to cache tests
        connection_->ExecuteDDL(R"(
            CREATE TABLE test_cached (
                id INTEGER NOT NULL PRIMARY KEY,
                name VARCHAR(100),
                value_num DOUBLE PRECISION,
                created_at TIMESTAMP
            )
        )");
    }
};

// Test basic cached statement retrieval
TEST_F(CachedStatementsTest, BasicCachedStatement) {
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    // First call should create and cache the statement
    auto stmt1 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt1, nullptr);

    // Second call should return the same cached statement
    auto stmt2 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt2, nullptr);

    // Verify they are the same statement object
    EXPECT_EQ(stmt1.get(), stmt2.get());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 1);
    EXPECT_EQ(stats.hitCount, 1);  // Second call was a hit
    EXPECT_EQ(stats.missCount, 1); // First call was a miss
}

// Test cache with different SQL queries
TEST_F(CachedStatementsTest, MultipleCachedStatements) {
    const std::string sql1 = "SELECT * FROM test_cached WHERE id = ?";
    const std::string sql2 = "SELECT * FROM test_cached WHERE name = ?";
    const std::string sql3 = "INSERT INTO test_cached (id, name) VALUES (?, ?)";

    // Cache multiple statements
    auto stmt1 = connection_->prepareStatement(sql1);
    auto stmt2 = connection_->prepareStatement(sql2);
    auto stmt3 = connection_->prepareStatement(sql3);

    ASSERT_NE(stmt1, nullptr);
    ASSERT_NE(stmt2, nullptr);
    ASSERT_NE(stmt3, nullptr);

    // Verify they are different statements
    EXPECT_NE(stmt1.get(), stmt2.get());
    EXPECT_NE(stmt2.get(), stmt3.get());
    EXPECT_NE(stmt1.get(), stmt3.get());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 3);
    EXPECT_EQ(stats.missCount, 3);
}

// Test cache clear functionality
TEST_F(CachedStatementsTest, ClearCache) {
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    // Cache a statement
    auto stmt1 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt1, nullptr);

    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 1);

    // Clear the cache
    connection_->clearStatementCache();

    // Check cache is empty
    stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 0);

    // Getting the same query again should be a miss
    auto stmt2 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt2, nullptr);

    stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 1);
    EXPECT_EQ(stats.missCount, 2);  // Two misses total
}

// Test cached statement validity
TEST_F(CachedStatementsTest, CachedStatementValidity) {
    const std::string sql1 = "SELECT name, value_num FROM test_cached WHERE id = 1";
    const std::string sql2 = "SELECT name, value_num FROM test_cached WHERE id = 2";

    // Get cached statements
    auto stmt1 = connection_->prepareStatement(sql1);
    ASSERT_NE(stmt1, nullptr);
    ASSERT_TRUE(stmt1->isValid());

    auto stmt2 = connection_->prepareStatement(sql2);
    ASSERT_NE(stmt2, nullptr);
    ASSERT_TRUE(stmt2->isValid());

    // Get the same statement again - should be cached
    auto stmt1_again = connection_->prepareStatement(sql1);
    EXPECT_EQ(stmt1.get(), stmt1_again.get());  // Should be the same statement
    ASSERT_TRUE(stmt1_again->isValid());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 2);  // Two different queries
    EXPECT_EQ(stats.hitCount, 1);   // Second prepareStatement(sql1) was a hit

    // Verify statement metadata is preserved
    auto outputMeta1 = stmt1->getOutputMetadata();
    ASSERT_NE(outputMeta1, nullptr);
    EXPECT_EQ(outputMeta1->getCount(), 2);  // Two columns: name, value_num

    auto outputMeta2 = stmt1_again->getOutputMetadata();
    ASSERT_NE(outputMeta2, nullptr);
    EXPECT_EQ(outputMeta2->getCount(), 2);  // Should have same metadata
}

// Test cache with different flags
TEST_F(CachedStatementsTest, CacheWithDifferentFlags) {
    const std::string sql = "SELECT * FROM test_cached";

    // Same SQL with different flags should create different cache entries
    auto stmt1 = connection_->prepareStatement(sql, 0);
    auto stmt2 = connection_->prepareStatement(sql, Statement::PREPARE_PREFETCH_METADATA);

    ASSERT_NE(stmt1, nullptr);
    ASSERT_NE(stmt2, nullptr);

    // Should be different statements
    EXPECT_NE(stmt1.get(), stmt2.get());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 2);  // Two different entries
    EXPECT_EQ(stats.missCount, 2);
}

// Test SQL normalization (case insensitive, whitespace)
TEST_F(CachedStatementsTest, SQLNormalization) {
    // These should all map to the same cached statement
    const std::string sql1 = "SELECT * FROM test_cached WHERE id = ?";
    const std::string sql2 = "select * from test_cached where id = ?";
    const std::string sql3 = "SELECT  *  FROM  test_cached  WHERE  id = ?";
    const std::string sql4 = "SELECT * FROM test_cached WHERE id = ? -- comment";
    const std::string sql5 = R"(SELECT *
                              FROM test_cached
                              WHERE id = ?)";

    auto stmt1 = connection_->prepareStatement(sql1);
    auto stmt2 = connection_->prepareStatement(sql2);
    auto stmt3 = connection_->prepareStatement(sql3);
    auto stmt4 = connection_->prepareStatement(sql4);
    auto stmt5 = connection_->prepareStatement(sql5);

    ASSERT_NE(stmt1, nullptr);

    // All should be the same cached statement
    EXPECT_EQ(stmt1.get(), stmt2.get());
    EXPECT_EQ(stmt1.get(), stmt3.get());
    EXPECT_EQ(stmt1.get(), stmt4.get());
    EXPECT_EQ(stmt1.get(), stmt5.get());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 1);   // Only one entry
    EXPECT_EQ(stats.missCount, 1);   // Only first was a miss
    EXPECT_EQ(stats.hitCount, 4);    // Four hits
}

// Test with string literals preserved
TEST_F(CachedStatementsTest, SQLNormalizationWithStrings) {
    // String literals should be preserved exactly
    const std::string sql1 = "SELECT * FROM test_cached WHERE name = 'Test Name'";
    const std::string sql2 = "SELECT * FROM test_cached WHERE name = 'test name'";  // Different case in string
    const std::string sql3 = "select * from test_cached where name = 'Test Name'";  // Same string, different SQL case

    auto stmt1 = connection_->prepareStatement(sql1);
    auto stmt2 = connection_->prepareStatement(sql2);
    auto stmt3 = connection_->prepareStatement(sql3);

    ASSERT_NE(stmt1, nullptr);
    ASSERT_NE(stmt2, nullptr);
    ASSERT_NE(stmt3, nullptr);

    // Different string literals = different statements
    EXPECT_NE(stmt1.get(), stmt2.get());

    // Same string literal, different SQL case = same statement
    EXPECT_EQ(stmt1.get(), stmt3.get());

    // Check cache statistics
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 2);   // Two different entries
}

// Test concurrent access to cache
TEST_F(CachedStatementsTest, ConcurrentCacheAccess) {
    const int numThreads = 10;
    const int opsPerThread = 5;
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                // Mix of different queries
                std::string sql;
                if (i % 3 == 0) {
                    sql = "SELECT * FROM test_cached WHERE id = ?";
                } else if (i % 3 == 1) {
                    sql = "SELECT * FROM test_cached WHERE name = ?";
                } else {
                    sql = "INSERT INTO test_cached (id, name) VALUES (?, ?)";
                }

                auto stmt = connection_->prepareStatement(sql);
                ASSERT_NE(stmt, nullptr);

                // Small delay to increase chance of contention
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Check final state
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 3);  // Three unique queries

    // Total accesses should be numThreads * opsPerThread
    size_t totalAccesses = stats.hitCount + stats.missCount;
    EXPECT_EQ(totalAccesses, numThreads * opsPerThread);
}

// Test cache hit rate
TEST_F(CachedStatementsTest, CacheHitRate) {
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    // First access - miss
    connection_->prepareStatement(sql);

    // Multiple subsequent accesses - hits
    for (int i = 0; i < 9; ++i) {
        connection_->prepareStatement(sql);
    }

    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.missCount, 1);
    EXPECT_EQ(stats.hitCount, 9);
    EXPECT_DOUBLE_EQ(stats.hitRate, 90.0);  // 9 hits out of 10 total = 90%
}