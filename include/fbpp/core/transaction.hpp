#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include <firebird/Interface.h>
#include <memory>
#include <vector>

namespace fbpp {
namespace core {

// Forward declarations
class Connection;
class Statement;
class ResultSet;
class Batch;

class Transaction : public std::enable_shared_from_this<Transaction> {
public:
    // Constructor
    Transaction(Connection* connection, Firebird::ITransaction* transaction);
    
    // Destructor - auto-rollback if not committed
    ~Transaction();
    
    // Commit transaction
    void Commit();
    
    // Rollback transaction
    void Rollback();
    
    // Commit retaining - keeps transaction active
    void CommitRetaining();
    
    // Rollback retaining - keeps transaction active
    void RollbackRetaining();
    
    // Check if transaction is active
    bool isActive() const;
    
    // Get raw transaction interface
    Firebird::ITransaction* getTransaction() const {
        return transaction_;
    }
    
    // Alias for getTransaction() for consistency
    Firebird::ITransaction* getRawTransaction() const {
        return transaction_;
    }
    
    // Get connection
    Connection* getConnection() const {
        return connection_;
    }
    
    // BLOB operations
    std::vector<uint8_t> loadBlob(ISC_QUAD* blobId);
    ISC_QUAD createBlob(const std::vector<uint8_t>& data);
    
    // Execute operations with Statement (for INSERT/UPDATE/DELETE)
    unsigned execute(const std::unique_ptr<Statement>& statement);
    unsigned execute(const std::shared_ptr<Statement>& statement);

    // Template methods for parameterized queries
    template<typename ParamsType>
    unsigned execute(const std::unique_ptr<Statement>& statement,
                    const ParamsType& params);

    template<typename ParamsType>
    unsigned execute(const std::shared_ptr<Statement>& statement,
                    const ParamsType& params);

    // Execute with RETURNING clause
    template<typename InParams, typename OutParams>
    std::pair<unsigned, OutParams> execute(const std::unique_ptr<Statement>& statement,
                                          const InParams& inParams,
                                          const OutParams& outTemplate);

    template<typename InParams, typename OutParams>
    std::pair<unsigned, OutParams> execute(const std::shared_ptr<Statement>& statement,
                                          const InParams& inParams,
                                          const OutParams& outTemplate);

    // Cursor operations with Statement (for SELECT)
    std::unique_ptr<ResultSet> openCursor(const std::unique_ptr<Statement>& statement);
    std::unique_ptr<ResultSet> openCursor(const std::shared_ptr<Statement>& statement);

    template<typename ParamsType>
    std::unique_ptr<ResultSet> openCursor(const std::unique_ptr<Statement>& statement,
                                          const ParamsType& params);

    template<typename ParamsType>
    std::unique_ptr<ResultSet> openCursor(const std::shared_ptr<Statement>& statement,
                                          const ParamsType& params);

    // Batch operations
    std::unique_ptr<Batch> createBatch(const std::unique_ptr<Statement>& statement,
                                       bool recordCounts = true,
                                       bool continueOnError = false);

    std::unique_ptr<Batch> createBatch(const std::shared_ptr<Statement>& statement,
                                       bool recordCounts = true,
                                       bool continueOnError = false);
    
    // Non-copyable
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    
    // Movable
    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) noexcept;
    
private:
    // Internal helper to get a ready-to-use ThrowStatusWrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
    
    Environment& env_;
    Connection* connection_;
    Firebird::ITransaction* transaction_;
    Firebird::IStatus* status_;
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    bool active_;
};

} // namespace core
} // namespace fbpp
