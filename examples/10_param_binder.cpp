// Example 10: Typed name-based parameter binding via ParamBinder
//
// Demonstrates fbpp::core::ParamBinder — typed binding of named parameters
// without going through nlohmann::json. Designed for use cases where:
//   - parameter values are already in their native C++ type and should not
//     be detoured through JSON serialization,
//   - the binder is created lazily (before a transaction is known) and
//     reused across iterations via clear(),
//   - unknown parameter names should be skipped silently (legacy IBO).
//
// Compare to 06_named_parameters.cpp, which does the same scenarios via
// nlohmann::json.

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/param_binder.hpp"
#include <fbpp_util/connection_helper.hpp>

#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>

using namespace fbpp::core;

namespace {

void heading(const char* title) {
    std::cout << "\n" << std::string(60, '=') << "\n"
              << title << "\n"
              << std::string(60, '=') << "\n";
}

int32_t maxKey(Connection& conn) {
    auto stmt = conn.prepareStatement(
        "SELECT COALESCE(MAX(F_INTEGER), 1000000) FROM TABLE_TEST_1");
    auto tx = conn.StartTransaction();
    auto cur = tx->openCursor(stmt);
    std::tuple<int32_t> r{};
    cur->fetch(r);
    cur->close();
    tx->Commit();
    return std::get<0>(r);
}

} // namespace

int main() try {
    auto params = fbpp::util::getConnectionParams("db");
    Connection conn(params);
    std::cout << "Connected to " << params.database << "\n";

    const int32_t base = maxKey(conn) + 1000;
    std::cout << "Using base key: " << base << "\n";

    // Statement is cached and reused across all iterations below.
    auto ins = conn.prepareStatement(R"SQL(
        INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN)
        VALUES (:id, :name, :price, :active)
    )SQL");

    heading("1. Bind native types by name");
    {
        auto tx = conn.StartTransaction();
        ParamBinder b(ins, tx.get());
        b.set("id",     int32_t{base + 1});
        b.set("name",   std::string("ParamBinder demo"));
        b.set("price",  99.99);
        b.set("active", true);
        std::cout << "Inserted: " << tx->execute(ins, b) << " row(s)\n";
        tx->Commit();
    }

    heading("2. Reuse binder across loop iterations via clear()");
    {
        auto tx = conn.StartTransaction();
        ParamBinder b(ins, tx.get());
        for (int i = 0; i < 3; ++i) {
            b.clear();
            b.set("id",     int32_t{base + 10 + i});
            b.set("name",   std::string("loop_") + std::to_string(i));
            b.set("price",  10.0 + i);
            b.set("active", i % 2 == 0);
            tx->execute(ins, b);
        }
        std::cout << "Inserted 3 rows by reusing the binder\n";
        tx->Commit();
    }

    heading("3. Unknown parameter is silently skipped (legacy IBO behaviour)");
    {
        auto tx = conn.StartTransaction();
        ParamBinder b(ins, tx.get());
        b.set("id",   int32_t{base + 20});
        b.set("name", std::string("typo_test"));
        bool ok = b.set("PrIcE",         42.0);   // case-insensitive: matches :price
        bool no = b.set("nonexistent",   "x");    // not in SQL: returns false
        std::cout << "set(\"PrIcE\") -> " << std::boolalpha << ok << "\n";
        std::cout << "set(\"nonexistent\") -> " << std::boolalpha << no << "\n";
        tx->execute(ins, b);
        tx->Commit();
    }

    heading("4. setNull() and missing parameter both yield NULL");
    {
        auto tx = conn.StartTransaction();
        ParamBinder b(ins, tx.get());
        b.set("id",   int32_t{base + 30});
        b.set("name", std::string("nulls"));
        b.setNull("price");      // explicit NULL
        // :active deliberately not set — also becomes NULL
        tx->execute(ins, b);
        tx->Commit();
    }

    heading("5. Repeated :param in SELECT — single set() fills all positions");
    {
        auto sel = conn.prepareStatement(R"SQL(
            SELECT F_INTEGER, F_VARCHAR
            FROM TABLE_TEST_1
            WHERE F_INTEGER >= :v AND F_INTEGER <= :v + 25
            ORDER BY F_INTEGER
        )SQL");

        auto tx = conn.StartTransaction();
        ParamBinder b(sel, tx.get());
        b.set("v", int32_t{base});

        auto cur = tx->openCursor(sel, b);
        std::tuple<int32_t, std::optional<std::string>> row;
        while (cur->fetch(row)) {
            std::cout << "  " << std::get<0>(row) << "  "
                      << (std::get<1>(row) ? *std::get<1>(row) : std::string("<null>"))
                      << "\n";
        }
        cur->close();
        tx->Commit();
    }

    heading("Cleanup");
    {
        auto tx = conn.StartTransaction();
        auto del = conn.prepareStatement(
            "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER BETWEEN :lo AND :hi");
        ParamBinder b(del, tx.get());
        b.set("lo", int32_t{base});
        b.set("hi", int32_t{base + 100});
        std::cout << "Deleted: " << tx->execute(del, b) << " row(s)\n";
        tx->Commit();
    }

    std::cout << "\nDone.\n";
    return 0;
}
catch (const FirebirdException& e) {
    std::cerr << "Firebird error: " << e.what() << "\n";
    return 1;
}
catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
}
