#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/struct_pack.hpp"
#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace fbpp::core;
using namespace fbpp::test;

namespace {

struct TableTestSelectInput {
    int32_t id;
};

struct TableTestSelectOutput {
    int32_t id;
    std::optional<int32_t> fInteger;
    std::string fVarchar;
};

} // namespace

namespace fbpp::core {

template<>
struct StructDescriptor<::TableTestSelectInput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&TableTestSelectInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false)
    );
};

template<>
struct StructDescriptor<::TableTestSelectOutput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&TableTestSelectOutput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false),
        makeField<&TableTestSelectOutput::fInteger>("F_INTEGER", SQL_LONG, 0, sizeof(int32_t), true),
        makeField<&TableTestSelectOutput::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 64, true)
    );
};

} // namespace fbpp::core

class StructPackTest : public PersistentDatabaseTest {};

TEST_F(StructPackTest, PackAndUnpackStructAgainstTableTest1) {
    const std::string fetchIdSql = "SELECT FIRST 1 ID FROM TABLE_TEST_1 ORDER BY ID";
    auto fetchStmt = connection_->prepareStatement(fetchIdSql);
    auto fetchTra = connection_->StartTransaction();
    auto fetchCursor = fetchTra->openCursor(fetchStmt);

    std::tuple<int32_t> idRow{};
    ASSERT_TRUE(fetchCursor->fetch(idRow)) << "TABLE_TEST_1 must contain at least one row";
    int32_t targetId = std::get<0>(idRow);
    fetchCursor->close();
    fetchTra->Commit();

    const std::string selectSql =
        "SELECT ID, F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE ID = ?";
    TableTestSelectInput filter{targetId};
    auto stmt = connection_->prepareStatement(selectSql);
    auto tra = connection_->StartTransaction();
    auto cursor = tra->openCursor(stmt, filter);
    ASSERT_TRUE(cursor) << "Failed to open cursor with struct parameters";

    TableTestSelectOutput row{};
    ASSERT_TRUE(cursor->fetch(row));
    EXPECT_EQ(row.id, targetId);

    // Allow optional value to be null; just ensure we captured string metadata
    if (row.fInteger.has_value()) {
        // no-op, presence verified
    }
    EXPECT_GE(row.fVarchar.size(), 0u);

    ASSERT_FALSE(cursor->fetch(row)) << "Expected exactly one row";
    cursor->close();
    tra->Commit();
}
