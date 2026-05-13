#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/named_param_parser.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/status_utils.hpp"
#include "fbpp/core/detail/firebird_raii.hpp"
#include "fbpp_util/trace.h"
#include <stdexcept>
#include <tuple>

namespace fbpp {
namespace core {

Connection::Connection(const std::string& database)
    : env_(Environment::getInstance())
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_) {
    ConnectionParams params;
    params.database = database;
    connect(params);
}

Connection::Connection(const ConnectionParams& params)
    : env_(Environment::getInstance())
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_)
    , options_(params.options) {
    connect(params);
}

Connection::~Connection() {
    disconnect();
    // Dispose our owned IStatus
    statusWrapper_.dispose();
}

void Connection::connect(const ConnectionParams& params) {
    fbpp::util::trace(fbpp::util::TraceLevel::info, "Connection",
                [&](auto& oss) { oss << "Connecting to " << params.database; });
    try {
        auto& st = status();

        // Build DPB (RAII: disposed automatically on scope exit, including throw paths)
        detail::XpbBuilderGuard dpb(env_.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0));

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

        // Set SQL dialect (1, 2, or 3)
        if (params.sql_dialect > 0) {
            dpb->insertInt(&st, isc_dpb_sql_dialect, params.sql_dialect);
        }

        // Attach to database
        attachment_ = env_.getProvider()->attachDatabase(
            &st,
            params.database.c_str(),
            dpb->getBufferLength(&st),
            dpb->getBuffer(&st));

        if (!attachment_) {
            fbpp::util::trace(fbpp::util::TraceLevel::error, "Connection",
                        [&](auto& oss) { oss << "Failed to attach to " << params.database; });
            throw FirebirdException("Failed to attach to database: " + params.database);
        }

        fbpp::util::trace(fbpp::util::TraceLevel::info, "Connection",
                    [&](auto& oss) { oss << "Connected to " << params.database; });
    }
    catch (const Firebird::FbException& e) {
        fbpp::util::trace(fbpp::util::TraceLevel::error, "Connection",
                    [&](auto& oss) {
                        oss << "Connection to " << params.database << " failed (Firebird exception)";
                    });
        throw FirebirdException(e);
    }
}

void Connection::disconnect() {
    if (attachment_) {
        try {
            auto& st = status();
            attachment_->detach(&st);
            attachment_->release();
            fbpp::util::trace(fbpp::util::TraceLevel::info, "Connection",
                        [](auto& oss) { oss << "Disconnected from database"; });
        }
        catch (...) {
            fbpp::util::trace(fbpp::util::TraceLevel::warn, "Connection",
                        [](auto& oss) { oss << "Error while disconnecting (ignored)"; });
            // Ignore errors during disconnect
        }
        attachment_ = nullptr;
    }
}

std::shared_ptr<Transaction> Connection::Execute(const std::string& sql) {
    if (!attachment_) {
        fbpp::util::trace(fbpp::util::TraceLevel::error, "Connection",
                    [&](auto& oss) {
                        oss << "Cannot execute SQL (not connected): "
                            << sql.substr(0, 100);
                    });
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
            fbpp::util::trace(fbpp::util::TraceLevel::error, "Connection",
                        [&](auto& oss) {
                            oss << "Failed to prepare SQL statement: "
                                << sql.substr(0, 100);
                        });
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
    fbpp::util::trace(fbpp::util::TraceLevel::info, "Connection",
                [&](auto& oss) {
                    oss << "cancelOperation(" << (optionName ? optionName : "UNKNOWN") << ")";
                });

    try {
        auto& st = status();
        attachment_->cancelOperation(&st, static_cast<int>(option));
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
        // Build DPB (RAII: disposed on scope exit, including throw paths)
        detail::XpbBuilderGuard dpb(env.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0));

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

        // Set SQL dialect (1, 2, or 3)
        if (params.sql_dialect > 0) {
            dpb->insertInt(&st, isc_dpb_sql_dialect, params.sql_dialect);
        }

        dpb->insertInt(&st, isc_dpb_page_size, 8192);

        // Create database
        Firebird::IAttachment* att = env.getProvider()->createDatabase(
            &st,
            params.database.c_str(),
            dpb->getBufferLength(&st),
            dpb->getBuffer(&st));

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

    // Build DPB (RAII: disposed on scope exit)
    detail::XpbBuilderGuard dpb(env.getUtil()->getXpbBuilder(
        &checkStatus, Firebird::IXpbBuilder::DPB, nullptr, 0));

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
        // Build DPB (RAII: disposed on scope exit, including throw paths)
        detail::XpbBuilderGuard dpb(env.getUtil()->getXpbBuilder(
            &st, Firebird::IXpbBuilder::DPB, nullptr, 0));

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
        statementCache_ = std::make_unique<StatementCache>(options_.statementCache);
    }

    // Get or create cached statement
    return statementCache_->get(this, sql, flags);
}

