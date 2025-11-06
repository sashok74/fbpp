#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/named_param_parser.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/message_metadata.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using fbpp::core::Connection;
using fbpp::core::ConnectionParams;
using fbpp::core::FirebirdException;
using fbpp::core::FieldInfo;
using fbpp::core::NamedParamParser;

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

std::string escapeString(const std::string& input) {
    std::string result;
    result.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string toCamelCase(const std::string& sqlName, bool lowerFirst = true) {
    std::string out;
    out.reserve(sqlName.size());
    bool capitalize = !lowerFirst;
    for (char c : sqlName) {
        if (c == '_' || c == ' ') {
            capitalize = true;
            continue;
        }
        char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (capitalize) {
            cc = static_cast<char>(std::toupper(static_cast<unsigned char>(cc)));
            capitalize = false;
        }
        out.push_back(cc);
    }
    if (lowerFirst && !out.empty()) {
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    }
    return out;
}

struct TypeMapping {
    std::string cppType;
    bool needsOptional = false;
    bool needsString = false;
    bool needsTimestamp = false;
};

TypeMapping mapFieldType(const FieldInfo& field, bool isOutput) {
    auto baseType = static_cast<unsigned>(field.type);
    bool nullable = field.nullable || (isOutput && field.type % 2 == 1);

    TypeMapping result;
    switch (baseType & ~1U) {
        case SQL_SHORT:
            result.cppType = "std::int16_t";
            break;
        case SQL_LONG:
            result.cppType = "std::int32_t";
            break;
        case SQL_INT64:
            result.cppType = "std::int64_t";
            break;
        case SQL_FLOAT:
            result.cppType = "float";
            break;
        case SQL_DOUBLE:
            result.cppType = "double";
            break;
        case SQL_TEXT:
        case SQL_VARYING:
            result.cppType = "std::string";
            result.needsString = true;
            break;
        case SQL_TIMESTAMP:
            result.cppType = "fbpp::core::Timestamp";
            result.needsTimestamp = true;
            break;
        case SQL_TYPE_DATE:
            result.cppType = "fbpp::core::Date";
            result.needsTimestamp = true;
            break;
        case SQL_TYPE_TIME:
            result.cppType = "fbpp::core::Time";
            result.needsTimestamp = true;
            break;
        default:
            throw std::runtime_error("Unsupported SQL type: " + std::to_string(field.type));
    }

    result.needsOptional = nullable;
    if (nullable) {
        result.cppType = "std::optional<" + result.cppType + ">";
    }

    return result;
}

struct FieldSpec {
    std::string sqlName;
    std::string memberName;
    TypeMapping type;
    FieldInfo info;
};

struct QuerySpec {
    std::string name;
    std::string originalSql;
    std::string preparedSql;
    std::vector<FieldSpec> inputs;
    std::vector<FieldSpec> outputs;
};

std::vector<FieldSpec> buildFieldSpecs(const std::vector<FieldInfo>& fields,
                                       bool isOutput) {
    std::vector<FieldSpec> specs;
    specs.reserve(fields.size());

    std::unordered_map<std::string, int> usedNames;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        FieldSpec spec;
        spec.info = fields[i];
        spec.sqlName = fields[i].name.empty()
                           ? ("PARAM_" + std::to_string(i + 1))
                           : fields[i].name;

        std::string baseName = toCamelCase(spec.sqlName);
        auto [it, inserted] = usedNames.emplace(baseName, 0);
        if (!inserted) {
            ++(it->second);
            baseName += std::to_string(it->second + 1);
        }
        spec.memberName = baseName;
        spec.type = mapFieldType(fields[i], isOutput);

        specs.push_back(std::move(spec));
    }
    return specs;
}

std::string makeStructName(const std::string& queryName, bool isInput) {
    return queryName + (isInput ? "In" : "Out");
}

