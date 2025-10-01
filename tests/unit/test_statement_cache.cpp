#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp_util/config.h"
#include "fbpp_util/logging.h"
#include <thread>
#include <chrono>

using namespace fbpp::core;
using namespace fbpp::test;

// Test fixture for statement cache tests
class StatementCacheTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        // Call base class SetUp - logging is now initialized from config
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
        connection_->ExecuteDDL(R"(
            CREATE TABLE test_cache (
                id INTEGER NOT NULL PRIMARY KEY,
                name VARCHAR(100),
                value_num DOUBLE PRECISION
            )
        )");
    }
};

// Test basic cache operations
TEST_F(StatementCacheTest, BasicCacheOperations) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    // Test initial state
    EXPECT_TRUE(cache.isEnabled());
    EXPECT_EQ(cache.getMaxSize(), 10);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 0);
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.missCount, 0);
}

// Test cache hit and miss
TEST_F(StatementCacheTest, CacheHitAndMiss) {
    StatementCache::CacheConfig config;
    config.maxSize = 5;
    config.enabled = true;

    StatementCache cache(config);

    const std::string sql = "SELECT * FROM test_cache WHERE id = ?";

    // First call should be a miss
    auto stmt1 = cache.get(connection_.get(), sql, 0);
    ASSERT_NE(stmt1, nullptr);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.missCount, 1);
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.cacheSize, 1);

    // Second call should be a hit
    auto stmt2 = cache.get(connection_.get(), sql, 0);
    ASSERT_NE(stmt2, nullptr);

    stats = cache.getStatistics();
    EXPECT_EQ(stats.missCount, 1);
    EXPECT_EQ(stats.hitCount, 1);
    EXPECT_EQ(stats.cacheSize, 1);

    // Same statement pointer should be returned
    EXPECT_EQ(stmt1.get(), stmt2.get());
}

// Test LRU eviction
TEST_F(StatementCacheTest, LRUEviction) {
    StatementCache::CacheConfig config;
    config.maxSize = 3;
    config.enabled = true;

    StatementCache cache(config);

    // Fill cache with 3 statements
    auto stmt1 = cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 1", 0);
    auto stmt2 = cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 2", 0);
    auto stmt3 = cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 3", 0);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 3);
    EXPECT_EQ(stats.evictionCount, 0);

    // Add 4th statement - should evict the first (LRU)
    auto stmt4 = cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 4", 0);

    stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 3);
    EXPECT_EQ(stats.evictionCount, 1);

    // Accessing stmt1 SQL again should be a miss (was evicted)
    auto stmt1_new = cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 1", 0);

    stats = cache.getStatistics();
    EXPECT_EQ(stats.missCount, 5);  // 4 initial misses + 1 for re-fetching stmt1
    EXPECT_EQ(stats.evictionCount, 2);  // Another eviction occurred
}

// Test cache with different flags
TEST_F(StatementCacheTest, CacheWithDifferentFlags) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    const std::string sql = "SELECT * FROM test_cache";

    // Same SQL with different flags should create different cache entries
    auto stmt1 = cache.get(connection_.get(), sql, 0);
    auto stmt2 = cache.get(connection_.get(), sql, Statement::PREPARE_PREFETCH_METADATA);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 2);  // Two different entries
    EXPECT_EQ(stats.missCount, 2);
    EXPECT_EQ(stats.hitCount, 0);

    // Verify they are different statements
    EXPECT_NE(stmt1.get(), stmt2.get());
}

// Test cache clear
TEST_F(StatementCacheTest, CacheClear) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    // Add some statements
    cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 1", 0);
    cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 2", 0);
    cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 3", 0);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 3);

    // Clear cache
    cache.clear();

    stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 0);
}

// Test cache disable
TEST_F(StatementCacheTest, DISABLED_CacheDisable) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    const std::string sql = "SELECT * FROM test_cache WHERE id = 1";

    // Add a statement while enabled
    {
        auto stmt1 = cache.get(connection_.get(), sql, 0);
        ASSERT_NE(stmt1, nullptr);

        auto stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 1);
    }

    // Disable cache
    cache.setEnabled(false);
    EXPECT_FALSE(cache.isEnabled());

    // Cache should be cleared when disabled
    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 0);

    // Getting statement with disabled cache should not cache it
    {
        auto stmt2 = cache.get(connection_.get(), sql, 0);
        ASSERT_NE(stmt2, nullptr);

        stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 0);

        auto stmt3 = cache.get(connection_.get(), sql, 0);
        ASSERT_NE(stmt3, nullptr);

        // Different statement objects when cache is disabled
        EXPECT_NE(stmt2.get(), stmt3.get());
    }
}

