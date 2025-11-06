#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/status_utils.hpp"
#include "fbpp_util/logging.h"
#include "fbpp_util/config.h"
#include <stdexcept>

namespace fbpp {
namespace core {

Connection::Connection(const std::string& database)
    : env_(Environment::getInstance())
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating connection to database: {}", database);
    }
    ConnectionParams params;
    params.database = database;
    connect(params);
}

Connection::Connection(const ConnectionParams& params)
    : env_(Environment::getInstance())
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating connection with params - database: {}, user: {}",
                      params.database, params.user);
    }
    connect(params);
}

Connection::~Connection() {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Destroying connection");
    }
    disconnect();
    // Dispose our owned IStatus
    statusWrapper_.dispose();
}

void Connection::connect(const ConnectionParams& params) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->info("Connecting to database: {}", params.database);
    }

    try {
        auto& st = status();

        // Build DPB
        Firebird::IXpbBuilder* dpb = env_.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0);

        if (!params.user.empty()) {
            dpb->insertString(&st, isc_dpb_user_name, params.user.c_str());
        }

        if (!params.password.empty()) {
            dpb->insertString(&st, isc_dpb_password, params.password.c_str());
        }

        if (!params.charset.empty()) {
            dpb->insertString(&st, isc_dpb_set_db_charset, params.charset.c_str());
        }

        if (!params.role.empty()) {
            dpb->insertString(&st, isc_dpb_sql_role_name, params.role.c_str());
        }

        // Attach to database
        attachment_ = env_.getProvider()->attachDatabase(
            &st,
            params.database.c_str(),
            dpb->getBufferLength(&st),
            dpb->getBuffer(&st));

        dpb->dispose();

        if (!attachment_) {
            if (logger) {
                logger->error("Failed to attach to database: {}", params.database);
            }
            throw FirebirdException("Failed to attach to database: " + params.database);
        }

        if (logger) {
            logger->info("Successfully connected to database");
        }
    }
    catch (const Firebird::FbException& e) {
        if (logger) {
            logger->error("Connection failed with Firebird exception");
        }
        throw FirebirdException(e);
    }
}

void Connection::disconnect() {
    auto logger = util::Logging::get();
    if (attachment_) {
        if (logger) {
            logger->debug("Disconnecting from database");
        }
        try {
            auto& st = status();
            attachment_->detach(&st);
            attachment_->release();
            if (logger) {
                logger->info("Successfully disconnected from database");
            }
        }
        catch (...) {
            if (logger) {
                logger->warn("Error during disconnect (ignored)");
            }
            // Ignore errors during disconnect
        }
        attachment_ = nullptr;
    }
}