std::shared_ptr<Statement> Connection::prepareStatementUncached(
    const std::string& sql, unsigned flags) {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    auto parseResult = NamedParamParser::parse(sql);
    const std::string& actualSql =
        parseResult.hasNamedParams ? parseResult.convertedSql : sql;

    auto tra = StartTransaction();
    Firebird::IStatement* rawStmt = nullptr;
    try {
        rawStmt = attachment_->prepare(
            &status(),
            tra->getTransaction(),
            0,
            actualSql.c_str(),
            3,
            flags);
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
    tra->Commit();

    if (!rawStmt) {
        throw FirebirdException("Failed to prepare statement");
    }

    auto stmt = std::make_shared<Statement>(rawStmt, this);
    if (parseResult.hasNamedParams) {
        stmt->setNamedParamMapping(parseResult.nameToPositions, true);
    }
    return stmt;
}

Connection::QueryMetadataInfo Connection::describeQuery(const std::string& sql) {
    QueryMetadataInfo info;

    // Single raw-prepare path: prepareStatementUncached. Statement's
    // destructor releases the underlying IStatement when the local
    // shared_ptr goes out of scope.
    auto stmt = prepareStatementUncached(sql);

    auto inMeta = stmt->getInputMetadata();
    if (inMeta) {
        const unsigned n = inMeta->getCount();
        info.inputFields.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            info.inputFields.emplace_back(inMeta->getField(i));
        }
    }

    auto outMeta = stmt->getOutputMetadata();
    if (outMeta) {
        const unsigned n = outMeta->getCount();
        info.outputFields.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            info.outputFields.emplace_back(outMeta->getField(i));
        }
    }

    return info;
}

// ---------------------------------------------------------------------------
// Engine version probe
// ---------------------------------------------------------------------------

int Connection::getEngineMajorVersion() const {
    if (engineMajor_ != 0) {
        return engineMajor_;
    }
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    // Self-call: this method is logically const but populates the cache.
    // The cast keeps the public API const without dragging mutable into
    // the connection-machinery side of things.
    auto* self = const_cast<Connection*>(this);
    auto stmt = self->prepareStatementUncached(
        "SELECT RDB$GET_CONTEXT('SYSTEM', 'ENGINE_VERSION') "
        "FROM RDB$DATABASE");
    auto tx = self->StartTransaction();
    auto cur = tx->openCursor(stmt);
    std::tuple<std::string> row;
    int major = 0;
    if (cur->fetch(row)) {
        const auto& ver = std::get<0>(row);
        // ENGINE_VERSION format is "M.m.r.b" (e.g., "5.0.0.1306"). Take
        // the integer prefix before the first '.'.
        try {
            major = std::stoi(ver);
        } catch (...) {
            major = 0;
        }
    }
    tx->Commit();

    engineMajor_ = major;
    return engineMajor_;
}

