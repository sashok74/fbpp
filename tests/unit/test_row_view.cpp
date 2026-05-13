#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/row.hpp"
#include "fbpp/core/exception.hpp"

#include <optional>
#include <string>

// RowView — non-owning runtime row access.

using namespace fbpp::core;
using namespace fbpp::test;

class RowViewTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE TABLE rv (
                id INTEGER NOT NULL PRIMARY KEY,
                name VARCHAR(64),
                amt DOUBLE PRECISION,
                fixed CHAR(8)
            )
        )");
        connection_->ExecuteDDL(R"(
            CREATE TABLE qident (
                "MyCol" INTEGER NOT NULL PRIMARY KEY
            )
        )");

        auto tx = connection_->StartTransaction();
        auto ins = connection_->prepareStatement(
            "INSERT INTO rv (id, name, amt, fixed) VALUES (?, ?, ?, ?)");
        tx->execute(ins, std::make_tuple(int32_t{1}, std::string("Alice"),
                                          3.14, std::string("CHARFLD ")));
        tx->execute(ins, std::make_tuple(int32_t{2}, std::optional<std::string>{},
                                          std::optional<double>{},
                                          std::optional<std::string>{}));
        tx->Commit();
    }
};

TEST_F(RowViewTest, GetByIndex) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amt FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    bool seen = false;
    for (const auto& v : cur->rows()) {
        seen = true;
        auto id = v.get<int32_t>(0);
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(*id, 1);
    }
    EXPECT_TRUE(seen);
}

TEST_F(RowViewTest, GetByNameCaseInsensitive) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id, name FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        EXPECT_EQ(v.get<int32_t>("ID").value_or(-1), 1);
        EXPECT_EQ(v.get<int32_t>("id").value_or(-1), 1);
        EXPECT_EQ(v.get<int32_t>("Id").value_or(-1), 1);
        EXPECT_EQ(v.get<int32_t>("  id  ").value_or(-1), 1);
        ASSERT_TRUE(v.get<std::string>("name").has_value());
        EXPECT_EQ(*v.get<std::string>("name"), "Alice");
    }
}

TEST_F(RowViewTest, GetByAlias) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT 0 AS flag, id FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        EXPECT_EQ(v.get<int32_t>("flag").value_or(-1), 0);
        EXPECT_EQ(v.get<int32_t>("id").value_or(-1), 1);
    }
}

TEST_F(RowViewTest, GetReturnsNulloptForSqlNull) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT name, amt FROM rv WHERE id = 2");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        EXPECT_FALSE(v.get<std::string>("name").has_value());
        EXPECT_FALSE(v.get<double>("amt").has_value());
        EXPECT_TRUE(v.isNull("name"));
        EXPECT_TRUE(v.isNull("amt"));
    }
}

TEST_F(RowViewTest, GetThrowsForUnknownColumn) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        EXPECT_THROW((void)v.get<int32_t>("nope"), FirebirdException);
        EXPECT_THROW((void)v.isNull("nope"), FirebirdException);
    }
}

TEST_F(RowViewTest, GetThrowsForTypeMismatch) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT name FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        // NAME is VARCHAR — asking for int should throw.
        EXPECT_THROW((void)v.get<int32_t>("name"), FirebirdException);
        // string is the universal fallback — must succeed.
        EXPECT_EQ(v.get<std::string>("name").value_or(""), "Alice");
    }
}

TEST_F(RowViewTest, ColumnEnumeration) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id, name AS who, 0 AS flag FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        ASSERT_EQ(v.columnCount(), 3u);
        EXPECT_EQ(v.columnName(0), "ID");
        EXPECT_EQ(v.columnName(1), "WHO");
        EXPECT_EQ(v.columnName(2), "FLAG");
    }
}

TEST_F(RowViewTest, GetDoubleAcceptsScaledNumeric) {
    // Regression: scaled NUMERIC is stored as SQL_SHORT/LONG/INT64 with
    // scale<0; codec applies the 10^-scale factor. get<double> must
    // accept it (the type-mismatch guard previously rejected it).
    connection_->ExecuteDDL(
        "CREATE TABLE rv_num ("
        "    id INTEGER NOT NULL PRIMARY KEY,"
        "    n_18_2 NUMERIC(18, 2),"
        "    n_9_4 NUMERIC(9, 4)"
        ")");
    auto txi = connection_->StartTransaction();
    auto ins = connection_->prepareStatement(
        "INSERT INTO rv_num (id, n_18_2, n_9_4) VALUES (?, ?, ?)");
    txi->execute(ins, std::make_tuple(int32_t{1}, 123.45, 0.5000));
    txi->Commit();

    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT n_18_2, n_9_4 FROM rv_num WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        auto a = v.get<double>("n_18_2");
        ASSERT_TRUE(a.has_value());
        EXPECT_NEAR(*a, 123.45, 1e-9);
        auto b = v.get<double>("n_9_4");
        ASSERT_TRUE(b.has_value());
        EXPECT_NEAR(*b, 0.5, 1e-9);
    }
}

TEST_F(RowViewTest, StringTrimmedForChar) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT fixed FROM rv WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    for (const auto& v : cur->rows()) {
        // CHAR(8) was inserted with literal "CHARFLD " (7 chars + trailing
        // space). Codec trims trailing spaces — exactly mirrors how
        // tuple/json paths read CHAR.
        ASSERT_TRUE(v.get<std::string>("fixed").has_value());
        EXPECT_EQ(*v.get<std::string>("fixed"), "CHARFLD");
    }
}

#ifndef NDEBUG
TEST_F(RowViewTest, GenerationGuardCatchesStaleViewInDebug) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement("SELECT id FROM rv ORDER BY id");
    auto cur = tx->openCursor(stmt);
    auto it = cur->rows().begin();
    ASSERT_NE(it, cur->rows().end());
    RowView stale = *it;
    ++it;  // bumps generation, stale becomes invalid
    EXPECT_THROW((void)stale.get<int32_t>(0), FirebirdException);
}
#endif
