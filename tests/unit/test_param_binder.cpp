#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/param_binder.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <tuple>

using namespace fbpp::core;
using namespace fbpp::test;
using json = nlohmann::json;

class ParamBinderTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();

        connection_->ExecuteDDL(R"(
            CREATE TABLE pb_basic (
                id INTEGER NOT NULL PRIMARY KEY,
                f_smallint SMALLINT,
                f_bigint BIGINT,
                f_bool BOOLEAN,
                f_float FLOAT,
                f_double DOUBLE PRECISION,
                f_varchar VARCHAR(64),
                f_char CHAR(8),
                f_date DATE,
                f_time TIME,
                f_timestamp TIMESTAMP
            )
        )");

        connection_->ExecuteDDL(R"(
            CREATE TABLE pb_extended (
                id INTEGER NOT NULL PRIMARY KEY,
                f_int128 INT128,
                f_numeric_38_2 NUMERIC(38, 2),
                f_numeric_18_4 NUMERIC(18, 4),
                f_decfloat34 DECFLOAT(34),
                f_ts_tz TIMESTAMP WITH TIME ZONE
            )
        )");

        connection_->ExecuteDDL(R"(
            CREATE TABLE pb_blob (
                id INTEGER NOT NULL PRIMARY KEY,
                payload BLOB SUB_TYPE TEXT,
                payload_bin BLOB SUB_TYPE BINARY
            )
        )");
    }
};

// ============================================================================
// Basic native types — all go through sql_value_codec
// ============================================================================

TEST_F(ParamBinderTest, BindsNativeTypesByName) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(R"(
        INSERT INTO pb_basic (id, f_smallint, f_bigint, f_bool, f_float, f_double, f_varchar, f_char)
        VALUES (:id, :si, :bi, :b, :fl, :dbl, :v, :c)
    )");

    ParamBinder b(stmt, tx.get());
    EXPECT_TRUE(b.set("id", int32_t{1}));
    EXPECT_TRUE(b.set("si", int16_t{32000}));
    EXPECT_TRUE(b.set("bi", int64_t{9'000'000'000LL}));
    EXPECT_TRUE(b.set("b", true));
    EXPECT_TRUE(b.set("fl", 3.14f));
    EXPECT_TRUE(b.set("dbl", 2.718281828));
    EXPECT_TRUE(b.set("v", std::string("hello")));
    EXPECT_TRUE(b.set("c", std::string("ABCDEFGH")));

    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT f_smallint, f_bigint, f_bool, f_float, f_double, f_varchar, f_char "
        "FROM pb_basic WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{1}));
    std::tuple<int16_t, int64_t, bool, float, double, std::string, std::string> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), 32000);
    EXPECT_EQ(std::get<1>(row), 9'000'000'000LL);
    EXPECT_TRUE(std::get<2>(row));
    EXPECT_FLOAT_EQ(std::get<3>(row), 3.14f);
    EXPECT_DOUBLE_EQ(std::get<4>(row), 2.718281828);
    EXPECT_EQ(std::get<5>(row), "hello");
    // CHAR(8) is space-padded
    EXPECT_EQ(std::get<6>(row).substr(0, 8), "ABCDEFGH");
    cur->close();
    tx->Commit();
}

// ============================================================================
// Extended types: INT128, NUMERIC(38,2), DECFLOAT34, TIMESTAMP_TZ
// ============================================================================

TEST_F(ParamBinderTest, BindsExtendedTypes) {
    using Decimal38_2 = fbpp::adapters::TTNumeric<2, -2>;

    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(R"(
        INSERT INTO pb_extended (id, f_int128, f_numeric_38_2, f_decfloat34)
        VALUES (:id, :i128, :n382, :df34)
    )");

    ParamBinder b(stmt, tx.get());
    EXPECT_TRUE(b.set("id", int32_t{1}));
    EXPECT_TRUE(b.set("i128", Int128(987654321099LL)));
    EXPECT_TRUE(b.set("n382", Decimal38_2("1234567.89")));
    EXPECT_TRUE(b.set("df34", DecFloat34("42.1951234567")));

    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(R"(
        SELECT CAST(f_int128 AS VARCHAR(64)),
               CAST(f_numeric_38_2 AS VARCHAR(64)),
               CAST(f_decfloat34 AS VARCHAR(64))
        FROM pb_extended WHERE id = ?
    )");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{1}));
    std::tuple<std::string, std::string, std::string> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), "987654321099");
    EXPECT_EQ(std::get<1>(row), "1234567.89");
    // DECFLOAT may format with E-notation; just check substring
    EXPECT_NE(std::get<2>(row).find("42.1951234567"), std::string::npos);
    cur->close();
    tx->Commit();
}

