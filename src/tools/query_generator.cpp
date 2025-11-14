#include "fbpp/query_generator_service.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using fbpp::core::Connection;
using fbpp::core::ConnectionParams;
using fbpp::core::FirebirdException;
using fbpp::core::QueryDefinition;
using fbpp::core::QueryGeneratorService;
using fbpp::core::renderQueryGeneratorMainHeader;
using fbpp::core::renderQueryGeneratorSupportHeader;

namespace {

struct Options {
    std::string dsn;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::filesystem::path inputPath;
    std::filesystem::path outputHeader;
    std::filesystem::path supportHeader;

    // Adapter configuration
    bool useTTMathNumeric = false;
    bool useTTMathInt128 = false;
    bool useChronoDatetime = false;
    bool useCppDecimalDecFloat = false;
    bool generateAliases = true;
};

void printUsage() {
    std::cout << R"(Usage: query_generator [options]

Required arguments:
  --dsn <path>              Database path (e.g., firebird5:/path/to/db.fdb)
  --input <file.json>       Input JSON file with queries
  --output <file.hpp>       Output header file with query wrappers
  --support <file.hpp>      Output support header with struct definitions

Optional arguments:
  --user <name>             Database user (default: SYSDBA)
  --password <pass>         Database password (default: planomer)
  --charset <charset>       Character set (default: UTF8)

Adapter options:
  --use-ttmath-numeric      Use TTMath for NUMERIC(38,x) types
  --use-ttmath-int128       Use TTMath for INT128 types
  --use-chrono              Use std::chrono for DATE/TIME/TIMESTAMP types
  --use-cppdecimal          Use CppDecimal for DECFLOAT types
  --no-aliases              Do not generate type aliases (using declarations)

Examples:
  # Generate with core types only (default)
  query_generator --dsn firebird5:/db.fdb --input queries.json --output queries.hpp --support queries.structs.hpp

  # Generate with TTMath adapters
  query_generator --dsn firebird5:/db.fdb --input queries.json --output queries.hpp --support queries.structs.hpp \
    --use-ttmath-numeric --use-ttmath-int128

  # Generate with all adapters
  query_generator --dsn firebird5:/db.fdb --input queries.json --output queries.hpp --support queries.structs.hpp \
    --use-ttmath-numeric --use-ttmath-int128 --use-chrono --use-cppdecimal

For more information, see doc/QUERY_GENERATOR_USER_GUIDE.md
)";
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
        } else if (arg == "--use-ttmath-numeric") {
            opts.useTTMathNumeric = true;
        } else if (arg == "--use-ttmath-int128") {
            opts.useTTMathInt128 = true;
        } else if (arg == "--use-chrono") {
            opts.useChronoDatetime = true;
        } else if (arg == "--use-cppdecimal") {
            opts.useCppDecimalDecFloat = true;
        } else if (arg == "--no-aliases") {
            opts.generateAliases = false;
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

void ensureParentDir(const std::filesystem::path& filePath) {
    auto parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void writeToFile(const std::filesystem::path& filePath, const std::string& contents) {
    ensureParentDir(filePath);
    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open output file: " + filePath.string());
    }
    out << contents;
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

        // Create adapter configuration from command-line options
        fbpp::core::AdapterConfig config;
        config.useTTMathNumeric = opts.useTTMathNumeric;
        config.useTTMathInt128 = opts.useTTMathInt128;
        config.useChronoDatetime = opts.useChronoDatetime;
        config.useCppDecimalDecFloat = opts.useCppDecimalDecFloat;
        config.generateAliases = opts.generateAliases;

        const auto specs = service.buildQuerySpecs(definitions, config);
        const auto supportHeaderName = opts.supportHeader.filename().string();

        const auto mainHeader = renderQueryGeneratorMainHeader(specs, supportHeaderName, config);
        const auto supportHeader = renderQueryGeneratorSupportHeader(specs, config);

        writeToFile(opts.outputHeader, mainHeader);
        writeToFile(opts.supportHeader, supportHeader);

        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "Firebird exception: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
