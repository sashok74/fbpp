#pragma once

#include <firebird/Interface.h>
#include <stdexcept>

namespace fbpp {
namespace core {

/**
 * Environment singleton - manages global Firebird interfaces (master, provider, util)
 */
class Environment {
public:
    static Environment& getInstance() {
        static Environment instance;
        return instance;
    }

    Firebird::IMaster*   getMaster()   const { return master_;   }
    Firebird::IProvider* getProvider() const { return provider_; }
    Firebird::IUtil*     getUtil()     const { return util_;     }

    Environment(const Environment&)            = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&)                 = delete;
    Environment& operator=(Environment&&)      = delete;

private:
    Environment()
        : master_(Firebird::fb_get_master_interface())
        , provider_(nullptr)
        , util_(nullptr)
    {
        if (!master_)
            throw std::runtime_error("Failed to get Firebird master interface");
        provider_ = master_->getDispatcher();
        if (!provider_)
            throw std::runtime_error("Failed to get Firebird provider interface");
        util_ = master_->getUtilInterface();
        if (!util_)
            throw std::runtime_error("Failed to get Firebird util interface");
    }

    ~Environment() = default; // Interfaces are managed by Firebird

    Firebird::IMaster*   master_;
    Firebird::IProvider* provider_;
    Firebird::IUtil*     util_;
};

} // namespace core
} // namespace fbpp