// ============================================================================
// NUMERIC(18,4) — INT64 storage with scale=-4. Used for currency-like values.
// Verify three input paths: native double (codec scales it), TTNumeric adapter
// (contextual to_firebird_ctx), and std::string (codec parses + scales via
// SQL_INT64 branch).
// ============================================================================

TEST_F(ParamBinderTest, BindsNumeric18_4) {
    using Numeric18_4 = fbpp::adapters::TTNumeric<1, -4>;  // 8-byte INT64 storage

    auto tx = connection_->StartTransaction();
    auto ins = connection_->prepareStatement(R"(
        INSERT INTO pb_extended (id, f_numeric_18_4) VALUES (:id, :v)
    )");

    {
        ParamBinder b(ins, tx.get());
        b.set("id", int32_t{1});
        b.set("v", 12345.6789);            // double — codec multiplies by 10^4
        EXPECT_EQ(tx->execute(ins, b), 1u);
    }
    {
        ParamBinder b(ins, tx.get());
        b.set("id", int32_t{2});
        b.set("v", Numeric18_4("9876.5432"));  // contextual adapter
        EXPECT_EQ(tx->execute(ins, b), 1u);
    }
    {
        ParamBinder b(ins, tx.get());
        b.set("id", int32_t{3});
        b.set("v", std::string("0.0001"));  // string parsed via SQL_INT64 + scale
        EXPECT_EQ(tx->execute(ins, b), 1u);
    }

    auto sel = connection_->prepareStatement(
        "SELECT CAST(f_numeric_18_4 AS VARCHAR(32)) FROM pb_extended "
        "WHERE id = ?");

    auto fetchAsString = [&](int id) {
        auto cur = tx->openCursor(sel, std::make_tuple(int32_t{id}));
        std::tuple<std::string> row;
        EXPECT_TRUE(cur->fetch(row));
        cur->close();
        return std::get<0>(row);
    };

    EXPECT_EQ(fetchAsString(1), "12345.6789");
    EXPECT_EQ(fetchAsString(2), "9876.5432");
    EXPECT_EQ(fetchAsString(3), "0.0001");
    tx->Commit();
}

// ============================================================================
// BLOB SUB_TYPE BINARY — opaque byte payload. std::string carries the bytes.
// Round-trip: write raw bytes (including a NUL byte mid-string), read back,
// compare byte-for-byte.
// ============================================================================

TEST_F(ParamBinderTest, BindsBlobBinary) {
    auto tx = connection_->StartTransaction();
    auto ins = connection_->prepareStatement(
        "INSERT INTO pb_blob (id, payload_bin) VALUES (:id, :p)");

    // Construct a payload with embedded NUL byte and high-bit bytes — proves
    // the binary path doesn't go through any text/charset interpretation.
    std::string raw;
    raw.push_back('\x01');
    raw.push_back('\x00');
    raw.push_back('\xFE');
    raw.push_back('\xFF');
    raw.append("tail");

    ParamBinder b(ins, tx.get());
    b.set("id", int32_t{1});
    EXPECT_TRUE(b.set("p", raw));
    EXPECT_EQ(tx->execute(ins, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT payload_bin FROM pb_blob WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{1}));
    std::tuple<std::string> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row).size(), raw.size());
    EXPECT_EQ(std::get<0>(row), raw);
    cur->close();
    tx->Commit();
}

// ============================================================================
// clear() and reuse of binder across iterations
// ============================================================================

