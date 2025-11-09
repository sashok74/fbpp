#pragma once

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>  // for std::getenv()

#if defined(_WIN32)
#include <process.h>  // for _getpid()
#else
#include <unistd.h>   // for getpid()
#endif
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/exception.hpp"

namespace fbpp {
namespace test {

using json = nlohmann::json;
using namespace fbpp::core;

// Base class for all fbpp tests
class FbppTestBase : public ::testing::Test {
protected:
    static json loadTestConfig() {
        // Try multiple paths to find the config file
        std::vector<std::string> paths = {
            "../../config/test_config.json",  // From build/tests when running via ctest
            "../config/test_config.json",      // From build when running directly
            "config/test_config.json",         // From project root
            "./test_config.json"               // Current directory
        };
        
        std::ifstream config_file;
        for (const auto& path : paths) {
            config_file.open(path);
            if (config_file.is_open()) {
                break;
            }
        }
        
        if (!config_file.is_open()) {
            throw std::runtime_error("Cannot open test_config.json - tried multiple paths");
        }
        json config;
        config_file >> config;
        return config;
    }
    
    static ConnectionParams getConnectionParams(const json& db_config) {
        ConnectionParams params;

        // Read defaults from JSON config
        std::string server = db_config["server"].get<std::string>();
        std::string path = db_config["path"].get<std::string>();
        std::string user = db_config["user"].get<std::string>();
        std::string password = db_config["password"].get<std::string>();
        std::string charset = db_config["charset"].get<std::string>();

        // Override with environment variables if set (for CI/CD)
        // This follows 12-factor app methodology: ENV vars > config files
        if (const char* env_host = std::getenv("FIREBIRD_HOST")) {
            server = env_host;
        }
        if (const char* env_port = std::getenv("FIREBIRD_PORT")) {
            // If port is explicitly provided, append it to server
            server = server + "/" + env_port;
        }

        // Handle both temp and persistent DB paths
        // If path is relative (like "testdb"), check for persistent DB path first
        bool is_relative_path = (path.find('/') == std::string::npos);
        if (is_relative_path) {
            // For relative paths, prefer FIREBIRD_PERSISTENT_DB_PATH
            if (const char* env_persistent_path = std::getenv("FIREBIRD_PERSISTENT_DB_PATH")) {
                path = env_persistent_path;
            }
        } else {
            // For absolute paths, use FIREBIRD_DB_PATH (temp DB)
            if (const char* env_path = std::getenv("FIREBIRD_DB_PATH")) {
                path = env_path;
            }
        }

        if (const char* env_user = std::getenv("FIREBIRD_USER")) {
            user = env_user;
        }
        if (const char* env_pass = std::getenv("FIREBIRD_PASSWORD")) {
            password = env_pass;
        }
        if (const char* env_charset = std::getenv("FIREBIRD_CHARSET")) {
            charset = env_charset;
        }

        // Build connection params
        params.database = server + ":" + path;
        params.user = user;
        params.password = password;
        params.charset = charset;

        return params;
    }
    
    static void initLoggingFromConfig(const json&) {
        // Logging subsystem removed; trace observer can be configured separately.
    }
};

// Test with persistent database (created once, reused across tests)
class PersistentDatabaseTest : public FbppTestBase {
protected:
    static void SetUpTestSuite() {
        auto config = loadTestConfig();
        initLoggingFromConfig(config);  // Initialize logging from config
        auto db_config = config["tests"]["persistent_db"];
        db_params_ = getConnectionParams(db_config);
        
        // Create database once if it doesn't exist
        if (!Connection::databaseExists(db_params_.database, db_params_)) {
            Connection::createDatabase(db_params_);
            
            // Create initial schema
            auto conn = std::make_unique<Connection>(db_params_);
            conn->ExecuteDDL(
                "CREATE TABLE test_data ("
                "    id INTEGER NOT NULL PRIMARY KEY,"
                "    name VARCHAR(100),"
                "    amount DOUBLE PRECISION,"
                "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
            
            conn->ExecuteDDL(
                "CREATE TABLE test_log ("
                "    id INTEGER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
                "    message VARCHAR(500),"
                "    logged_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
        }
    }
    
    static void TearDownTestSuite() {
        // Persistent database is not dropped
    }
    
    void SetUp() override {
        connection_ = std::make_unique<Connection>(db_params_);
    }
    
    void TearDown() override {
        connection_.reset();
    }
    
    std::unique_ptr<Connection> connection_;
    static ConnectionParams db_params_;
};

// Test with temporary database (recreated for each test)
class TempDatabaseTest : public FbppTestBase {
protected:
    void SetUp() override {
        auto config = loadTestConfig();
        initLoggingFromConfig(config);  // Initialize logging from config
        auto db_config = config["tests"]["temp_db"];
        db_params_ = getConnectionParams(db_config);
        
        // Add unique suffix to avoid conflicts
        
        auto pos = db_params_.database.rfind(".fdb");
        if (pos != std::string::npos) {
            db_params_.database.insert(pos, "_" + std::to_string(getCurrentProcessId()) + 
                                           "_" + std::to_string(test_counter_++));
        }
        
        // Drop if exists and create new
        Connection::dropDatabase(db_params_);
        Connection::createDatabase(db_params_);
        
        connection_ = std::make_unique<Connection>(db_params_);
        
        // Create test schema
        createTestSchema();
    }
    
    void TearDown() override {
        connection_.reset();
        
        // Drop temporary database
        try {
            Connection::dropDatabase(db_params_);
        } catch (...) {
            // Ignore errors during cleanup
        }
    }
    
    virtual void createTestSchema() {
        connection_->ExecuteDDL(
            "CREATE TABLE test_table ("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    name VARCHAR(100),"
            "    amount DOUBLE PRECISION"
            ")");
    }
    
    static long getCurrentProcessId() {
#if defined(_WIN32)
        return static_cast<long>(_getpid());
#else
        return static_cast<long>(getpid());
#endif
    }

    std::unique_ptr<Connection> connection_;
    ConnectionParams db_params_;
    static int test_counter_;
};


} // namespace test
} // namespace fbpp