// ---------------------------------------------------------------------------
// Procedure metadata
// ---------------------------------------------------------------------------
//
// Two-step strategy:
//   listProcedures()       reads RDB$PROCEDURES alone (cheap, one cursor).
//   describeProcedure()    additionally reads RDB$PROCEDURE_PARAMETERS for
//                          parameter names + directions, then renders a
//                          probe SQL ("EXECUTE PROCEDURE ..." or
//                          "SELECT * FROM ...") and feeds it through
//                          prepareStatementUncached so FieldInfo for each
//                          parameter comes from the same machinery as
//                          describeQuery.
namespace {

std::string trimAscii(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

std::string renderProbeSql(const ProcedureInfo& info, unsigned inputCount) {
    // Quote identifiers so case-sensitive procedure names round-trip.
    auto quote = [](const std::string& id) {
        return "\"" + id + "\"";
    };
    std::string qualified;
    if (!info.packageName.empty()) {
        qualified = quote(info.packageName) + "." + quote(info.name);
    } else {
        qualified = quote(info.name);
    }

    std::string args;
    if (inputCount > 0) {
        args = "(";
        for (unsigned i = 0; i < inputCount; ++i) {
            if (i > 0) args += ',';
            args += '?';
        }
        args += ")";
    }

    if (info.kind == ProcedureKind::Selectable) {
        return "SELECT * FROM " + qualified + args;
    }
    return "EXECUTE PROCEDURE " + qualified + args;
}

} // namespace

std::vector<ProcedureInfo> Connection::listProcedures() {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    // FB 3+ has RDB$PACKAGE_NAME; on FB 2.5 the column is absent. We
    // assume FB 3+ here (CLAUDE.md targets FB 5).
    const std::string sql =
        "SELECT TRIM(P.RDB$PROCEDURE_NAME),"
        "       COALESCE(TRIM(P.RDB$PACKAGE_NAME), ''),"
        "       COALESCE(P.RDB$PROCEDURE_TYPE, 0) "
        "FROM RDB$PROCEDURES P "
        "WHERE COALESCE(P.RDB$SYSTEM_FLAG, 0) = 0 "
        "ORDER BY P.RDB$PACKAGE_NAME, P.RDB$PROCEDURE_NAME";

    auto stmt = prepareStatementUncached(sql);
    auto tx = StartTransaction();
    auto cur = tx->openCursor(stmt);

    std::vector<ProcedureInfo> out;
    std::tuple<std::string, std::string, int16_t> row;
    while (cur->fetch(row)) {
        ProcedureInfo info;
        info.name        = trimAscii(std::get<0>(row));
        info.packageName = trimAscii(std::get<1>(row));
        switch (std::get<2>(row)) {
            case 1: info.kind = ProcedureKind::Selectable;  break;
            case 2: info.kind = ProcedureKind::Executable;  break;
            default: info.kind = ProcedureKind::Unknown;    break;
        }
        out.push_back(std::move(info));
    }
    tx->Commit();
    return out;
}

ProcedureInfo Connection::describeProcedure(const std::string& name,
                                            const std::string& packageName) {
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }

    // Fetch header row first.
    const std::string headerSql =
        "SELECT COALESCE(P.RDB$PROCEDURE_TYPE, 0),"
        "       COALESCE(P.RDB$PROCEDURE_INPUTS, 0),"
        "       COALESCE(P.RDB$PROCEDURE_OUTPUTS, 0) "
        "FROM RDB$PROCEDURES P "
        "WHERE P.RDB$PROCEDURE_NAME = ? "
        "  AND COALESCE(P.RDB$PACKAGE_NAME, '') = ?";

    auto headerStmt = prepareStatementUncached(headerSql);
    auto tx = StartTransaction();

    int16_t procType = 0;
    int16_t inputCount = 0;
    int16_t outputCount = 0;
    {
        auto cur = tx->openCursor(headerStmt,
            std::make_tuple(name, packageName));
        std::tuple<int16_t, int16_t, int16_t> hdr;
        if (!cur->fetch(hdr)) {
            tx->Commit();
            throw FirebirdException("Procedure not found: " +
                (packageName.empty() ? name : packageName + "." + name));
        }
        procType    = std::get<0>(hdr);
        inputCount  = std::get<1>(hdr);
        outputCount = std::get<2>(hdr);
    }

