#include "fbpp/query_generator_service.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using fbpp::core::Connection;
using fbpp::core::ConnectionParams;
using fbpp::core::FirebirdException;
using fbpp::core::QueryDefinition;
using fbpp::core::QueryGeneratorConfig;
using fbpp::core::QueryGeneratorService;

namespace {

struct Options {
    std::string dsn;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::filesystem::path inputPath;
    std::filesystem::path outputHeader;
    std::filesystem::path supportHeader;
};

void printUsage() {
    std::cout << "Usage: query_generator --dsn <dsn> [--user name] [--password pass] "
                 "[--charset utf8] --input <queries.json> --output <queries.hpp> "
                 "--support <queries.structs.hpp>\n";
}

std::optional<Options> parseOptions(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument " + arg);
            }
            return argv[++i];
        };

        if (arg == "--dsn") {
            opts.dsn = next();
        } else if (arg == "--user") {
            opts.user = next();
        } else if (arg == "--password") {
            opts.password = next();
        } else if (arg == "--charset") {
            opts.charset = next();
        } else if (arg == "--input") {
            opts.inputPath = next();
        } else if (arg == "--output") {
            opts.outputHeader = next();
        } else if (arg == "--support") {
            opts.supportHeader = next();
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return std::nullopt;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (opts.dsn.empty() || opts.inputPath.empty() || opts.outputHeader.empty() || opts.supportHeader.empty()) {
        throw std::runtime_error("Missing required arguments.");
    }

    return opts;
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto optsOpt = parseOptions(argc, argv);
        if (!optsOpt.has_value()) {
            return 0;
        }
        const auto& opts = optsOpt.value();

        std::ifstream inFile(opts.inputPath);
        if (!inFile) {
            throw std::runtime_error("Failed to open input file: " + opts.inputPath.string());
        }

        nlohmann::json jsonQueries = nlohmann::json::parse(inFile);
        if (!jsonQueries.is_object()) {
            throw std::runtime_error("Input JSON must be an object of name->sql.");
        }

        ConnectionParams params;
        params.database = opts.dsn;
        params.user = opts.user;
        params.password = opts.password;
        params.charset = opts.charset;

        Connection connection(params);
        QueryGeneratorService service(connection);

        std::vector<QueryDefinition> definitions;
        definitions.reserve(jsonQueries.size());
        for (auto it = jsonQueries.begin(); it != jsonQueries.end(); ++it) {
            if (!it.value().is_string()) {
                throw std::runtime_error("Query text for '" + it.key() + "' must be a string.");
            }

            definitions.push_back(QueryDefinition{
                .name = it.key(),
                .sql = it.value().get<std::string>()
            });
        }

        QueryGeneratorConfig config{
            .queries = std::move(definitions),
            .outputHeader = opts.outputHeader,
            .supportHeader = opts.supportHeader
        };

        service.generate(config);

        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "Firebird exception: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
