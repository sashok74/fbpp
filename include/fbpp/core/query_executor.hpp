#pragma once

#include "fbpp/core/connection.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp/core/transaction.hpp"
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fbpp::core {

/// Intended execution mode of a generated query descriptor. Emitted by the
/// code generator as `Descriptor::mode` so callers (and future generic
/// dispatchers) know which executeXxx entry point fits the statement:
///   Select     -> executeQuery / fetchOne (set-returning cursor)
///   Returning  -> executeReturning / executeReturningOne (single row out)
///   Modify     -> executeNonQuery (affected count, no output)
///   Ddl        -> Connection::ExecuteDDL
/// It is advisory; hand-written descriptors may omit it.
enum class QueryMode { Unknown, Select, Modify, Returning, Ddl };

struct NoResult {};

template<>
struct StructDescriptor<NoResult> {
    static constexpr auto fields = std::make_tuple();
};

template<typename Descriptor>
std::vector<typename Descriptor::Output> executeQuery(Connection& connection,
                                                      Transaction& transaction,
                                                      const typename Descriptor::Input& params) {
    auto statement = connection.prepareStatement(std::string(Descriptor::sql));
    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    std::unique_ptr<ResultSet> cursor;
    if (hasParams) {
        cursor = transaction.openCursor(statement, params);
    } else {
        cursor = transaction.openCursor(statement);
    }

    std::vector<typename Descriptor::Output> rows;
    typename Descriptor::Output row{};
    while (cursor->fetch(row)) {
        rows.push_back(row);
    }
    return rows;
}

template<typename Descriptor>
std::optional<typename Descriptor::Output> fetchOne(Connection& connection,
                                                    Transaction& transaction,
                                                    const typename Descriptor::Input& params) {
    auto statement = connection.prepareStatement(std::string(Descriptor::sql));
    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    std::unique_ptr<ResultSet> cursor;
    if (hasParams) {
        cursor = transaction.openCursor(statement, params);
    } else {
        cursor = transaction.openCursor(statement);
    }

    typename Descriptor::Output row{};
    if (cursor->fetch(row)) {
        return row;
    }
    return std::nullopt;
}

/// Execute a singleton statement that BOTH takes parameters and returns one
/// row of output — `INSERT/UPDATE/DELETE ... RETURNING` of a single row, or
/// `EXECUTE PROCEDURE` with OUT parameters. Returns {affectedRows, output}.
///
/// For set-returning statements (`SELECT`, selectable stored procedures, or
/// multi-row `RETURNING`) use executeQuery/fetchOne instead — those open a
/// cursor; this path reads exactly one output message.
template<typename Descriptor>
std::pair<unsigned, typename Descriptor::Output> executeReturning(
        Connection& connection,
        Transaction& transaction,
        const typename Descriptor::Input& params) {
    auto statement = connection.prepareStatement(std::string(Descriptor::sql));
    return transaction.execute(statement, params, typename Descriptor::Output{});
}

/// Convenience over executeReturning() when the affected-row count is not
/// needed — returns just the output row.
template<typename Descriptor>
typename Descriptor::Output executeReturningOne(
        Connection& connection,
        Transaction& transaction,
        const typename Descriptor::Input& params) {
    return executeReturning<Descriptor>(connection, transaction, params).second;
}

template<typename Descriptor>
unsigned executeNonQuery(Connection& connection,
                         Transaction& transaction,
                         const typename Descriptor::Input& params) {
    auto statement = connection.prepareStatement(std::string(Descriptor::sql));
    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    if (hasParams) {
        return transaction.execute(statement, params);
    }
    return transaction.execute(statement);
}

} // namespace fbpp::core
