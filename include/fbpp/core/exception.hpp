#pragma once

#include "fbpp/core/firebird_compat.hpp"
#include <exception>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

class FirebirdException : public std::exception {
public:
    explicit FirebirdException(std::string message);
    explicit FirebirdException(const Firebird::FbException& fb_ex);

    const char* what() const noexcept override { return message_.c_str(); }

    int getErrorCode() const noexcept { return error_code_; }
    const std::string& getSQLState() const noexcept { return sql_state_; }
    int getSQLCode() const noexcept { return sql_code_; }
    const std::vector<std::string>& getErrorMessages() const noexcept { return error_messages_; }

private:
    std::string message_;
    int         error_code_ {0};
    std::string sql_state_;
    int         sql_code_ {0};
    std::vector<std::string> error_messages_;

    void extractErrorDetails(Firebird::IStatus* status);
};

} // namespace core
} // namespace fbpp