void writeMainHeader(const std::filesystem::path& path,
                     const std::string& headerGuard,
                     const std::string& supportHeaderName,
                     const std::vector<QuerySpec>& queries) {
    bool needsOptional = false;
    bool needsString = false;
    bool needsTimestamp = false;

    for (const auto& q : queries) {
        for (const auto& f : q.inputs) {
            needsOptional |= f.type.needsOptional;
            needsString |= f.type.needsString;
            needsTimestamp |= f.type.needsTimestamp;
        }
        for (const auto& f : q.outputs) {
            needsOptional |= f.type.needsOptional;
            needsString |= f.type.needsString;
            needsTimestamp |= f.type.needsTimestamp;
        }
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open output header: " + path.string());
    }

    out << "#pragma once\n\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    if (needsOptional) {
        out << "#include <optional>\n";
    }
    if (needsTimestamp) {
        out << "#include \"fbpp/core/extended_types.hpp\"\n";
    }
    out << "#include \"fbpp/core/query_executor.hpp\"\n\n";

    out << "namespace generated::queries {\n\n";

    out << "enum class QueryId {\n";
    for (std::size_t i = 0; i < queries.size(); ++i) {
        out << "    " << queries[i].name;
        if (i + 1 != queries.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "};\n\n";

    out << "template<QueryId Q>\nstruct QueryDescriptor;\n\n";

    for (const auto& q : queries) {
        auto writeStruct = [&](bool isInput) {
            const auto& fields = isInput ? q.inputs : q.outputs;
            std::string structName = makeStructName(q.name, isInput);
            out << "struct " << structName << " {\n";
            for (const auto& f : fields) {
                out << "    " << f.type.cppType << " " << f.memberName << ";\n";
            }
            if (fields.empty()) {
                out << "    // empty\n";
            }
            out << "};\n\n";
        };

        writeStruct(true);
        writeStruct(false);
    }

    for (const auto& q : queries) {
        out << "template<>\nstruct QueryDescriptor<QueryId::" << q.name << "> {\n";
        out << "    static constexpr std::string_view sql = \""
            << escapeString(q.preparedSql) << "\";\n";
        out << "    using Input = " << makeStructName(q.name, true) << ";\n";
        out << "    using Output = " << makeStructName(q.name, false) << ";\n";
        out << "};\n\n";
    }

    out << "} // namespace generated::queries\n";
}

void writeSupportHeader(const std::filesystem::path& path,
                        const std::string& mainHeaderName,
                        const std::vector<QuerySpec>& queries) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open support header: " + path.string());
    }

    out << "#pragma once\n\n";
    out << "#include <tuple>\n";
    out << "#include \"fbpp/core/struct_pack.hpp\"\n";
    out << "#include \"fbpp/core/firebird_compat.hpp\"\n";
    out << "#include \"" << mainHeaderName << "\"\n\n";

    out << "namespace fbpp::core {\n\n";

    for (const auto& q : queries) {
        auto writeDescriptor = [&](bool isInput) {
            const auto& fields = isInput ? q.inputs : q.outputs;
            std::string structName = "generated::queries::" + makeStructName(q.name, isInput);
            out << "template<>\nstruct StructDescriptor<" << structName << "> {\n";
            out << "    static constexpr auto fields = std::make_tuple(\n";
            for (std::size_t i = 0; i < fields.size(); ++i) {
                const auto& f = fields[i];
                out << "        fbpp::core::makeField<&" << structName << "::" << f.memberName
                    << ">(\"" << f.sqlName << "\", " << f.info.type << ", "
                    << f.info.scale << ", " << f.info.length << ", "
                    << (f.info.nullable ? "true" : "false") << ", "
                    << f.info.subType << ")";
                if (i + 1 != fields.size()) {
                    out << ",";
                }
                out << "\n";
            }
            if (fields.empty()) {
                out << "        std::tuple<>{}\n";
            }
            out << "    );\n";
            out << "};\n\n";
        };

        writeDescriptor(true);
        writeDescriptor(false);
    }

    out << "} // namespace fbpp::core\n";
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

        std::vector<QuerySpec> querySpecs;
        for (auto it = jsonQueries.begin(); it != jsonQueries.end(); ++it) {
            if (!it.value().is_string()) {
                throw std::runtime_error("Query text for '" + it.key() + "' must be a string.");
            }

            QuerySpec spec;
            spec.name = it.key();
            spec.originalSql = it.value().get<std::string>();

            auto parseResult = NamedParamParser::parse(spec.originalSql);
            spec.preparedSql = parseResult.hasNamedParams ? parseResult.convertedSql : spec.originalSql;

            auto meta = connection.describeQuery(spec.originalSql);
            spec.inputs = buildFieldSpecs(meta.inputFields, false);
            spec.outputs = buildFieldSpecs(meta.outputFields, true);

            querySpecs.push_back(std::move(spec));
        }

        std::sort(querySpecs.begin(), querySpecs.end(),
                  [](const QuerySpec& a, const QuerySpec& b) { return a.name < b.name; });

        std::filesystem::create_directories(opts.outputHeader.parent_path());
        std::filesystem::create_directories(opts.supportHeader.parent_path());

        auto mainHeaderName = opts.outputHeader.filename().string();

        writeMainHeader(opts.outputHeader, "GENERATED_QUERIES_HPP", mainHeaderName, querySpecs);
        writeSupportHeader(opts.supportHeader, mainHeaderName, querySpecs);

        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "Firebird exception: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