TEST_F(ParamBinderTest, ClearAndReuse) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar) VALUES (:id, :name)");

    ParamBinder b(stmt, tx.get());

    for (int i = 1; i <= 3; ++i) {
        b.clear();
        ASSERT_TRUE(b.set("id", int32_t{i}));
        ASSERT_TRUE(b.set("name", std::string("row_" + std::to_string(i))));
        EXPECT_EQ(tx->execute(stmt, b), 1u);
    }

    auto sel = connection_->prepareStatement(
        "SELECT id, f_varchar FROM pb_basic ORDER BY id");
    auto cur = tx->openCursor(sel);
    int seen = 0;
    std::tuple<int32_t, std::string> row;
    while (cur->fetch(row)) {
        ++seen;
        EXPECT_EQ(std::get<0>(row), seen);
        EXPECT_EQ(std::get<1>(row), "row_" + std::to_string(seen));
    }
    EXPECT_EQ(seen, 3);
    cur->close();
    tx->Commit();
}

// ============================================================================
// Unknown name returns false (non-fatal — legacy IBO behaviour)
// ============================================================================

TEST_F(ParamBinderTest, UnknownNameReturnsFalse) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar) VALUES (:id, :name)");

    ParamBinder b(stmt, tx.get());
    EXPECT_TRUE(b.set("id", int32_t{42}));
    EXPECT_TRUE(b.set("name", std::string("ok")));
    EXPECT_FALSE(b.set("typo_param", 999));   // not in SQL
    EXPECT_FALSE(b.setNull("nope"));

    EXPECT_EQ(tx->execute(stmt, b), 1u);
    tx->Commit();
}

// ============================================================================
// Trim + case-insensitive name lookup
// ============================================================================

TEST_F(ParamBinderTest, NormalizesNameTrimAndLower) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar) VALUES (:Doc_Id, :NAME)");

    ParamBinder b(stmt, tx.get());
    EXPECT_TRUE(b.set("  doc_id  ", int32_t{7}));   // trim + lowercase
    EXPECT_TRUE(b.set("Name", std::string("trimmed"))); // mixed case
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT f_varchar FROM pb_basic WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{7}));
    std::tuple<std::string> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), "trimmed");
    cur->close();
    tx->Commit();
}

// ============================================================================
// Repeated :name in SQL — single set() fills all positions
// ============================================================================

TEST_F(ParamBinderTest, RepeatedParameterFillsAllPositions) {
    auto tx = connection_->StartTransaction();
    auto ins = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_bigint) VALUES (:id, :v)");
    {
        ParamBinder b(ins, tx.get());
        b.set("id", int32_t{1});
        b.set("v", int64_t{100});
        tx->execute(ins, b);
    }
    {
        ParamBinder b(ins, tx.get());
        b.set("id", int32_t{2});
        b.set("v", int64_t{200});
        tx->execute(ins, b);
    }

    auto sel = connection_->prepareStatement(
        "SELECT id FROM pb_basic WHERE f_bigint >= :v OR id = :v ORDER BY id");

    ParamBinder b(sel, tx.get());
    ASSERT_TRUE(b.set("v", int64_t{150}));   // both positions get 150

    auto cur = tx->openCursor(sel, b);
    std::vector<int32_t> ids;
    std::tuple<int32_t> row;
    while (cur->fetch(row)) ids.push_back(std::get<0>(row));
    cur->close();
    // Both rows match: row 2 has f_bigint=200 >= 150, row 1 (id=1) doesn't,
    // but neither row has id=150 so the second predicate never matches.
    // Only row id=2 should appear.
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 2);
    tx->Commit();
}

// ============================================================================
// Missing parameter → NULL (must match JSON-path behaviour)
// ============================================================================

TEST_F(ParamBinderTest, MissingParameterIsNull) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar, f_bigint) VALUES (:id, :name, :amount)");

    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{1});
    b.set("name", std::string("partial"));
    // :amount intentionally NOT set
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT f_varchar, f_bigint FROM pb_basic WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{1}));
    std::tuple<std::string, std::optional<int64_t>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), "partial");
    EXPECT_FALSE(std::get<1>(row).has_value());   // NULL
    cur->close();
    tx->Commit();
}

// ============================================================================
// setNull(name)
// ============================================================================

TEST_F(ParamBinderTest, SetNullByName) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar) VALUES (:id, :name)");

    ParamBinder b(stmt, tx.get());
    b.set("id", int32_t{99});
    EXPECT_TRUE(b.setNull("name"));
    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT f_varchar FROM pb_basic WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{99}));
    std::tuple<std::optional<std::string>> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_FALSE(std::get<0>(row).has_value());
    cur->close();
    tx->Commit();
}

// ============================================================================
// Lazy transaction — bind without tx, attach later for BLOB
// ============================================================================

