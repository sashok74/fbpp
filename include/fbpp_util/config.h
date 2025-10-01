#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace util {

class Config {
public:
    struct DbConfig {
        std::string server = "firebird5";
        std::string path = "/mnt/test/binding_test.fdb";
        std::string user = "SYSDBA";
        std::string password = "planomer";
        std::string charset = "UTF8";
        bool createIfMissing = true;
        bool dropOnCleanup = true;
    };
    
    struct LoggingConfig {
        std::string level = "info";
        bool console = true;
        bool file = true;
        std::string filePath = "logs/binding_lab.log";
        size_t rotateMaxSizeMb = 5;
        size_t rotateMaxFiles = 3;
    };
    
    struct TestsConfig {
        bool skipCreateSchema = false;
        std::string logLevel = "info";
        std::string tempDbPath = "/mnt/test/binding_test_temp.fdb";
    };

    struct CacheConfig {
        bool enabled = true;
        size_t maxStatements = 100;      // Maximum number of cached statements
        size_t ttlMinutes = 60;          // Time-to-live for unused statements
    };

    static bool load(const std::string& jsonPath);
    static DbConfig& db() { return instance().db_; }
    static LoggingConfig& logging() { return instance().logging_; }
    static TestsConfig& tests() { return instance().tests_; }
    static CacheConfig& cache() { return instance().cache_; }
    
private:
    Config() = default;
    static Config& instance();
    
    void loadFromEnv();
    void loadFromJson(const nlohmann::json& j);
    
    DbConfig db_;
    LoggingConfig logging_;
    TestsConfig tests_;
    CacheConfig cache_;
};

}  // namespace util
