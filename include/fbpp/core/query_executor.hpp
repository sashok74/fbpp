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
