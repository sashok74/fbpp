#pragma once

#include "fbpp/core/firebird_compat.hpp"
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

/**
 * Stable snapshot entry from a Firebird status vector.
 */
struct FirebirdStatusEntry {
    enum class PayloadKind {
        none,
        integer,
        text
    };

    intptr_t tag = 0;
    PayloadKind payloadKind = PayloadKind::none;
    intptr_t numericValue = 0;
    std::string textValue;
};

/**
 * Exception wrapper that normalizes Firebird diagnostic data for consumers.
 *
 * Stable error contract:
 * - `what()` is a human-readable message formatted from Firebird status data.
 * - `getErrorCode()` returns the first Firebird GDS code when present.
 * - `getSQLState()` returns a 5-character SQLSTATE or `HY000` fallback.
 * - `getSQLCode()` returns the Firebird SQLCODE derived from the original
 *   status vector when available.
 * - `getErrorMessages()` returns parsed per-entry messages.
 * - `getStatusVector()` returns a copied snapshot of the original Firebird
 *   status vector payloads in a stable C++ representation.
 */
class FirebirdException : public std::exception {
public:
    explicit FirebirdException(std::string message);
    explicit FirebirdException(const Firebird::FbException& fb_ex);

    const char* what() const noexcept override { return message_.c_str(); }

    int getErrorCode() const noexcept { return error_code_; }
    const std::string& getSQLState() const noexcept { return sql_state_; }
    int getSQLCode() const noexcept { return sql_code_; }
    const std::vector<std::string>& getErrorMessages() const noexcept { return error_messages_; }
    const std::vector<FirebirdStatusEntry>& getStatusVector() const noexcept {
        return status_vector_;
    }

private:
    std::string message_;
    int         error_code_ {0};
    std::string sql_state_;
    int         sql_code_ {0};
    std::vector<std::string> error_messages_;
    std::vector<FirebirdStatusEntry> status_vector_;

    void extractErrorDetails(Firebird::IStatus* status);
};

} // namespace core
} // namespace fbpp
