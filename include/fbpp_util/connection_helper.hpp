#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "fbpp/core/connection.hpp"

namespace fbpp {
namespace util {

using json = nlohmann::json;
using namespace fbpp::core;

json loadConfig();

/**
 * @brief Gets database connection parameters with environment variable overrides
 *
 * This function implements the 12-factor app methodology:
 * Environment variables have priority over config file values.
 *
 * Supported sections:
 * - "db" - main database configuration (used by examples and shared demo data)
 * - "tests.temp_db" - base config for managed test databases created by fixtures/build helpers
 *
 * Environment variable overrides:
 * - FIREBIRD_HOST - server hostname/IP
 * - FIREBIRD_PORT - server port (appended to host as host/port)
 * - FIREBIRD_USER - database user
 * - FIREBIRD_PASSWORD - database password
 * - FIREBIRD_CHARSET - character set
 * - FIREBIRD_MAIN_DB_PATH - path override for main "db" section
 * - FIREBIRD_DB_PATH - path override for managed test DBs based on "tests.temp_db"
 *
 * Local development (no ENV vars):
 *   firebird5.home.lan:testdb
 *
 * CI/CD (with ENV vars):
 *   localhost:/tmp/testdb.fdb
 *
 * @param section Configuration section to use ("db" or "tests.temp_db")
 * @return ConnectionParams configured with values from config file and ENV overrides
 * @throws std::runtime_error if section is unknown or config cannot be loaded
 */
ConnectionParams getConnectionParams(const std::string& section = "db");

} // namespace util
} // namespace fbpp
