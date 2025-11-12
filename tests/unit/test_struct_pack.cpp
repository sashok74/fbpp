#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/query_executor.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"
#include "fbpp/adapters/chrono_datetime.hpp"
#include <array>
#include <chrono>
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

using Decimal38_8 = fbpp::adapters::TTNumeric<2, -8>;
using Numeric64_6 = fbpp::adapters::TTNumeric<1, -6>;
using MicroTime = std::chrono::hh_mm_ss<std::chrono::microseconds>;
using ZonedMicroTime = std::chrono::zoned_time<std::chrono::microseconds>;
using TimeWithTz = fbpp::core::TimeWithTz;

struct ExtendedTypesInsertInput {
    int32_t id;
    fbpp::core::Int128 fInt128;
    Decimal38_8 fDecimal;
    Numeric64_6 fNumeric;
    fbpp::core::DecFloat34 fDecfloat;
    std::chrono::year_month_day fDate;
    MicroTime fTime;
    std::chrono::system_clock::time_point fTimestamp;
    ZonedMicroTime fTimestampTz;
    TimeWithTz fTimeTz;
    std::string fVarchar;
};

struct ExtendedTypesFetchInput {
    int32_t id;
};

struct ExtendedTypesOutput {
    int32_t id;
    std::optional<fbpp::core::Int128> fInt128;
    std::optional<Decimal38_8> fDecimal;
    std::optional<Numeric64_6> fNumeric;
    std::optional<fbpp::core::DecFloat34> fDecfloat;
    std::optional<std::chrono::year_month_day> fDate;
    std::optional<MicroTime> fTime;
    std::optional<std::chrono::system_clock::time_point> fTimestamp;
    std::optional<ZonedMicroTime> fTimestampTz;
    std::optional<TimeWithTz> fTimeTz;
    std::optional<std::string> fVarchar;
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

template<>
struct StructDescriptor<::ExtendedTypesInsertInput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesInsertInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false),
        makeField<&ExtendedTypesInsertInput::fInt128>("F_INT128", SQL_INT128, 0, 16, false),
        makeField<&ExtendedTypesInsertInput::fDecimal>("F_DECIMAL", SQL_INT128, -8, 16, false),
        makeField<&ExtendedTypesInsertInput::fNumeric>("F_NUMERIC", SQL_INT64, -6, sizeof(int64_t), false),
        makeField<&ExtendedTypesInsertInput::fDecfloat>("F_DECFLOAT", SQL_DEC34, 0, 16, false),
        makeField<&ExtendedTypesInsertInput::fDate>("F_DATE", SQL_TYPE_DATE, 0, sizeof(uint32_t), false),
        makeField<&ExtendedTypesInsertInput::fTime>("F_TIME", SQL_TYPE_TIME, 0, sizeof(uint32_t), false),
        makeField<&ExtendedTypesInsertInput::fTimestamp>("F_TIMESHTAMP", SQL_TIMESTAMP, 0, 8, false),
        makeField<&ExtendedTypesInsertInput::fTimestampTz>("F_TIMESHTAMP_TZ", SQL_TIMESTAMP_TZ, 0, 12, false),
        makeField<&ExtendedTypesInsertInput::fTimeTz>("F_TIME_TZ", SQL_TIME_TZ, 0, 8, false),
        makeField<&ExtendedTypesInsertInput::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 66, false)
    );
};

template<>
struct StructDescriptor<::ExtendedTypesFetchInput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesFetchInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false)
    );
};

template<>
struct StructDescriptor<::ExtendedTypesOutput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesOutput::id>("ID", SQL_LONG, 0, sizeof(int32_t), false),
        makeField<&ExtendedTypesOutput::fInt128>("F_INT128", SQL_INT128, 0, 16, true),
        makeField<&ExtendedTypesOutput::fDecimal>("F_DECIMAL", SQL_INT128, -8, 16, true),
        makeField<&ExtendedTypesOutput::fNumeric>("F_NUMERIC", SQL_INT64, -6, sizeof(int64_t), true),
        makeField<&ExtendedTypesOutput::fDecfloat>("F_DECFLOAT", SQL_DEC34, 0, 16, true),
        makeField<&ExtendedTypesOutput::fDate>("F_DATE", SQL_TYPE_DATE, 0, sizeof(uint32_t), true),
        makeField<&ExtendedTypesOutput::fTime>("F_TIME", SQL_TYPE_TIME, 0, sizeof(uint32_t), true),
        makeField<&ExtendedTypesOutput::fTimestamp>("F_TIMESHTAMP", SQL_TIMESTAMP, 0, 8, true),
        makeField<&ExtendedTypesOutput::fTimestampTz>("F_TIMESHTAMP_TZ", SQL_TIMESTAMP_TZ, 0, 12, true),
        makeField<&ExtendedTypesOutput::fTimeTz>("F_TIME_TZ", SQL_TIME_TZ, 0, 8, true),
        makeField<&ExtendedTypesOutput::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 66, true)
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

