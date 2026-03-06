#include "test_base.hpp"
#include "fbpp/query_generator_service.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using fbpp::core::Connection;
using fbpp::core::FirebirdException;
using fbpp::core::QueryDefinition;
using fbpp::core::QueryGeneratorService;
using fbpp::core::renderQueryGeneratorMainHeader;
using fbpp::core::renderQueryGeneratorSupportHeader;
using fbpp::test::SchemaProfile;

namespace {

struct Options {
    std::string scope;
    std::string configSection = "tests.temp_db";
    std::filesystem::path inputPath;
    std::filesystem::path outputHeader;
    std::filesystem::path supportHeader;
    std::vector<SchemaProfile> profiles;

    bool useTTMathNumeric = false;
    bool useTTMathInt128 = false;
    bool useChronoDatetime = false;
    bool useCppDecimalDecFloat = false;
    bool useStringForTextBlob = false;
    bool generateAliases = true;
};

SchemaProfile parseProfile(const std::string& value) {
    if (value == "basic_table") {
        return SchemaProfile::BasicTable;
    }
    if (value == "query_metadata") {
        return SchemaProfile::QueryMetadata;
    }
    if (value == "table_test_1") {
        return SchemaProfile::TableTest1;
    }
    if (value == "ttmath") {
        return SchemaProfile::TTMath;
    }
    if (value == "decfloat") {
        return SchemaProfile::DecFloat;
    }
    if (value == "datetime") {
        return SchemaProfile::DateTime;
    }
    if (value == "combined") {
        return SchemaProfile::Combined;
    }
    if (value == "extended_types") {
        return SchemaProfile::ExtendedTypes;
    }

    throw std::runtime_error("Unknown schema profile: " + value);
}

void printUsage() {
    std::cout << R"(Usage: test_query_generator_runner [options]

Required arguments:
  --scope <name>             Unique suffix for the managed test database
  --profile <name>           Schema profile to apply (repeatable)
  --input <file.json>        Input JSON file with queries
  --output <file.hpp>        Output header file with query wrappers
  --support <file.hpp>       Output support header with struct descriptors

Optional arguments:
  --config-section <name>    Connection section from test_config.json (default: tests.temp_db)
  --use-ttmath-numeric
  --use-ttmath-int128
  --use-chrono
  --use-cppdecimal
  --use-string-blob
  --no-aliases
)";
}

std::optional<Options> parseOptions(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument " + arg);
            }
            return argv[++i];
        };

        if (arg == "--scope") {
            opts.scope = next();
        } else if (arg == "--config-section") {
            opts.configSection = next();
        } else if (arg == "--profile") {
            opts.profiles.push_back(parseProfile(next()));
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
        } else if (arg == "--use-string-blob") {
            opts.useStringForTextBlob = true;
        } else if (arg == "--no-aliases") {
            opts.generateAliases = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return std::nullopt;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (opts.scope.empty() || opts.profiles.empty() || opts.inputPath.empty() ||
        opts.outputHeader.empty() || opts.supportHeader.empty()) {
        throw std::runtime_error("Missing required arguments.");
    }

    return opts;
}

void ensureParentDir(const std::filesystem::path& filePath) {
    const auto parent = filePath.parent_path();
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

struct DatabaseGuard {
    fbpp::core::ConnectionParams params;

    ~DatabaseGuard() {
        fbpp::test::dropTestDatabaseQuietly(params);
    }
};

} // namespace

int main(int argc, char** argv) {
    try {
        auto opts = parseOptions(argc, argv);
        if (!opts.has_value()) {
            return 0;
        }

        std::ifstream inFile(opts->inputPath);
        if (!inFile) {
            throw std::runtime_error("Failed to open input file: " + opts->inputPath.string());
        }

        const nlohmann::json jsonQueries = nlohmann::json::parse(inFile);
        if (!jsonQueries.is_object()) {
            throw std::runtime_error("Input JSON must be an object of name->sql.");
        }

        auto params = fbpp::test::makeScopedTestDatabaseParams(
            "query_generator_" + opts->scope, opts->configSection);
        fbpp::test::recreateTestDatabase(params);
        DatabaseGuard guard{params};

        Connection connection(params);
        fbpp::test::applySchemaProfiles(connection, opts->profiles);

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

        fbpp::core::AdapterConfig config;
        config.useTTMathNumeric = opts->useTTMathNumeric;
        config.useTTMathInt128 = opts->useTTMathInt128;
        config.useChronoDatetime = opts->useChronoDatetime;
        config.useCppDecimalDecFloat = opts->useCppDecimalDecFloat;
        config.useStringForTextBlob = opts->useStringForTextBlob;
        config.generateAliases = opts->generateAliases;

        const auto specs = service.buildQuerySpecs(definitions, config);
        const auto supportHeaderName = opts->supportHeader.filename().string();

        writeToFile(opts->outputHeader,
                    renderQueryGeneratorMainHeader(specs, supportHeaderName, config));
        writeToFile(opts->supportHeader,
                    renderQueryGeneratorSupportHeader(specs, config));

        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "Firebird exception: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
