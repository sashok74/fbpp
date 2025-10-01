#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include <firebird/Interface.h>
#include <sstream>
#include <iomanip>

namespace fbpp::core {

// ============================================================================
// DecFloat16 Implementation
// ============================================================================

DecFloat16::DecFloat16(double value) {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat16(&status);

        // Convert double to string first, then parse - more reliable
        std::ostringstream oss;
        oss << std::setprecision(17) << value;
        std::string str = oss.str();

        helper->fromString(&status, str.c_str(), reinterpret_cast<FB_DEC16*>(data_.data()));
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

DecFloat16::DecFloat16(const std::string& str) {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat16(&status);

        helper->fromString(&status, str.c_str(), reinterpret_cast<FB_DEC16*>(data_.data()));
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

DecFloat16::DecFloat16(const char* str) : DecFloat16(std::string(str)) {
}

std::string DecFloat16::to_string() const {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat16(&status);

        char buffer[50]; // Safe buffer size
        helper->toString(&status, reinterpret_cast<const FB_DEC16*>(data_.data()), sizeof(buffer), buffer);

        return std::string(buffer);
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

// ============================================================================
// DecFloat34 Implementation
// ============================================================================

DecFloat34::DecFloat34(double value) {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat34(&status);

        // Convert double to string first, then parse - more reliable
        std::ostringstream oss;
        oss << std::setprecision(17) << value;
        std::string str = oss.str();

        helper->fromString(&status, str.c_str(), reinterpret_cast<FB_DEC34*>(data_.data()));
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

DecFloat34::DecFloat34(const std::string& str) {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat34(&status);

        helper->fromString(&status, str.c_str(), reinterpret_cast<FB_DEC34*>(data_.data()));
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

DecFloat34::DecFloat34(const char* str) : DecFloat34(std::string(str)) {
}

std::string DecFloat34::to_string() const {
    try {
        auto& env = Environment::getInstance();
        auto util = env.getUtil();
        auto master = env.getMaster();

        Firebird::ThrowStatusWrapper status(master->getStatus());
        auto helper = util->getDecFloat34(&status);

        char buffer[50]; // Safe buffer size
        helper->toString(&status, reinterpret_cast<const FB_DEC34*>(data_.data()), sizeof(buffer), buffer);

        return std::string(buffer);
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

} // namespace fbpp::core