    ProcedureInfo info;
    info.name        = name;
    info.packageName = packageName;
    switch (procType) {
        case 1: info.kind = ProcedureKind::Selectable;  break;
        case 2: info.kind = ProcedureKind::Executable;  break;
        default: info.kind = ProcedureKind::Unknown;    break;
    }

    // Fetch parameter NAMES + DIRECTIONS from RDB$PROCEDURE_PARAMETERS.
    // RDB$PARAMETER_TYPE: 0=input, 1=output.
    // RDB$PARAMETER_NUMBER: position within direction.
    const std::string paramsSql =
        "SELECT TRIM(PP.RDB$PARAMETER_NAME),"
        "       PP.RDB$PARAMETER_TYPE,"
        "       PP.RDB$PARAMETER_NUMBER "
        "FROM RDB$PROCEDURE_PARAMETERS PP "
        "WHERE PP.RDB$PROCEDURE_NAME = ? "
        "  AND COALESCE(PP.RDB$PACKAGE_NAME, '') = ? "
        "ORDER BY PP.RDB$PARAMETER_TYPE, PP.RDB$PARAMETER_NUMBER";

    std::vector<std::string> inputNames(inputCount);
    std::vector<std::string> outputNames(outputCount);
    {
        auto pStmt = prepareStatementUncached(paramsSql);
        auto cur = tx->openCursor(pStmt,
            std::make_tuple(name, packageName));
        std::tuple<std::string, int16_t, int16_t> row;
        while (cur->fetch(row)) {
            const std::string pname = trimAscii(std::get<0>(row));
            const int16_t dir = std::get<1>(row);
            const int16_t pos = std::get<2>(row);
            if (dir == 0 && pos < inputCount)  inputNames [pos] = pname;
            if (dir == 1 && pos < outputCount) outputNames[pos] = pname;
        }
    }
    tx->Commit();

    // Probe prepare to get FieldInfo per parameter — same machinery as
    // describeQuery, so type/scale/length match what a real call would see.
    // Strict-fail by default: if the probe cannot be built (permissions,
    // FB version quirk, malformed SQL render) we let the FirebirdException
    // propagate. A "best-effort" mode without per-param FieldInfo would
    // make Phase 0 manifest CI silently green on incomplete metadata —
    // unsafe. Callers that genuinely want soft-mode should catch
    // FirebirdException themselves and fall back to listProcedures().
    QueryMetadataInfo probe = describeQuery(
        renderProbeSql(info, static_cast<unsigned>(inputCount)));

    info.params.reserve(probe.inputFields.size() + probe.outputFields.size());
    for (size_t i = 0; i < probe.inputFields.size(); ++i) {
        ProcedureParamInfo p;
        p.procedureName = name;
        p.packageName   = packageName;
        p.name          = (i < inputNames.size()) ? inputNames[i] : "";
        p.direction     = ParamDirection::Input;
        p.field         = probe.inputFields[i];
        p.position      = static_cast<unsigned>(i);
        info.params.push_back(std::move(p));
    }
    for (size_t i = 0; i < probe.outputFields.size(); ++i) {
        ProcedureParamInfo p;
        p.procedureName = name;
        p.packageName   = packageName;
        p.name          = (i < outputNames.size()) ? outputNames[i] : "";
        p.direction     = ParamDirection::Output;
        p.field         = probe.outputFields[i];
        p.position      = static_cast<unsigned>(i);
        info.params.push_back(std::move(p));
    }
    return info;
}

void Connection::clearStatementCache() {
    if (statementCache_) {
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

void Connection::setOptions(const ConnectionOptions& options) {
    options_ = options;
    if (statementCache_) {
        statementCache_->setEnabled(options_.statementCache.enabled);
        statementCache_->setMaxSize(options_.statementCache.maxSize);
        statementCache_->setTtlMinutes(options_.statementCache.ttlMinutes);
    }
}

void Connection::setStatementCacheConfig(const StatementCacheConfig& config) {
    ConnectionOptions updated = options_;
    updated.statementCache = config;
    setOptions(updated);
}

} // namespace core
} // namespace fbpp
