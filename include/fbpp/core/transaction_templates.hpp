#pragma once

// Template implementations for Transaction class
// This file should be included after both Transaction and Statement are fully defined

#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/result_set.hpp"
#include <optional>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

// Template implementations - must be in header due to template instantiation rules
template<typename ParamsType>
unsigned Transaction::execute(const std::unique_ptr<Statement>& statement,
                             const ParamsType& params) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    // Forward to Statement's execute method
    return statement->execute(this, params);
}

template<typename InParams, typename OutParams>
std::pair<unsigned, OutParams> Transaction::execute(const std::unique_ptr<Statement>& statement,
                                                   const InParams& inParams,
                                                   const OutParams& outTemplate) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    return statement->execute(this, inParams, outTemplate);
}

template<typename ParamsType>
std::unique_ptr<ResultSet> Transaction::openCursor(const std::unique_ptr<Statement>& statement,
                                                   const ParamsType& params) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    return statement->openCursor(this, params);
}

// Shared pointer versions
template<typename ParamsType>
unsigned Transaction::execute(const std::shared_ptr<Statement>& statement,
                             const ParamsType& params) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    // Forward to Statement's execute method
    return statement->execute(this, params);
}

template<typename InParams, typename OutParams>
std::pair<unsigned, OutParams> Transaction::execute(const std::shared_ptr<Statement>& statement,
                                                   const InParams& inParams,
                                                   const OutParams& outTemplate) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    return statement->execute(this, inParams, outTemplate);
}

template<typename ParamsType>
std::unique_ptr<ResultSet> Transaction::openCursor(const std::shared_ptr<Statement>& statement,
                                                   const ParamsType& params) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    return statement->openCursor(this, params);
}

// QueryDescriptor pattern methods
template<typename Descriptor>
std::vector<typename Descriptor::Output> Transaction::executeQuery(
    const typename Descriptor::Input& params) {

    auto statement = connection_->prepareStatement(std::string(Descriptor::sql));

    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    std::unique_ptr<ResultSet> cursor;
    if (hasParams) {
        cursor = openCursor(statement, params);
    } else {
        cursor = openCursor(statement);
    }

    std::vector<typename Descriptor::Output> rows;
    typename Descriptor::Output row{};
    while (cursor->fetch(row)) {
        rows.push_back(row);
    }
    return rows;
}

template<typename Descriptor>
std::optional<typename Descriptor::Output> Transaction::fetchOne(
    const typename Descriptor::Input& params) {

    auto statement = connection_->prepareStatement(std::string(Descriptor::sql));

    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    std::unique_ptr<ResultSet> cursor;
    if (hasParams) {
        cursor = openCursor(statement, params);
    } else {
        cursor = openCursor(statement);
    }

    typename Descriptor::Output row{};
    if (cursor->fetch(row)) {
        return row;
    }
    return std::nullopt;
}

template<typename Descriptor>
unsigned Transaction::executeNonQuery(const typename Descriptor::Input& params) {
    auto statement = connection_->prepareStatement(std::string(Descriptor::sql));

    bool hasParams = false;
    if (auto meta = statement->getInputMetadata()) {
        hasParams = meta->getCount() > 0;
    }

    if (hasParams) {
        return execute(statement, params);
    }
    return execute(statement);
}

} // namespace core
} // namespace fbpp