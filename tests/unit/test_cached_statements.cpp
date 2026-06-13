#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/result_set.hpp"
#include <atomic>
#include <thread>
#include <chrono>

using namespace fbpp::core;
using namespace fbpp::test;

// Test fixture for cached statement tests
class CachedStatementsTest : public TempDatabaseTest {
protected:
    void SetUp() override {
        TempDatabaseTest::SetUp();

        StatementCacheConfig cacheConfig;
        cacheConfig.enabled = true;
        cacheConfig.maxSize = 10;
        cacheConfig.ttlMinutes = 60;
        connection_->setStatementCacheConfig(cacheConfig);
    }

    void createTestSchema() override {
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

// Test basic cached statement retrieval (checkout semantics)
TEST_F(CachedStatementsTest, BasicCachedStatement) {
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    // First call should create and cache the statement
    auto stmt1 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt1, nullptr);

    // Second call while stmt1 is still held must give an EXCLUSIVE
    // instance: shared use of one IStatement leaks cursors/timeouts
    // between unrelated call sites.
    auto stmt2 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt2, nullptr);
    EXPECT_NE(stmt1.get(), stmt2.get());

    // Check cache statistics (hit = key found, even if an extra
    // instance had to be prepared because stmt1 was checked out)
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 1);
    EXPECT_EQ(stats.hitCount, 1);  // Second call was a (key) hit
    EXPECT_EQ(stats.missCount, 1); // First call was a miss

    // After releasing, the instance returns to the pool and is reused.
    Statement* raw1 = stmt1.get();
    Statement* raw2 = stmt2.get();
    stmt1.reset();
    stmt2.reset();
    auto stmt3 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt3, nullptr);
    EXPECT_TRUE(stmt3.get() == raw1 || stmt3.get() == raw2)
        << "Released instance was not reused from the pool";
}

TEST_F(CachedStatementsTest, ConnectionParamsCanDisableCache) {
    ConnectionParams params = db_params_;
    params.options.statementCache.enabled = false;
    params.options.statementCache.maxSize = 10;
    params.options.statementCache.ttlMinutes = 60;

    Connection directConnection(params);
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    auto stmt1 = directConnection.prepareStatement(sql);
    auto stmt2 = directConnection.prepareStatement(sql);

    ASSERT_NE(stmt1, nullptr);
    ASSERT_NE(stmt2, nullptr);
    EXPECT_NE(stmt1.get(), stmt2.get());

    auto stats = directConnection.getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 0);
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.missCount, 0);
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

    // Get the same SQL again while stmt1 is held — a distinct, valid
    // instance (checkout semantics), still counted as a cache hit.
    auto stmt1_again = connection_->prepareStatement(sql1);
    EXPECT_NE(stmt1.get(), stmt1_again.get());
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

    // Normalization is proven by the statistics: all five spellings map
    // to ONE cache entry, the first is the only miss. (The pointers are
    // distinct while held — checkout semantics.)
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

    // Same string literal, different SQL case = same cache entry:
    // cacheSize stays 2 and the third prepare counts as a hit.
    auto stats = connection_->getCacheStatistics();
    EXPECT_EQ(stats.cacheSize, 2);   // Two different entries
    EXPECT_EQ(stats.hitCount, 1);    // sql3 hit sql1's entry
}

// Два открытых курсора по ОДНОМУ SQL одновременно: до checkout-семантики
// оба вызова получали один IStatement, и второй openCursor падал с
// "attempt to reopen an open cursor".
TEST_F(CachedStatementsTest, NestedCursorsOnSameSqlDoNotCollide) {
    {
        auto tx0 = connection_->StartTransaction();
        auto ins = connection_->prepareStatement(
            "INSERT INTO test_cached (id, name) VALUES (?, ?)");
        tx0->execute(ins, std::make_tuple(1, std::string("a")));
        tx0->execute(ins, std::make_tuple(2, std::string("b")));
        tx0->Commit();
    }

    const std::string sql = "SELECT id FROM test_cached ORDER BY id";
    auto tx = connection_->StartTransaction();

    auto stmtA = connection_->prepareStatement(sql);
    auto rsA = tx->openCursor(stmtA);
    std::tuple<int> row;
    ASSERT_TRUE(rsA->fetch(row));
    EXPECT_EQ(std::get<0>(row), 1);

    // Пока rsA открыт — тот же SQL ещё раз
    auto stmtB = connection_->prepareStatement(sql);
    EXPECT_NE(stmtA.get(), stmtB.get());
    auto rsB = tx->openCursor(stmtB);
    ASSERT_TRUE(rsB->fetch(row));
    EXPECT_EQ(std::get<0>(row), 1);

    // rsA жив и сохранил позицию
    ASSERT_TRUE(rsA->fetch(row));
    EXPECT_EQ(std::get<0>(row), 2);

    rsA->close();
    rsB->close();
    tx->Commit();
}

// free() на выданном инстансе не должен отравлять пул: следующий
// prepareStatement обязан вернуть валидный стейтмент.
TEST_F(CachedStatementsTest, FreedInstanceDoesNotPoisonPool) {
    const std::string sql = "SELECT * FROM test_cached WHERE id = ?";

    auto stmt1 = connection_->prepareStatement(sql);
    ASSERT_TRUE(stmt1->isValid());
    stmt1->free();
    EXPECT_FALSE(stmt1->isValid());
    stmt1.reset();  // инвалидный инстанс должен быть отброшен, не запулен

    auto stmt2 = connection_->prepareStatement(sql);
    ASSERT_NE(stmt2, nullptr);
    EXPECT_TRUE(stmt2->isValid());
}

// Test supported parallel usage: one connection (and one cache) per thread.
TEST_F(CachedStatementsTest, ParallelConnectionsCanUseCachesIndependently) {
    const int numThreads = 10;
    const int opsPerThread = 5;
    std::vector<std::thread> threads;
    std::atomic<size_t> totalHits{0};
    std::atomic<size_t> totalMisses{0};
    std::atomic<size_t> totalCacheSize{0};
    std::atomic<size_t> nullStatements{0};

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, opsPerThread, &totalHits, &totalMisses, &totalCacheSize, &nullStatements]() {
            ConnectionParams params = db_params_;
            params.options.statementCache.enabled = true;
            params.options.statementCache.maxSize = 10;
            params.options.statementCache.ttlMinutes = 60;

            Connection localConnection(params);

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

                auto stmt = localConnection.prepareStatement(sql);
                if (!stmt) {
                    nullStatements.fetch_add(1, std::memory_order_relaxed);
                }

                // Small delay to increase chance of contention
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            const auto stats = localConnection.getCacheStatistics();
            totalHits.fetch_add(stats.hitCount, std::memory_order_relaxed);
            totalMisses.fetch_add(stats.missCount, std::memory_order_relaxed);
            totalCacheSize.fetch_add(stats.cacheSize, std::memory_order_relaxed);
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(nullStatements.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(totalCacheSize.load(std::memory_order_relaxed), static_cast<size_t>(numThreads * 3));
    EXPECT_EQ(totalMisses.load(std::memory_order_relaxed), static_cast<size_t>(numThreads * 3));
    EXPECT_EQ(totalHits.load(std::memory_order_relaxed),
              static_cast<size_t>(numThreads * (opsPerThread - 3)));
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
