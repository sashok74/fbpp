#pragma once

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace util {

class Logging {
public:
    static void init(const std::string& level = "info",
                     bool console = true,
                     bool file = false,
                     const std::string& filePath = "logs/binding_lab.log",
                     size_t maxSizeMb = 5,
                     size_t maxFiles = 3);
    
    static std::shared_ptr<spdlog::logger> get();
    
    // Shutdown logging (useful for tests)
    static void shutdown();
    
private:
    static std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace util