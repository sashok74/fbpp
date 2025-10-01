#include "fbpp/core/exception.hpp"
#include "fbpp/core/environment.hpp"
#include <firebird/iberror.h>
#include <sstream>
#include <cstring>
#include <unordered_map>

namespace fbpp {
namespace core {

namespace {
    // Map Firebird error codes to SQL state codes
    const std::unordered_map<int, std::string> ERROR_TO_SQLSTATE = {
        // Data exception
        {335544321, "22012"},  // isc_arith_except - division by zero

        // Integrity constraint violation
        {335544347, "23000"},  // isc_integ_constraint - integrity constraint violation
        {335544665, "23000"},  // isc_unique_key_violation - unique key violation
        {335544558, "23000"},  // isc_check_constraint - check constraint violation
        {335544466, "23000"},  // isc_foreign_key - foreign key violation
        {335544838, "23000"},  // isc_foreign_key_target_doesnt_exist
        {335544839, "23000"},  // isc_foreign_key_references_present

        // Transaction rollback
        {335544336, "40001"},  // isc_deadlock - deadlock
        {335544345, "40001"},  // isc_lock_conflict - lock conflict on no wait transaction
        {335544510, "40001"},  // isc_lock_timeout - lock time-out on wait transaction
        {335544856, "40001"},  // isc_update_conflict - update conflicts with concurrent update

        // Invalid cursor state
        {335544332, "24000"},  // isc_stream_eof - no current record for fetch operation

        // Syntax error or access rule violation
        {335544343, "42000"},  // isc_dsql_token_unk_err - token unknown
        {335544569, "42000"},  // isc_dsql_command_err - invalid command

        // Access violation
        {335544352, "28000"},  // isc_login - invalid user name or password
        {335544353, "28000"},  // isc_no_priv - no permission for operation

        // Connection exception
        {335544324, "08001"},  // isc_unavailable - database unavailable
        {335544375, "08001"},  // isc_io_error - I/O error
        {335544344, "08003"},  // isc_bad_db_handle - invalid database handle
        {335544327, "08004"},  // isc_shutinprog - database shutdown in progress

        // Invalid transaction state
        {335544337, "25000"}   // isc_tra_state - invalid transaction handle (expecting explicit transaction start)
    };

    std::string mapErrorCodeToSQLState(int error_code) {
        auto it = ERROR_TO_SQLSTATE.find(error_code);
        if (it != ERROR_TO_SQLSTATE.end()) {
            return it->second;
        }
        return "HY000";  // General error
    }
}

FirebirdException::FirebirdException(std::string message)
    : message_(std::move(message))
    , error_code_(0)
    , sql_code_(0) {
    if (sql_state_.empty()) sql_state_ = "HY000";
}

FirebirdException::FirebirdException(const Firebird::FbException& fb_ex)
    : error_code_(0)
    , sql_code_(0) {

    Firebird::IStatus* status = fb_ex.getStatus();
    extractErrorDetails(status);

    auto& env = Environment::getInstance();
    char buffer[4096] = {0};
    env.getUtil()->formatStatus(buffer, sizeof(buffer), status);
    message_ = buffer;

    if (error_messages_.size() > 1) {
        std::stringstream ss;
        ss << message_ << "\\nError chain:";
        for (size_t i = 0; i < error_messages_.size(); ++i) {
            ss << "\\n  [" << i << "] " << error_messages_[i];
        }
        message_ = ss.str();
    }
    if (sql_state_.empty()) sql_state_ = "HY000";
}

void FirebirdException::extractErrorDetails(Firebird::IStatus* status) {
    if (!status) return;
    const intptr_t* errors = status->getErrors();
    if (!errors) return;

    size_t i = 0;
    while (errors[i] != isc_arg_end) {
        const intptr_t tag = errors[i++];
        switch (tag) {
            case isc_arg_gds: {
                const intptr_t code = errors[i++];
                if (error_code_ == 0) {
                    error_code_ = static_cast<int>(code);
                    // Map error code to SQL state if not already set
                    if (sql_state_.empty()) {
                        sql_state_ = mapErrorCodeToSQLState(error_code_);
                    }
                }

                auto& env = Environment::getInstance();
                char msg[1024] = {0};

                Firebird::IStatus* tmp = env.getMaster()->getStatus();
                const intptr_t temp_vec[] = { isc_arg_gds, code, isc_arg_end };
                tmp->setErrors(temp_vec);
                env.getUtil()->formatStatus(msg, sizeof(msg), tmp);
                error_messages_.emplace_back(msg);
                tmp->dispose();
                break;
            }
            case isc_arg_string: {
                const char* s = reinterpret_cast<const char*>(errors[i++]);
                if (s) {
                    if (!error_messages_.empty()) {
                        error_messages_.back() += " - ";
                        error_messages_.back() += s;
                    } else {
                        error_messages_.emplace_back(s);
                    }
                }
                break;
            }
            case isc_arg_number: {
                const intptr_t n = errors[i++];
                if (!error_messages_.empty()) {
                    error_messages_.back() += " ";
                    error_messages_.back() += std::to_string(n);
                }
                break;
            }
            case isc_arg_sql_state: {
                const char* st = reinterpret_cast<const char*>(errors[i++]);
                if (st && sql_state_.empty()) sql_state_ = st;
                break;
            }
            case isc_arg_interpreted: {
                const char* s = reinterpret_cast<const char*>(errors[i++]);
                if (s) error_messages_.emplace_back(s);
                break;
            }
            case isc_arg_warning: {
                const intptr_t w = errors[i++];
                std::stringstream ss;
                ss << "Warning: " << w;
                error_messages_.emplace_back(ss.str());
                break;
            }
            default:
                ++i; // skip payload conservatively
                break;
        }
    }
    sql_code_ = 0;

    // If we still don't have a SQL state, set default
    if (sql_state_.empty()) {
        sql_state_ = "HY000";
    }
}

} // namespace core
} // namespace fbpp
