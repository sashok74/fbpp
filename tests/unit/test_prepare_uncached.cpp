#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/exception.hpp"

#include <string>

// Connection::prepareStatementUncached + describeQuery refactor.
// The motivation is CI manifest tooling that scans 1000+ procedures —
// the cache would otherwise be flushed by transient lookups.

using namespace fbpp::core;
using namespace fbpp::test;

class PrepareUncachedTest : public TempDatabaseTest {};

TEST_F(PrepareUncachedTest, PrepareUncachedReturnsValidStatement) {
    auto stmt = connection_->prepareStatementUncached("SELECT 1 FROM RDB$DATABASE");
    ASSERT_NE(stmt, nullptr);
    EXPECT_TRUE(stmt->isValid());
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Select);
    EXPECT_TRUE(stmt->hasOutput());
}

TEST_F(PrepareUncachedTest, PrepareUncachedDoesNotPopulateCache) {
    auto sizeBefore = connection_->getCacheStatistics().cacheSize;
    for (int i = 0; i < 50; ++i) {
        auto s = connection_->prepareStatementUncached(
            "SELECT " + std::to_string(i) + " FROM RDB$DATABASE");
        EXPECT_TRUE(s->isValid());
    }
    EXPECT_EQ(connection_->getCacheStatistics().cacheSize, sizeBefore);
}

TEST_F(PrepareUncachedTest, PrepareUncachedCoexistsWithCached) {
    const std::string sql = "SELECT 42 FROM RDB$DATABASE";
    auto cached1 = connection_->prepareStatement(sql);
    auto sizeAfterFirstCached = connection_->getCacheStatistics().cacheSize;
    EXPECT_GE(sizeAfterFirstCached, 1u);

    // Uncached call with the SAME SQL must not bump cache size.
    auto raw = connection_->prepareStatementUncached(sql);
    EXPECT_EQ(connection_->getCacheStatistics().cacheSize, sizeAfterFirstCached);

    // Cached prepare again — must hit the existing entry (checkout
    // semantics: cached1 is still held, so cached2 is a distinct exclusive
    // instance; the hit is visible in the statistics, not via pointers).
    auto hitsBefore = connection_->getCacheStatistics().hitCount;
    auto cached2 = connection_->prepareStatement(sql);
    EXPECT_TRUE(cached2->isValid());
    EXPECT_EQ(connection_->getCacheStatistics().hitCount, hitsBefore + 1);
    EXPECT_EQ(connection_->getCacheStatistics().cacheSize, sizeAfterFirstCached);
}

TEST_F(PrepareUncachedTest, DescribeQueryDoesNotPopulateCache) {
    auto sizeBefore = connection_->getCacheStatistics().cacheSize;
    for (int i = 0; i < 50; ++i) {
        auto info = connection_->describeQuery(
            "SELECT " + std::to_string(i) + " AS x FROM RDB$DATABASE");
        EXPECT_FALSE(info.outputFields.empty());
    }
    EXPECT_EQ(connection_->getCacheStatistics().cacheSize, sizeBefore);
}

TEST_F(PrepareUncachedTest, DescribeQueryReturnsInputAndOutputFields) {
    auto info = connection_->describeQuery(
        "SELECT id, name FROM test_table WHERE id = ?");
    ASSERT_EQ(info.inputFields.size(), 1u);
    ASSERT_EQ(info.outputFields.size(), 2u);
    EXPECT_EQ(info.outputFields[0].name, "ID");
    EXPECT_EQ(info.outputFields[1].name, "NAME");
}

TEST_F(PrepareUncachedTest, PrepareUncachedNamedParamsRecognized) {
    auto stmt = connection_->prepareStatementUncached(
        "SELECT id FROM test_table WHERE id = :the_id");
    ASSERT_TRUE(stmt->hasNamedParameters());
    const auto& mapping = stmt->getNamedParamMapping();
    EXPECT_EQ(mapping.count("the_id"), 1u);
}
