#include "fbpp/core/connection.hpp"
#include "fbpp/core/query_executor.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp_util/logging.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace fbpp::core;

namespace {

struct FetchInput {};

struct TableRow {
    std::int32_t id;
    std::optional<std::int32_t> fInteger;
    std::optional<std::string> fVarchar;
    std::optional<double> fDouble;
    std::optional<bool> fBoolean;
};

enum class QueryId { FetchTop100 };

template<QueryId>
struct QueryDescriptor;

template<>
struct QueryDescriptor<QueryId::FetchTop100> {
    static constexpr std::string_view sql =
        "SELECT FIRST 100 ID, F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN "
        "FROM TABLE_TEST_1 ORDER BY ID";
    using Input = FetchInput;
    using Output = TableRow;
};

std::string boolToString(std::optional<bool> value) {
    if (!value.has_value()) {
        return "null";
    }
    return value.value() ? "true" : "false";
}

} // namespace

namespace fbpp::core {

template<>
struct StructDescriptor<::FetchInput> {
    static constexpr auto fields = std::make_tuple();
};

template<>
struct StructDescriptor<::TableRow> {
    static constexpr auto fields = std::make_tuple(
        makeField<&TableRow::id>("ID", SQL_LONG, 0, sizeof(std::int32_t), false),
        makeField<&TableRow::fInteger>("F_INTEGER", SQL_LONG, 0, sizeof(std::int32_t), true),
        makeField<&TableRow::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 255, true),
        makeField<&TableRow::fDouble>("F_DOUBLE_PRECISION", SQL_DOUBLE, 0, sizeof(double), true),
        makeField<&TableRow::fBoolean>("F_BOOLEAN", SQL_BOOLEAN, 0, 1, true));
};

} // namespace fbpp::core

int main() {
    try {
        std::vector<std::string> configPaths = {
            "../../config/test_config.json",
            "../config/test_config.json",
            "config/test_config.json",
            "./test_config.json"
        };

        std::ifstream configFile;
        for (const auto& path : configPaths) {
            configFile.open(path);
            if (configFile.is_open()) {
                std::cout << "Configuration loaded from: " << path << '\n';
                break;
            }
        }

        if (!configFile.is_open()) {
            std::cerr << "Could not find test_config.json\n";
            return 1;
        }

        auto config = nlohmann::json::parse(configFile);
        auto dbConfig = config["tests"]["persistent_db"];

        ConnectionParams params;
        params.database = dbConfig["server"].get<std::string>() + ":" + dbConfig["path"].get<std::string>();
        params.user = dbConfig["user"].get<std::string>();
        params.password = dbConfig["password"].get<std::string>();
        params.charset = dbConfig["charset"].get<std::string>();

        auto connection = std::make_unique<Connection>(params);
        auto transaction = connection->StartTransaction();

        auto rows = executeQuery<QueryDescriptor<QueryId::FetchTop100>>(
            *connection, *transaction, FetchInput{});
        transaction->Commit();

        std::cout << "Fetched " << rows.size() << " rows from TABLE_TEST_1\n";

        const std::size_t previewCount = std::min<std::size_t>(rows.size(), 5);
        for (std::size_t i = 0; i < previewCount; ++i) {
            const auto& row = rows[i];
            std::cout << "Row #" << i + 1 << ": "
                      << "ID=" << row.id
                      << ", F_INTEGER=" << (row.fInteger ? std::to_string(*row.fInteger) : "null")
                      << ", F_VARCHAR=" << (row.fVarchar ? *row.fVarchar : "null")
                      << ", F_DOUBLE_PRECISION=" << (row.fDouble ? std::to_string(*row.fDouble) : "null")
                      << ", F_BOOLEAN=" << boolToString(row.fBoolean)
                      << '\n';
        }

        if (rows.empty()) {
            std::cout << "TABLE_TEST_1 contains no records.\n";
        }
        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "FirebirdException: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
