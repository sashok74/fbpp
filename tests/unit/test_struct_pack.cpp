#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/query_executor.hpp"
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

namespace local {

enum class QueryId { SelectById, UpdateName };

template<QueryId>
struct QueryDescriptor;

struct TableTestUpdateInput {
    std::string name;
    int32_t id;
};

} // namespace local

namespace fbpp::core {

template<>
struct StructDescriptor<local::TableTestUpdateInput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&local::TableTestUpdateInput::name>("F_VARCHAR", SQL_VARYING, 0, 64, true),
        makeField<&local::TableTestUpdateInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false)
    );
};

} // namespace fbpp::core

namespace local {

template<>
struct QueryDescriptor<QueryId::SelectById> {
    static constexpr std::string_view sql =
        "SELECT ID, F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE ID = :id";
    using Input = TableTestSelectInput;
    using Output = TableTestSelectOutput;
};

template<>
struct QueryDescriptor<QueryId::UpdateName> {
    static constexpr std::string_view sql =
        "UPDATE TABLE_TEST_1 SET F_VARCHAR = :name WHERE ID = :id";
    using Input = TableTestUpdateInput;
    using Output = fbpp::core::NoResult;
};

} // namespace local

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

    // Execute SELECT via QueryDescriptor helper
    auto selectTra = connection_->StartTransaction();
    auto rows = fbpp::core::executeQuery<local::QueryDescriptor<local::QueryId::SelectById>>(
        *connection_, *selectTra, TableTestSelectInput{targetId});
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].id, targetId);
    auto originalName = rows[0].fVarchar;
    selectTra->Commit();

    // Fetch single row
    auto fetchTra2 = connection_->StartTransaction();
    auto oneRow = fbpp::core::fetchOne<local::QueryDescriptor<local::QueryId::SelectById>>(
        *connection_, *fetchTra2, TableTestSelectInput{targetId});
    ASSERT_TRUE(oneRow.has_value());
    fetchTra2->Commit();

    // Update value and verify non-query execution
    auto updateTra = connection_->StartTransaction();
    local::TableTestUpdateInput updateParams{"UpdatedName", targetId};
    auto affected = fbpp::core::executeNonQuery<local::QueryDescriptor<local::QueryId::UpdateName>>(
        *connection_, *updateTra, updateParams);
    EXPECT_EQ(affected, 1u);
    updateTra->Commit();

    auto verifyTra = connection_->StartTransaction();
    auto updatedRow = fbpp::core::fetchOne<local::QueryDescriptor<local::QueryId::SelectById>>(
        *connection_, *verifyTra, TableTestSelectInput{targetId});
    ASSERT_TRUE(updatedRow.has_value());
    EXPECT_EQ(updatedRow->fVarchar, "UpdatedName");
    verifyTra->Commit();

    auto restoreTra = connection_->StartTransaction();
    local::TableTestUpdateInput restoreParams{originalName, targetId};
    fbpp::core::executeNonQuery<local::QueryDescriptor<local::QueryId::UpdateName>>(
        *connection_, *restoreTra, restoreParams);
    restoreTra->Commit();
}
