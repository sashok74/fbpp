#include "fbpp_util/connection_helper.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fbpp::util {

json loadConfig() {
    const std::vector<std::string> paths = {
        "../../config/test_config.json",  // From build/examples or build/tests
        "../config/test_config.json",     // From build
        "config/test_config.json",        // From project root
        "./test_config.json"              // Current directory
    };

    std::ifstream configFile;
    for (const auto& path : paths) {
        configFile.open(path);
        if (configFile.is_open()) {
            break;
        }
    }

    if (!configFile.is_open()) {
        throw std::runtime_error("Cannot open test_config.json - tried multiple paths");
    }

    json config;
    configFile >> config;
    return config;
}

ConnectionParams getConnectionParams(const std::string& section) {
    auto config = loadConfig();

    json dbConfig;
    if (section == "db") {
        dbConfig = config["db"];
    } else if (section == "tests.temp_db") {
        dbConfig = config["tests"]["temp_db"];
    } else {
        throw std::runtime_error("Unknown config section: " + section);
    }

    std::string server = dbConfig.value("server", "firebird5.home.lan");
    std::string path = dbConfig.value("path", "testdb");
    std::string user = dbConfig.value("user", "SYSDBA");
    std::string password = dbConfig.value("password", "planomer");
    std::string charset = dbConfig.value("charset", "UTF8");

    if (const char* envHost = std::getenv("FIREBIRD_HOST")) {
        server = envHost;
    }
    if (const char* envPort = std::getenv("FIREBIRD_PORT")) {
        server = server + "/" + envPort;
    }

    if (section == "db") {
        if (const char* envMainPath = std::getenv("FIREBIRD_MAIN_DB_PATH")) {
            path = envMainPath;
        }
    } else if (section == "tests.temp_db") {
        if (const char* envPath = std::getenv("FIREBIRD_DB_PATH")) {
            path = envPath;
        }
    }

    if (const char* envUser = std::getenv("FIREBIRD_USER")) {
        user = envUser;
    }
    if (const char* envPass = std::getenv("FIREBIRD_PASSWORD")) {
        password = envPass;
    }
    if (const char* envCharset = std::getenv("FIREBIRD_CHARSET")) {
        charset = envCharset;
    }

    ConnectionParams params;
    params.database = server + ":" + path;
    params.user = user;
    params.password = password;
    params.charset = charset;
    return params;
}

} // namespace fbpp::util