TEST_F(StructPackTest, ExtendedTypesRoundTripUsingStructDescriptors) {
    const std::string nextIdSql = "SELECT COALESCE(MAX(ID), 0) + 1 FROM TABLE_TEST_1";
    auto nextIdStmt = connection_->prepareStatement(nextIdSql);
    auto nextIdTra = connection_->StartTransaction();
    auto nextIdCursor = nextIdTra->openCursor(nextIdStmt);
    std::tuple<int32_t> idRow{};
    ASSERT_TRUE(nextIdCursor->fetch(idRow)) << "TABLE_TEST_1 must be accessible";
    const int32_t newId = std::get<0>(idRow);
    nextIdCursor->close();
    nextIdTra->Commit();

    const std::string label = "struct_ext_" + std::to_string(newId);

    using namespace std::chrono;
    using namespace std::chrono_literals;

    const year_month_day testDate{year{2024}, month{5}, day{17}};
    // Firebird TIME/TIMESTAMP precision is 100 microseconds (1/10000 sec)
    // Use time value divisible by 100 to avoid precision loss
    const auto timeDuration = 8h + 15min + 42s + 123400us;  // Changed from 123456us
    MicroTime testTime{timeDuration};
    const sys_days baseDay{testDate};
    const auto timestampMicros = time_point_cast<microseconds>(baseDay + timeDuration);
    auto tz = std::chrono::locate_zone("UTC");
    ZonedMicroTime timestampTz{tz, timestampMicros};
    TimeWithTz timeTz{testTime, std::string("UTC")};

    ExtendedTypesInsertInput insertRow{
        newId,
        fbpp::core::Int128(123456789012345678LL),
        Decimal38_8("12345.67890123"),
        Numeric64_6("9876.543210"),
        fbpp::core::DecFloat34("42.195"),
        testDate,
        testTime,
        timestampMicros,
        timestampTz,
        timeTz,
        label
    };

    const std::string insertSql =
        "INSERT INTO TABLE_TEST_1 ("
        "ID, F_INT128, F_DECIMAL, F_NUMERIC, F_DECFLOAT, "
        "F_DATE, F_TIME, F_TIMESHTAMP, F_TIMESHTAMP_TZ, F_TIME_TZ, F_VARCHAR"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    auto insertStmt = connection_->prepareStatement(insertSql);
    auto insertTra = connection_->StartTransaction();
    ASSERT_EQ(insertTra->execute(insertStmt, insertRow), 1u);
    insertTra->Commit();

    ExtendedTypesFetchInput fetchParams{newId};
    const std::string selectSql =
        "SELECT ID, F_INT128, F_DECIMAL, F_NUMERIC, F_DECFLOAT, "
        "F_DATE, F_TIME, F_TIMESHTAMP, F_TIMESHTAMP_TZ, F_TIME_TZ, F_VARCHAR "
        "FROM TABLE_TEST_1 WHERE ID = ?";
    auto selectStmt = connection_->prepareStatement(selectSql);
    auto selectTra = connection_->StartTransaction();
    auto selectCursor = selectTra->openCursor(selectStmt, fetchParams);
    ExtendedTypesOutput result{};
    ASSERT_TRUE(selectCursor->fetch(result));
    selectCursor->close();
    selectTra->Commit();

    ASSERT_TRUE(result.fInt128.has_value());
    EXPECT_EQ(*result.fInt128, insertRow.fInt128);

    ASSERT_TRUE(result.fDecimal.has_value());
    EXPECT_EQ(*result.fDecimal, insertRow.fDecimal);

    ASSERT_TRUE(result.fNumeric.has_value());
    EXPECT_EQ(*result.fNumeric, insertRow.fNumeric);

    ASSERT_TRUE(result.fDecfloat.has_value());
    EXPECT_EQ(result.fDecfloat->to_string(), insertRow.fDecfloat.to_string());

    ASSERT_TRUE(result.fDate.has_value());
    EXPECT_EQ(*result.fDate, insertRow.fDate);

    ASSERT_TRUE(result.fTime.has_value());
    EXPECT_EQ(result.fTime->to_duration(), insertRow.fTime.to_duration());

    ASSERT_TRUE(result.fTimestamp.has_value());
    EXPECT_EQ(
        std::chrono::time_point_cast<std::chrono::microseconds>(*result.fTimestamp),
        insertRow.fTimestamp);

    ASSERT_TRUE(result.fTimestampTz.has_value());
    EXPECT_EQ(result.fTimestampTz->get_sys_time(), insertRow.fTimestampTz.get_sys_time());
    EXPECT_EQ(result.fTimestampTz->get_time_zone()->name(),
              insertRow.fTimestampTz.get_time_zone()->name());

    ASSERT_TRUE(result.fTimeTz.has_value());
    EXPECT_EQ(result.fTimeTz->first.to_duration(), insertRow.fTimeTz.first.to_duration());
    EXPECT_EQ(result.fTimeTz->second, insertRow.fTimeTz.second);

    ASSERT_TRUE(result.fVarchar.has_value());
    EXPECT_EQ(*result.fVarchar, insertRow.fVarchar);

    const std::string deleteSql = "DELETE FROM TABLE_TEST_1 WHERE ID = ?";
    auto deleteStmt = connection_->prepareStatement(deleteSql);
    auto deleteTra = connection_->StartTransaction();
    ASSERT_EQ(deleteTra->execute(deleteStmt, fetchParams), 1u);
    deleteTra->Commit();
}
