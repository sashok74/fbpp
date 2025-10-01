#pragma once

// Template implementations for Transaction class
// This file should be included after both Transaction and Statement are fully defined

#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"

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

} // namespace core
} // namespace fbpp