std::shared_ptr<Transaction> Connection::Execute(const std::string& sql) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Executing SQL: {}", sql.substr(0, 100)); // Log first 100 chars
    }

    if (!attachment_) {
        if (logger) {
            logger->error("Cannot execute SQL - not connected to database");
        }
        throw FirebirdException("Not connected to database");
    }

    try {
        auto& st = status();

        // Start transaction
        Firebird::ITransaction* tra = attachment_->startTransaction(&st, 0, nullptr);
        if (!tra) {
            throw FirebirdException("Failed to start transaction");
        }

        // Try to prepare the statement first to catch syntax errors
        Firebird::IStatement* stmt = attachment_->prepare(
            &st, tra, 0, sql.c_str(), 3, 0);

        if (!stmt) {
            // Statement preparation failed
            if (logger) {
                logger->error("Failed to prepare SQL statement: {}", sql);
            }
            // Clean up transaction
            tra->rollback(&st);
            tra->release();
            throw FirebirdException("Failed to prepare SQL statement");
        }

        // Execute the prepared statement
        try {
            stmt->execute(&st, tra, nullptr, nullptr, nullptr, nullptr);
            stmt->free(&st);
            stmt->release();
        }
        catch (...) {
            // Clean up on error
            if (stmt) {
                stmt->release();
            }
            throw;
        }

        // Return transaction for commit/rollback
        return std::make_shared<Transaction>(this, tra);
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

std::shared_ptr<Transaction> Connection::StartTransaction() {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    try {
        auto& st = status();

        Firebird::ITransaction* tra = attachment_->startTransaction(&st, 0, nullptr);
        if (!tra) {
            throw FirebirdException("Failed to start transaction");
        }

        return std::make_shared<Transaction>(this, tra);
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

void Connection::ExecuteInTransaction(Transaction* tra, const std::string& sql) {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    if (!tra || !tra->isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    try {
        auto& st = status();

        attachment_->execute(&st, tra->getTransaction(), 0, sql.c_str(), 3,
                             nullptr, nullptr, nullptr, nullptr);
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

bool Connection::isConnected() const {
    if (!attachment_) {
        return false;
    }

    try {
        auto& st = status();
        // Use ping() method to check if connection is alive (Firebird 3.0+)
        attachment_->ping(&st);
        return true;
    }
    catch (const Firebird::FbException&) {
        return false;
    }
    catch (...) {
        return false;
    }
}

void Connection::cancelOperation(CancelOperation option) {
    if (!attachment_) {
        throw FirebirdException("Cannot cancel operation: not connected");
    }

    auto logger = util::Logging::get();
    if (logger) {
        const char* optionName = nullptr;
        switch (option) {
            case CancelOperation::DISABLE:
                optionName = "DISABLE";
                break;
            case CancelOperation::ENABLE:
                optionName = "ENABLE";
                break;
            case CancelOperation::RAISE:
                optionName = "RAISE";
                break;
            case CancelOperation::ABORT:
                optionName = "ABORT";
                break;
        }
        logger->debug("Calling cancelOperation with option: {}", optionName);
    }

    try {
        auto& st = status();
        attachment_->cancelOperation(&st, static_cast<int>(option));

        if (logger) {
            logger->debug("cancelOperation completed successfully");
        }
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

void Connection::ExecuteDDL(const std::string& ddl) {
    auto tra = StartTransaction();
    ExecuteInTransaction(tra.get(), ddl);
    tra->Commit();
}

// Static methods for database management
void Connection::createDatabase(const std::string& database, const ConnectionParams& params) {
    ConnectionParams fullParams = params;
    fullParams.database = database;
    createDatabase(fullParams);
}

void Connection::createDatabase(const ConnectionParams& params) {
    auto& env = Environment::getInstance();

    // Temporary status owned by this function
    Firebird::IStatus* raw = env.getMaster()->getStatus();
    Firebird::ThrowStatusWrapper st(raw);

    try {
        // Build DPB
        Firebird::IXpbBuilder* dpb = env.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0);

        if (!params.user.empty()) {
            dpb->insertString(&st, isc_dpb_user_name, params.user.c_str());
        }

        if (!params.password.empty()) {
            dpb->insertString(&st, isc_dpb_password, params.password.c_str());
        }

        if (!params.charset.empty()) {
            dpb->insertString(&st, isc_dpb_set_db_charset, params.charset.c_str());
        }

        if (!params.role.empty()) {
            dpb->insertString(&st, isc_dpb_sql_role_name, params.role.c_str());
        }

        dpb->insertInt(&st, isc_dpb_page_size, 8192);

        // Create database
        Firebird::IAttachment* att = env.getProvider()->createDatabase(
            &st,
            params.database.c_str(),
            dpb->getBufferLength(&st),
            dpb->getBuffer(&st));

        dpb->dispose();

        if (att) {
            att->detach(&st);
            att->release();
        }
    }
    catch (const Firebird::FbException& e) {
        st.dispose();
        throw FirebirdException(e);
    }
    st.dispose();
}

void Connection::dropDatabase(const std::string& database, const ConnectionParams& params) {
    ConnectionParams fullParams = params;
    fullParams.database = database;
    dropDatabase(fullParams);
}

void Connection::dropDatabase(const ConnectionParams& params) {
    auto& env = Environment::getInstance();

    // First, check if database exists using CheckStatusWrapper (doesn't throw)
    Firebird::IStatus* raw_status = env.getMaster()->getStatus();
    Firebird::CheckStatusWrapper checkStatus(raw_status);

    // Build DPB
    Firebird::IXpbBuilder* dpb = env.getUtil()->getXpbBuilder(
        &checkStatus, Firebird::IXpbBuilder::DPB, nullptr, 0);

    if (!params.user.empty()) {
        dpb->insertString(&checkStatus, isc_dpb_user_name, params.user.c_str());
    }

    if (!params.password.empty()) {
        dpb->insertString(&checkStatus, isc_dpb_password, params.password.c_str());
    }

    // Try to attach to database
    Firebird::IAttachment* att = env.getProvider()->attachDatabase(
        &checkStatus,
        params.database.c_str(),
        dpb->getBufferLength(&checkStatus),
        dpb->getBuffer(&checkStatus));

    dpb->dispose();

    if (checkStatus.getState() & Firebird::IStatus::STATE_ERRORS) {
        const bool missing = fb_status::isDbMissingOnAttach(raw_status);

        // форматирование только для логов/исключения
        char buf[1024]{};
        env.getUtil()->formatStatus(buf, sizeof(buf), raw_status);

        raw_status->dispose();
        if (missing) return;            // базы нет — это «нормально» для drop
        throw FirebirdException(buf);   // другая ошибка → исключение
    }

    // нет ошибок: успешно attached
    raw_status->dispose();

    // дропаем
    if (att) {
        Firebird::IStatus* raw2 = env.getMaster()->getStatus();
        Firebird::ThrowStatusWrapper dropStatus(raw2);
        try {
            att->dropDatabase(&dropStatus);
        } catch (const Firebird::FbException& e) {
            dropStatus.dispose();
            throw FirebirdException(e);
        }
        att->release();
        dropStatus.dispose();
    }
}

bool Connection::databaseExists(const std::string& database, const ConnectionParams& params) {
    ConnectionParams fullParams = params;
    fullParams.database = database;

    auto& env = Environment::getInstance();

    // Temp status for this check
    Firebird::IStatus* raw = env.getMaster()->getStatus();
    Firebird::ThrowStatusWrapper st(raw);

    try {
        // Build DPB
        Firebird::IXpbBuilder* dpb = env.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0);

        if (!params.user.empty()) {
            dpb->insertString(&st, isc_dpb_user_name, params.user.c_str());
        }

        if (!params.password.empty()) {
            dpb->insertString(&st, isc_dpb_password, params.password.c_str());
        }

        // Try to attach
        Firebird::IAttachment* att = env.getProvider()->attachDatabase(
            &st,
            fullParams.database.c_str(),
            dpb->getBufferLength(&st),
            dpb->getBuffer(&st));

        dpb->dispose();

        if (att) {
            att->detach(&st);
            att->release();
            st.dispose();
            return true;
        }
        st.dispose();
        return false;
    }
    catch (const Firebird::FbException& e) {
        st.dispose();
        return false;
    }
}

// prepareStatement now uses cache by default for better performance
std::shared_ptr<Statement> Connection::prepareStatement(const std::string& sql, unsigned flags) {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    // Lazy initialization of cache
    if (!statementCache_) {
        auto logger = util::Logging::get();

        // Get cache configuration
        StatementCache::CacheConfig cacheConfig;
        cacheConfig.enabled = util::Config::cache().enabled;
        cacheConfig.maxSize = util::Config::cache().maxStatements;
        cacheConfig.ttlMinutes = util::Config::cache().ttlMinutes;

        if (logger) {
            logger->debug("Initializing statement cache - enabled: {}, maxSize: {}, ttl: {} minutes",
                          cacheConfig.enabled, cacheConfig.maxSize, cacheConfig.ttlMinutes);
        }

        statementCache_ = std::make_unique<StatementCache>(cacheConfig);
    }

    // Get or create cached statement
    return statementCache_->get(this, sql, flags);
}

Connection::QueryMetadataInfo Connection::describeQuery(const std::string& sql) {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    auto logger = util::Logging::get();
    auto statement = prepareStatement(sql, Statement::PREPARE_PREFETCH_ALL);
    if (!statement) {
        throw FirebirdException("Failed to prepare statement for metadata inspection");
    }

    QueryMetadataInfo info;

    if (auto inputMeta = statement->getInputMetadata()) {
        unsigned count = inputMeta->getCount();
        info.inputFields.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            info.inputFields.emplace_back(inputMeta->getField(i));
        }
    }

    if (auto outputMeta = statement->getOutputMetadata()) {
        unsigned count = outputMeta->getCount();
        info.outputFields.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            info.outputFields.emplace_back(outputMeta->getField(i));
        }
    }

    if (logger) {
        logger->trace("Describe query '{}' -> in: {}, out: {}",
                      sql.substr(0, 100),
                      info.inputFields.size(),
                      info.outputFields.size());
    }

    return info;
}

void Connection::clearStatementCache() {
    if (statementCache_) {
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Clearing statement cache");
        }
        statementCache_->clear();
    }
}

StatementCache::Statistics Connection::getCacheStatistics() const {
    if (statementCache_) {
        return statementCache_->getStatistics();
    }

    // Return empty statistics if cache not initialized
    return StatementCache::Statistics{};
}

} // namespace core
} // namespace fbpp
