#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "fbpp/core/connection.hpp"

namespace fbpp {
namespace util {

using json = nlohmann::json;
using namespace fbpp::core;

/**
 * @brief Loads test_config.json from multiple possible paths
 *
 * Searches for test_config.json in the following order:
 * 1. ../../config/test_config.json (from build/examples or build/tests)
 * 2. ../config/test_config.json (from build)
 * 3. config/test_config.json (from project root)
 * 4. ./test_config.json (current directory)
 *
 * @return Parsed JSON configuration
 * @throws std::runtime_error if config file is not found
 */
inline json loadConfig() {
    std::vector<std::string> paths = {
        "../../config/test_config.json",  // From build/examples or build/tests
        "../config/test_config.json",      // From build
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

/**
 * @brief Gets database connection parameters with environment variable overrides
 *
 * This function implements the 12-factor app methodology:
 * Environment variables have priority over config file values.
 *
 * Supported sections:
 * - "db" - main database configuration (used by examples)
 * - "tests.persistent_db" - persistent test database (reused across tests)
 * - "tests.temp_db" - temporary test database (recreated per test)
 *
 * Environment variable overrides:
 * - FIREBIRD_HOST - server hostname/IP
 * - FIREBIRD_PORT - server port (appended to host as host/port)
 * - FIREBIRD_USER - database user
 * - FIREBIRD_PASSWORD - database password
 * - FIREBIRD_CHARSET - character set
 * - FIREBIRD_PERSISTENT_DB_PATH - path for persistent DB (sections: "db", "tests.persistent_db")
 * - FIREBIRD_DB_PATH - path for temporary DB (section: "tests.temp_db")
 *
 * Local development (no ENV vars):
 *   firebird5.home.lan:testdb
 *
 * CI/CD (with ENV vars):
 *   localhost:/tmp/testdb.fdb
 *
 * @param section Configuration section to use ("db", "tests.persistent_db", "tests.temp_db")
 * @return ConnectionParams configured with values from config file and ENV overrides
 * @throws std::runtime_error if section is unknown or config cannot be loaded
 */
inline ConnectionParams getConnectionParams(const std::string& section = "db") {
    auto config = loadConfig();

    // Select appropriate config section
    json db_config;
    if (section == "db") {
        db_config = config["db"];
    } else if (section == "tests.persistent_db") {
        db_config = config["tests"]["persistent_db"];
    } else if (section == "tests.temp_db") {
        db_config = config["tests"]["temp_db"];
    } else {
        throw std::runtime_error("Unknown config section: " + section);
    }

    // Read defaults from JSON config
    std::string server = db_config.value("server", "firebird5.home.lan");
    std::string path = db_config.value("path", "testdb");
    std::string user = db_config.value("user", "SYSDBA");
    std::string password = db_config.value("password", "planomer");
    std::string charset = db_config.value("charset", "UTF8");

    // Override with environment variables if set (for CI/CD)
    // This follows 12-factor app methodology: ENV vars > config files
    if (const char* env_host = std::getenv("FIREBIRD_HOST")) {
        server = env_host;
    }
    if (const char* env_port = std::getenv("FIREBIRD_PORT")) {
        // If port is explicitly provided, append it to server
        server = server + "/" + env_port;
    }

    // Handle both persistent and temp DB paths
    // For "db" and "tests.persistent_db": use FIREBIRD_PERSISTENT_DB_PATH
    // For "tests.temp_db": use FIREBIRD_DB_PATH
    if (section == "db" || section == "tests.persistent_db") {
        if (const char* env_persistent_path = std::getenv("FIREBIRD_PERSISTENT_DB_PATH")) {
            path = env_persistent_path;
        }
    } else if (section == "tests.temp_db") {
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
    ConnectionParams params;
    params.database = server + ":" + path;
    params.user = user;
    params.password = password;
    params.charset = charset;

    return params;
}

} // namespace util
} // namespace fbpp
