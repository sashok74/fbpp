#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/named_param_parser.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <memory>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

// Forward declarations
class Transaction;
class Statement;
class ResultSet;
class StatementCache;

struct ConnectionParams {
    std::string database;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::string role;
    int sql_dialect = 3;  // SQL dialect (1, 2, or 3). Default: 3 (modern dialect)
};

// Cancel operation options (from Firebird's consts_pub.h)
enum class CancelOperation : int {
    DISABLE = 1,  // fb_cancel_disable - Disable execution of fb_cancel_raise requests
    ENABLE = 2,   // fb_cancel_enable - Enable previously disabled cancel delivery
    RAISE = 3,    // fb_cancel_raise - Cancel any activity related to this attachment
    ABORT = 4     // fb_cancel_abort - Forcefully abort operations
};

/**
 * Connection owns its own IStatus/ThrowStatusWrapper.
 * Assumption: one connection is used from one thread.
 */
class Connection {
public:
    // Constructor with database path
    explicit Connection(const std::string& database);

    // Constructor with connection parameters
    explicit Connection(const ConnectionParams& params);

    // Destructor
    ~Connection();

    // Static methods for database management
    static void createDatabase(const std::string& database, const ConnectionParams& params = {});
    static void createDatabase(const ConnectionParams& params);
    static void dropDatabase(const std::string& database, const ConnectionParams& params = {});
    static void dropDatabase(const ConnectionParams& params);
    static bool databaseExists(const std::string& database, const ConnectionParams& params = {});

    // Execute SQL and return transaction (for quick operations)
    std::shared_ptr<Transaction> Execute(const std::string& sql);

    // Start a new transaction
    std::shared_ptr<Transaction> StartTransaction();

    // Execute SQL in existing transaction
    void ExecuteInTransaction(Transaction* tra, const std::string& sql);

    // Execute DDL in new transaction (for table creation etc)
    void ExecuteDDL(const std::string& ddl);

    // Prepare statement with cache (returns shared_ptr for shared ownership)
    std::shared_ptr<Statement> prepareStatement(const std::string& sql, unsigned flags = 0);

    // Clear all cached statements
    void clearStatementCache();

    // Get cache statistics
    StatementCache::Statistics getCacheStatistics() const;

    // Check if connected
    bool isConnected() const;

    // Cancel operations on this attachment
    void cancelOperation(CancelOperation option);

    // Get attachment
    Firebird::IAttachment* getAttachment() const { return attachment_; }

    struct QueryMetadataInfo {
        std::vector<FieldInfo> inputFields;
        std::vector<FieldInfo> outputFields;
    };

    QueryMetadataInfo describeQuery(const std::string& sql);

    // Non-copyable
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

private:
    // Internal helper to get a ready-to-use ThrowStatusWrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }

    void connect(const ConnectionParams& params);
    void disconnect();

    Firebird::IAttachment* attachment_ = nullptr;
    Environment& env_;
    // Each connection owns its status
    Firebird::IStatus* status_;                    // created from master, disposed in destructor
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};

    // Statement cache (lazy initialized)
    mutable std::unique_ptr<StatementCache> statementCache_;
};

} // namespace core
} // namespace fbpp

// Include template implementations for Transaction methods
// This must be at the very end after Transaction, Statement, and Connection are all fully defined
#include "fbpp/core/transaction_templates.hpp"