TEST_F(ParamBinderTest, LazyTransactionForBlob) {
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_blob (id, payload) VALUES (:id, :p)");

    // Construct binder BEFORE we start a transaction.
    ParamBinder b(stmt);
    EXPECT_TRUE(b.set("id", int32_t{5}));   // non-BLOB ok without tx

    // Now we open transaction and attach it.
    auto tx = connection_->StartTransaction();
    b.setTransaction(tx.get());
    EXPECT_TRUE(b.set("p", std::string("blob payload")));

    EXPECT_EQ(tx->execute(stmt, b), 1u);

    auto sel = connection_->prepareStatement(
        "SELECT payload FROM pb_blob WHERE id = ?");
    auto cur = tx->openCursor(sel, std::make_tuple(int32_t{5}));
    std::tuple<std::string> row;
    ASSERT_TRUE(cur->fetch(row));
    EXPECT_EQ(std::get<0>(row), "blob payload");
    cur->close();
    tx->Commit();
}

// ============================================================================
// Move into std::optional (storage scenario)
// ============================================================================

TEST_F(ParamBinderTest, MoveIntoOptional) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id, f_varchar) VALUES (:id, :name)");

    std::optional<ParamBinder> slot;
    slot.emplace(stmt, tx.get());        // emplace via constructor
    ASSERT_TRUE(slot.has_value());
    EXPECT_TRUE(slot->set("id", int32_t{77}));
    EXPECT_TRUE(slot->set("name", std::string("from-optional")));
    EXPECT_EQ(tx->execute(stmt, *slot), 1u);

    // reset and re-emplace — should not throw, no leaks
    slot.reset();
    EXPECT_FALSE(slot.has_value());
    slot.emplace(stmt, tx.get());
    slot->set("id", int32_t{78});
    slot->set("name", std::string("again"));
    EXPECT_EQ(tx->execute(stmt, *slot), 1u);

    tx->Commit();
}

// ============================================================================
// JSON parity — same INSERT via JSON and via ParamBinder, then compare
// ============================================================================

TEST_F(ParamBinderTest, ParityWithJsonPath) {
    using Decimal38_2 = fbpp::adapters::TTNumeric<2, -2>;

    auto tx = connection_->StartTransaction();
    auto ins = connection_->prepareStatement(R"(
        INSERT INTO pb_extended (id, f_int128, f_numeric_38_2, f_decfloat34)
        VALUES (:id, :i128, :n382, :df34)
    )");

    // Insert row #1 via JSON path.
    json jp;
    jp["id"]   = 1;
    jp["i128"] = "987654321099";
    jp["n382"] = "1234567.89";
    jp["df34"] = "42.1951234567";
    EXPECT_EQ(tx->execute(ins, jp), 1u);

    // Insert row #2 via ParamBinder path with native types.
    ParamBinder b(ins, tx.get());
    b.set("id",   int32_t{2});
    b.set("i128", Int128(987654321099LL));
    b.set("n382", Decimal38_2("1234567.89"));
    b.set("df34", DecFloat34("42.1951234567"));
    EXPECT_EQ(tx->execute(ins, b), 1u);

    // Compare both rows as strings — they must be identical.
    auto sel = connection_->prepareStatement(R"(
        SELECT CAST(f_int128 AS VARCHAR(64)),
               CAST(f_numeric_38_2 AS VARCHAR(64)),
               CAST(f_decfloat34 AS VARCHAR(64))
        FROM pb_extended WHERE id = ?
    )");

    auto fetch = [&](int id) {
        auto cur = tx->openCursor(sel, std::make_tuple(int32_t{id}));
        std::tuple<std::string, std::string, std::string> row;
        EXPECT_TRUE(cur->fetch(row));
        cur->close();
        return row;
    };

    auto a = fetch(1);
    auto c = fetch(2);
    EXPECT_EQ(a, c);

    tx->Commit();
}

// ============================================================================
// Statement without input params — set() is non-fatal (returns false)
// ============================================================================

TEST_F(ParamBinderTest, NoInputParamsBinderEmpty) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "INSERT INTO pb_basic (id) VALUES (1234)");   // no params

    ParamBinder b(stmt, tx.get());
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.set("anything", 1));
    EXPECT_EQ(tx->execute(stmt, b), 1u);
    tx->Commit();
}