// Test cache remove
TEST_F(StatementCacheTest, CacheRemove) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    const std::string sql1 = "SELECT * FROM test_cache WHERE id = 1";
    const std::string sql2 = "SELECT * FROM test_cache WHERE id = 2";

    // Add statements
    cache.get(connection_.get(), sql1, 0);
    cache.get(connection_.get(), sql2, 0);

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 2);

    // Remove one statement
    bool removed = cache.remove(sql1, 0);
    EXPECT_TRUE(removed);

    stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 1);

    // Try to remove non-existent statement
    removed = cache.remove(sql1, 0);
    EXPECT_FALSE(removed);
}

// Test TTL expiration
TEST_F(StatementCacheTest, TTLExpiration) {
    // TTL = 0 means no TTL, so test that
    {
        StatementCache::CacheConfig config;
        config.maxSize = 10;
        config.enabled = true;
        config.ttlMinutes = 0;  // No TTL

        StatementCache cache(config);

        // Add statements
        cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 1", 0);
        cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 2", 0);

        auto stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 2);

        // Sleep a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Remove expired entries - should remove nothing because ttl = 0
        size_t removed = cache.removeExpired();
        EXPECT_EQ(removed, 0);

        stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 2);
    }

    // Test with real TTL (but can't wait that long in unit test)
    // So we'll just verify the mechanism exists
    {
        StatementCache::CacheConfig config;
        config.maxSize = 10;
        config.enabled = true;
        config.ttlMinutes = 1;  // 1 minute TTL

        StatementCache cache(config);

        // Add statements
        cache.get(connection_.get(), "SELECT * FROM test_cache WHERE id = 3", 0);

        auto stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 1);

        // Immediately after adding, nothing should expire
        size_t removed = cache.removeExpired();
        EXPECT_EQ(removed, 0);

        stats = cache.getStatistics();
        EXPECT_EQ(stats.cacheSize, 1);
    }
}

// Test concurrent access (thread safety)
TEST_F(StatementCacheTest, ThreadSafety) {
    StatementCache::CacheConfig config;
    config.maxSize = 100;
    config.enabled = true;

    StatementCache cache(config);

    const int numThreads = 10;
    const int opsPerThread = 10;

    std::vector<std::thread> threads;

    // Launch threads
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, this, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                std::string sql = "SELECT * FROM test_cache WHERE id = " +
                                 std::to_string(t * opsPerThread + i);
                auto stmt = cache.get(connection_.get(), sql, 0);
                ASSERT_NE(stmt, nullptr);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify final state
    auto stats = cache.getStatistics();
    EXPECT_LE(stats.cacheSize, 100);  // Should not exceed max size
    EXPECT_EQ(stats.missCount, numThreads * opsPerThread);  // All should be misses
}

// Test cache size adjustment
TEST_F(StatementCacheTest, CacheSizeAdjustment) {
    StatementCache::CacheConfig config;
    config.maxSize = 5;
    config.enabled = true;

    StatementCache cache(config);

    // Fill cache with 5 statements
    for (int i = 0; i < 5; ++i) {
        std::string sql = "SELECT * FROM test_cache WHERE id = " + std::to_string(i);
        cache.get(connection_.get(), sql, 0);
    }

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 5);

    // Reduce max size
    cache.setMaxSize(3);

    // Cache should be reduced to 3 entries
    stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 3);

    // Increase max size
    cache.setMaxSize(10);

    // Add more statements
    for (int i = 5; i < 10; ++i) {
        std::string sql = "SELECT * FROM test_cache WHERE id = " + std::to_string(i);
        cache.get(connection_.get(), sql, 0);
    }

    stats = cache.getStatistics();
    EXPECT_EQ(stats.cacheSize, 8);  // 3 old + 5 new
}

// Test hit rate calculation
TEST_F(StatementCacheTest, HitRateCalculation) {
    StatementCache::CacheConfig config;
    config.maxSize = 10;
    config.enabled = true;

    StatementCache cache(config);

    const std::string sql = "SELECT * FROM test_cache WHERE id = ?";

    // Generate some hits and misses
    cache.get(connection_.get(), sql, 0);  // Miss
    cache.get(connection_.get(), sql, 0);  // Hit
    cache.get(connection_.get(), sql, 0);  // Hit
    cache.get(connection_.get(), sql, 0);  // Hit

    auto stats = cache.getStatistics();
    EXPECT_EQ(stats.missCount, 1);
    EXPECT_EQ(stats.hitCount, 3);
    EXPECT_DOUBLE_EQ(stats.hitRate, 75.0);  // 3 hits out of 4 total = 75%
}