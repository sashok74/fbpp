#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/row.hpp"
#include "fbpp/core/exception.hpp"

#include <string>

// ResultSet::rows() iterator + fetchOne() + generation guard.

using namespace fbpp::core;
using namespace fbpp::test;

class ResultSetRowsTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE TABLE rs_t (
                id INTEGER NOT NULL PRIMARY KEY,
                v INTEGER
            )
        )");
        auto tx = connection_->StartTransaction();
        auto ins = connection_->prepareStatement(
            "INSERT INTO rs_t (id, v) VALUES (?, ?)");
        for (int i = 1; i <= 5; ++i) {
            tx->execute(ins, std::make_tuple(int32_t{i}, int32_t{i * 10}));
        }
        tx->Commit();
    }
};

TEST_F(ResultSetRowsTest, RowsRangeIteratesAllRows) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id, v FROM rs_t ORDER BY id");
    auto cur = tx->openCursor(stmt);
    int count = 0;
    int sum = 0;
    for (const auto& v : cur->rows()) {
        ++count;
        sum += v.get<int32_t>("v").value_or(0);
    }
    EXPECT_EQ(count, 5);
    EXPECT_EQ(sum, 10 + 20 + 30 + 40 + 50);
}

TEST_F(ResultSetRowsTest, RowsRangeBreaksMidLoop) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rs_t ORDER BY id");
    auto cur = tx->openCursor(stmt);
    int count = 0;
    for (const auto& v : cur->rows()) {
        (void)v;
        ++count;
        if (count == 3) break;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ResultSetRowsTest, FetchOneReturnsFirstRow) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id, v FROM rs_t ORDER BY id");
    auto cur = tx->openCursor(stmt);
    auto first = cur->fetchOne();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->get<int32_t>("id").value_or(-1), 1);
    EXPECT_EQ(first->get<int32_t>("v").value_or(-1), 10);
    // Subsequent fetchOne returns next.
    auto second = cur->fetchOne();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->get<int32_t>("id").value_or(-1), 2);
}

TEST_F(ResultSetRowsTest, FetchOneEmptyResultSet) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id FROM rs_t WHERE id > 100");
    auto cur = tx->openCursor(stmt);
    EXPECT_FALSE(cur->fetchOne().has_value());
}

TEST_F(ResultSetRowsTest, GenerationStartsAtZeroAndAdvances) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rs_t ORDER BY id");
    auto cur = tx->openCursor(stmt);
    EXPECT_EQ(cur->generation(), 0u);
    auto first = cur->fetchOne();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(cur->generation(), 1u);
    auto second = cur->fetchOne();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(cur->generation(), 2u);
}

TEST_F(ResultSetRowsTest, GenerationAdvancesOnClose) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rs_t ORDER BY id");
    auto cur = tx->openCursor(stmt);
    auto first = cur->fetchOne();
    ASSERT_TRUE(first.has_value());
    auto genBefore = cur->generation();
    cur->close();
    EXPECT_GT(cur->generation(), genBefore);
}

TEST_F(ResultSetRowsTest, FetchOneAfterCloseThrows) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rs_t");
    auto cur = tx->openCursor(stmt);
    cur->close();
    EXPECT_THROW((void)cur->fetchOne(), FirebirdException);
}
