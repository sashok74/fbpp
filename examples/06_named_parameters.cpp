/**
 * Example: Named Parameters Support with TABLE_TEST_1
 *
 * Demonstrates the use of named parameters in SQL queries with FBPP
 * using the existing TABLE_TEST_1 table.
 */

#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp_util/logging.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>

using namespace fbpp::core;
using json = nlohmann::json;

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void printInfo(const std::string& label, const std::string& value) {
    std::cout << "  " << std::setw(15) << std::left << label << ": " << value << "\n";
}

int main() {
    try {
        // Initialize logging
        auto logger = util::Logging::get();
        if (!logger) {
            logger = spdlog::default_logger();
        }

        printHeader("Named Parameters Example with TABLE_TEST_1");

        // Load configuration
        std::vector<std::string> paths = {
            "../../config/test_config.json",
            "../config/test_config.json",
            "config/test_config.json",
            "./test_config.json"
        };

        std::ifstream config_file;
        for (const auto& path : paths) {
            config_file.open(path);
            if (config_file.is_open()) {
                std::cout << "Конфигурация загружена из: " << path << "\n";
                break;
            }
        }

        if (!config_file.is_open()) {
            std::cerr << "Не найден файл конфигурации test_config.json\n";
            std::cerr << "Убедитесь, что файл находится в одном из путей:\n";
            for (const auto& path : paths) {
                std::cerr << "  - " << path << "\n";
            }
            return 1;
        }

        auto config = nlohmann::json::parse(config_file);
        auto db_config = config["tests"]["persistent_db"];

        std::string database = std::string(db_config.value("server", "firebird5")) + ":" +
                              std::string(db_config["path"]);
        std::string username = db_config["user"];
        std::string password = db_config["password"];
        std::string charset = db_config["charset"];

        printInfo("Database", database);
        printInfo("User", username);
        printInfo("Charset", charset);

        std::cout << "\nПодключение к базе данных...\n";

        // Create connection parameters
        ConnectionParams params;
        params.database = database;
        params.user = username;
        params.password = password;
        params.charset = charset;

        // Connect to existing database
        auto connection = std::make_unique<Connection>(params);
        std::cout << "✓ Успешно подключились к базе данных!\n";

        // Check if TABLE_TEST_1 exists
        auto transaction = connection->StartTransaction();
        auto checkStmt = connection->prepareStatement(
            "SELECT COUNT(*) FROM RDB$RELATIONS "
            "WHERE RDB$RELATION_NAME = 'TABLE_TEST_1' AND RDB$SYSTEM_FLAG = 0"
        );

        auto cursor = transaction->openCursor(checkStmt);
        std::tuple<int64_t> count_result;
        if (cursor->fetch(count_result)) {
            if (std::get<0>(count_result) == 0) {
                std::cerr << "✗ Таблица TABLE_TEST_1 не найдена в базе данных\n";
                return 1;
            }
        }
        cursor.reset();  // Close cursor before commit
        std::cout << "✓ Таблица TABLE_TEST_1 найдена\n";
        transaction->Commit();

        // Start new transaction for examples
        transaction = connection->StartTransaction();

        // Get max key to avoid conflicts
        int32_t base_key = 1000000;
        try {
            auto maxKeyStmt = connection->prepareStatement(
                "SELECT COALESCE(MAX(F_INTEGER), 1000000) FROM TABLE_TEST_1"
            );
            auto maxCursor = transaction->openCursor(maxKeyStmt);
            std::tuple<int32_t> max_key;
            if (maxCursor->fetch(max_key)) {
                base_key = std::get<0>(max_key) + 1000;
            }
            maxCursor.reset();  // Close cursor
        } catch (const std::exception& e) {
            std::cerr << "Error getting max key: " << e.what() << "\n";
            // Use default base_key
        }
        std::cout << "Используем ключи начиная с: " << base_key << "\n";

        printHeader("1. INSERT WITH NAMED PARAMETERS (:param syntax)");

        // Insert using named parameters with : prefix
        auto insertStmt = connection->prepareStatement(R"SQL(
            INSERT INTO TABLE_TEST_1 (
                F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN
            ) VALUES (
                :id, :name, :price, :active
            ) RETURNING ID
        )SQL");

        // Using JSON object with named parameters
        json record1;
        record1["id"] = base_key + 1;
        record1["name"] = "Named Parameter Test 1";
        record1["price"] = 99.99;
        record1["active"] = true;

        transaction->execute(insertStmt, record1);
        std::cout << "✓ Inserted record with ID: " << record1["id"] << "\n";

        printHeader("2. INSERT WITH @ PREFIX PARAMETERS");

        // Insert using @ prefix (SQL Server style)
        auto insertStmt2 = connection->prepareStatement(R"SQL(
            INSERT INTO TABLE_TEST_1 (
                F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN
            ) VALUES (
                @id, @name, @price, @active
            ) RETURNING ID
        )SQL");

        json record2;
        record2["id"] = base_key + 2;
        record2["name"] = "Named Parameter Test 2";
        record2["price"] = 149.99;
        record2["active"] = false;

        transaction->execute(insertStmt2, record2);
        std::cout << "✓ Inserted record with ID: " << record2["id"] << "\n";

        printHeader("3. INSERT WITH MISSING PARAMETERS (NULL values)");

        // Insert with some parameters missing (will be NULL)
        json record3;
        record3["id"] = base_key + 3;
        record3["name"] = "Partial Data Record";
        // price and date not provided - will be NULL
        record3["active"] = true;

        transaction->execute(insertStmt, record3);
        std::cout << "✓ Inserted record with ID: " << record3["id"] << " (NULLs for missing params)\n";

        printHeader("4. SELECT WITH REPEATED NAMED PARAMETERS");

        // Query with repeated named parameter
        auto selectStmt = connection->prepareStatement(R"SQL(
            SELECT F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN
            FROM TABLE_TEST_1
            WHERE F_INTEGER >= :min_id
               OR F_INTEGER = :specific_id
               OR F_INTEGER <= :min_id + 10
            ORDER BY F_INTEGER
        )SQL");

        json queryParams;
        queryParams["min_id"] = base_key;
        queryParams["specific_id"] = base_key + 2;

        auto selectCursor = transaction->openCursor(selectStmt, queryParams);

        std::cout << "\nRecords matching criteria:\n";
        std::cout << std::left;
        std::cout << std::setw(10) << "ID"
                  << std::setw(30) << "Name"
                  << std::setw(10) << "Price"
                  << std::setw(8) << "Active" << "\n";
        std::cout << std::string(58, '-') << "\n";

        json row;
        int count = 0;
        while (selectCursor->fetch(row) && count < 10) {
            // JSON array for result columns
            if (row.is_array() && row.size() >= 4) {
                std::cout << std::setw(10) << (row[0].is_null() ? "NULL" : row[0].dump())
                          << std::setw(30) << (row[1].is_null() ? "NULL" : row[1].get<std::string>())
                          << std::setw(10) << (row[2].is_null() ? "NULL" : std::to_string(row[2].get<double>()))
                          << std::setw(8) << (row[3].is_null() ? "NULL" : (row[3].get<bool>() ? "true" : "false"))
                          << "\n";
            }
            count++;
        }

        printHeader("5. UPDATE WITH NAMED PARAMETERS");

        // Update using named parameters
        auto updateStmt = connection->prepareStatement(R"SQL(
            UPDATE TABLE_TEST_1
            SET F_VARCHAR = :new_name,
                F_DOUBLE_PRECISION = F_DOUBLE_PRECISION * :multiplier
            WHERE F_INTEGER = :record_id
        )SQL");

        json updateParams;
        updateParams["new_name"] = "Updated via Named Params";
        updateParams["multiplier"] = 1.1;  // Increase price by 10%
        updateParams["record_id"] = base_key + 1;

        unsigned affected = transaction->execute(updateStmt, updateParams);
        std::cout << "✓ Updated " << affected << " record(s)\n";

        printHeader("6. CASE-INSENSITIVE PARAMETER NAMES");

        // Parameters are case-insensitive
        auto mixedCaseStmt = connection->prepareStatement(R"SQL(
            SELECT F_VARCHAR FROM TABLE_TEST_1 WHERE F_INTEGER = :RecordId
        )SQL");

        json mixedParams;
        mixedParams["RECORDID"] = base_key + 1;  // Different case - still works

        auto mixedCursor = transaction->openCursor(mixedCaseStmt, mixedParams);
        if (mixedCursor->fetch(row)) {
            if (row.is_array() && row.size() > 0) {
                std::cout << "✓ Found record with case-insensitive param: "
                          << row[0].get<std::string>() << "\n";
            }
        }

        printHeader("7. PERFORMANCE COMPARISON");

        // Prepare statements for performance test
        auto perfStmt1 = connection->prepareStatement(
            "SELECT * FROM TABLE_TEST_1 WHERE F_INTEGER = :id"
        );
        auto perfStmt2 = connection->prepareStatement(
            "SELECT * FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
        );

        const int iterations = 100;

        // Test named parameters
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            json params;
            params["id"] = base_key + (i % 3);
            auto c = transaction->openCursor(perfStmt1, params);
            json r;
            c->fetch(r);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto named_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        // Test positional parameters
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            json params = json::array();
            params.push_back(base_key + (i % 3));
            auto c = transaction->openCursor(perfStmt2, params);
            json r;
            c->fetch(r);
        }
        end = std::chrono::high_resolution_clock::now();
        auto positional_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "Performance comparison (" << iterations << " iterations):\n";
        std::cout << "  Named parameters:      " << named_time << " µs\n";
        std::cout << "  Positional parameters: " << positional_time << " µs\n";
        std::cout << "  Overhead: " << ((named_time - positional_time) * 100.0 / positional_time) << "%\n";

        printHeader("8. STATEMENT CACHE WITH NAMED PARAMS");

        // Get cache statistics
        auto stats = connection->getCacheStatistics();
        std::cout << "\nCache Statistics:\n";
        std::cout << "  Cache size: " << stats.cacheSize << "\n";
        std::cout << "  Hits: " << stats.hitCount << "\n";
        std::cout << "  Misses: " << stats.missCount << "\n";
        std::cout << "  Hit Rate: " << std::fixed << std::setprecision(1)
                  << stats.hitRate << "%\n";

        // Cleanup - delete test records
        auto deleteStmt = connection->prepareStatement(
            "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER BETWEEN :min_id AND :max_id"
        );

        json deleteParams;
        deleteParams["min_id"] = base_key;
        deleteParams["max_id"] = base_key + 100;

        affected = transaction->execute(deleteStmt, deleteParams);
        std::cout << "\n✓ Cleaned up " << affected << " test record(s)\n";

        // Commit transaction
        transaction->Commit();
        logger->info("Transaction committed");

        printHeader("✅ EXAMPLE COMPLETED SUCCESSFULLY");

    } catch (const FirebirdException& e) {
        std::cerr << "\n❌ Firebird Error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}