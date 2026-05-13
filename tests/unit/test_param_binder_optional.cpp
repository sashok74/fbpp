#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/param_binder.hpp"

#include <optional>
#include <string>
#include <tuple>

// Verify that ParamBinder::set already accepts std::optional<T> via codec's
// optional overload. The explicit set(name, std::nullopt) overload is also
// covered so that callers can spell bare NULL without naming the inner type.

using namespace fbpp::core;
using namespace fbpp::test;

class ParamBinderOptionalTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE TABLE pb_opt (
                id INTEGER NOT NULL PRIMARY KEY,
                f_int INTEGER,
                f_text VARCHAR(64)
            )
        )");
    }
};

TEST_F(ParamBinderOptionalTest, SetOptionalEngagedWritesValue) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_int) VALUES (:id, :v)");
    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{1});
    EXPECT_TRUE(b.set("v", std::optional<int32_t>{42}));
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement("SELECT f_int FROM pb_opt WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{1}));
    std::tuple<std::optional<int32_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    ASSERT_TRUE(std::get<0>(row).has_value());
    EXPECT_EQ(*std::get<0>(row), 42);
}

TEST_F(ParamBinderOptionalTest, SetOptionalDisengagedWritesNull) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_int) VALUES (:id, :v)");
    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{2});
    EXPECT_TRUE(b.set("v", std::optional<int32_t>{}));
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement("SELECT f_int FROM pb_opt WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{2}));
    std::tuple<std::optional<int32_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_FALSE(std::get<0>(row).has_value());
}

TEST_F(ParamBinderOptionalTest, SetNulloptDeducesType) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_int) VALUES (:id, :v)");
    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{3});
    // Bare std::nullopt — handled by the explicit set(name, std::nullopt_t)
    // overload (the template path would deduce T=std::nullopt_t and silently
    // write junk via the generic memcpy branch).
    EXPECT_TRUE(b.set("v", std::nullopt));
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement("SELECT f_int FROM pb_opt WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{3}));
    std::tuple<std::optional<int32_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_FALSE(std::get<0>(row).has_value());
}

TEST_F(ParamBinderOptionalTest, SetOptionalString) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_text) VALUES (:id, :t)");
    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{4});
    EXPECT_TRUE(b.set("t", std::optional<std::string>{"hello"}));
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement("SELECT f_text FROM pb_opt WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{4}));
    std::tuple<std::optional<std::string>> row;
    ASSERT_TRUE(cur->fetch(row));
    ASSERT_TRUE(std::get<0>(row).has_value());
    EXPECT_EQ(*std::get<0>(row), "hello");
}

TEST_F(ParamBinderOptionalTest, SetOptionalThenSetSameNameOverwrites) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_int) VALUES (:id, :v)");
    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{5});
    b.set("v", std::optional<int32_t>{100});
    b.set("v", std::optional<int32_t>{}); // overwrite to NULL
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement("SELECT f_int FROM pb_opt WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{5}));
    std::tuple<std::optional<int32_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_FALSE(std::get<0>(row).has_value());
}

TEST_F(ParamBinderOptionalTest, RoundTripOptionalInt64ViaInsertSelect) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_opt (id, f_int) VALUES (:id, :v)");

    ParamBinder b(stmt, tx.get());
    // engaged
    b.clear();
    b.set("id", int32_t{10});
    b.set("v", std::optional<int32_t>{777});
    EXPECT_EQ(tx->execute(stmt, b), 1u);
    // disengaged
    b.clear();
    b.set("id", int32_t{11});
    b.set("v", std::optional<int32_t>{});
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT id, f_int FROM pb_opt WHERE id IN (10, 11) ORDER BY id");
    auto cur = tx->openCursor(sel);
    std::tuple<int32_t, std::optional<int32_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), 10);
    ASSERT_TRUE(std::get<1>(row).has_value());
    EXPECT_EQ(*std::get<1>(row), 777);
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), 11);
    EXPECT_FALSE(std::get<1>(row).has_value());
    EXPECT_FALSE(cur->fetch(row));
}
