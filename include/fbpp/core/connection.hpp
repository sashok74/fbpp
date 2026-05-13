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

struct ConnectionOptions {
    StatementCacheConfig statementCache;
};

struct ConnectionParams {
    std::string database;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::string role;
    int sql_dialect = 3;  // SQL dialect (1, 2, or 3). Default: 3 (modern dialect)
    ConnectionOptions options;
};

// Procedure metadata exposed by Connection::listProcedures /
// Connection::describeProcedure. The shape complements the static schema
// inspector by carrying a wire-level FieldInfo per parameter (sourced
// from a probe prepare, so type/scale/length match describeQuery).
enum class ProcedureKind {
    Executable,   // EXECUTE PROCEDURE; OUT params returned as a single row.
    Selectable,   // SELECT * FROM proc(...); SUSPEND-driven cursor.
    Unknown
};

enum class ParamDirection { Input, Output };

struct ProcedureParamInfo {
    std::string    procedureName;
    std::string    packageName;     // empty for non-packaged procedures
    std::string    name;             // parameter name from RDB$PROCEDURE_PARAMETERS
    ParamDirection direction;
    FieldInfo      field;            // wire layout — same shape as describeQuery
    unsigned       position = 0;     // 0-based within direction
};

struct ProcedureInfo {
    std::string                     name;
    std::string                     packageName;     // empty for non-packaged
    ProcedureKind                   kind = ProcedureKind::Unknown;
    std::vector<ProcedureParamInfo> params;
};

// Cancel operation options (from Firebird's consts_pub.h)
enum class CancelOperation : int {
    DISABLE = 1,  // fb_cancel_disable - Disable execution of fb_cancel_raise requests
    ENABLE = 2,   // fb_cancel_enable - Enable previously disabled cancel delivery
    RAISE = 3,    // fb_cancel_raise - Cancel any activity related to this attachment
    ABORT = 4     // fb_cancel_abort - Forcefully abort operations
};

/**
 * Firebird attachment wrapper with explicit per-connection runtime options.
 *
 * Thread-safety contract:
 * - Connection is not internally synchronized for concurrent use.
 * - Treat one Connection instance as thread-affine: use it from one thread at
 *   a time, and keep its Statement / Transaction / ResultSet objects on the
 *   same thread as the owning connection.
 * - Environment remains process-wide, but runtime behavior such as statement
 *   caching is configured per Connection via ConnectionOptions.
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

    // Prepare a one-off statement without consulting or populating the
    // statement cache. Use when scanning many SQLs for metadata only —
    // typical CI manifest tooling on 1000+ procedures — so the working
    // statement cache is not evicted by transient lookups. The returned
    // Statement owns the underlying IStatement and releases it when it
    // goes out of scope.
    std::shared_ptr<Statement> prepareStatementUncached(
        const std::string& sql,
        unsigned flags = Statement::PREPARE_PREFETCH_ALL);

    // Clear all cached statements
    void clearStatementCache();

    // Get cache statistics
    StatementCache::Statistics getCacheStatistics() const;

    // Configure runtime connection behavior
    void setOptions(const ConnectionOptions& options);
    const ConnectionOptions& getOptions() const { return options_; }
    void setStatementCacheConfig(const StatementCacheConfig& config);
    const StatementCacheConfig& getStatementCacheConfig() const {
        return options_.statementCache;
    }

    // Check if connected
    bool isConnected() const;

    // Cancel operations on this attachment
    void cancelOperation(CancelOperation option);

    // Major version of the attached Firebird engine (3, 4, 5, ...). Cached
    // after the first call. Use to gate access to FB4+-only types
    // (INT128, DECFLOAT, TIMESTAMP/TIME WITH TIME ZONE) — those types
    // do not appear in metadata on FB3, so reading them through codec
    // surfaces an opaque "unsupported SQL type" error otherwise.
    int getEngineMajorVersion() const;

    // Get attachment
    Firebird::IAttachment* getAttachment() const { return attachment_; }

    struct QueryMetadataInfo {
        std::vector<FieldInfo> inputFields;
        std::vector<FieldInfo> outputFields;
    };

    QueryMetadataInfo describeQuery(const std::string& sql);

    // Enumerate user procedures from RDB$PROCEDURES. Returns name / package
    // / kind only (params not loaded — call describeProcedure for that).
    // System procedures (RDB$SYSTEM_FLAG != 0) are excluded. Goes through
    // prepareStatementUncached so listing many procedures does not flush
    // the working statement cache. Complements fbpp::schema::SchemaInspector
    // by exposing wire-level FieldInfo per parameter on the describe path.
    std::vector<ProcedureInfo> listProcedures();

    // Full metadata for one procedure, including parameter names and a
    // FieldInfo per parameter (sourced from a probe prepare so types
    // match exactly what describeQuery would return for the same call).
    // Throws if the procedure is unknown.
    ProcedureInfo describeProcedure(
        const std::string& name,
        const std::string& packageName = std::string());

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
    // Each connection owns its status wrapper and keeps it thread-local by contract.
    Firebird::IStatus* status_;                    // created from master, disposed in destructor
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    ConnectionOptions options_{};

    // Statement cache (lazy initialized)
    mutable std::unique_ptr<StatementCache> statementCache_;

    // Cached engine version (lazy on first getEngineMajorVersion()).
    mutable int engineMajor_ = 0;
};

} // namespace core
} // namespace fbpp
