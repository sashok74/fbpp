#include "fbpp/query_generator_service.hpp"

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/schema/type_mapper.hpp"
#include "fbpp/schema/query_analyzer.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fbpp::core {
namespace {

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

std::string escapeBracesForFormat(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        if (c == '{' || c == '}') {
            result.push_back(c);
            result.push_back(c);
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::string makeStructName(const std::string& queryName, bool isInput) {
    return queryName + (isInput ? "In" : "Out");
}

std::string renderMainHeader(const std::vector<QuerySpec>& queries,
                             std::string_view supportHeaderName,
                             const AdapterConfig& config) {
    bool needsOptional = false;
    bool needsString = false;
    bool needsExtended = false;
    bool needsTTMath = false;
    bool needsChrono = false;
    bool needsCppDecimal = false;

    // Collect unique scaled numerics for type aliases
    std::set<std::pair<int, int16_t>> ttNumericAliases; // (intWords, scale)

    for (const auto& q : queries) {
        for (const auto& f : q.inputs) {
            needsOptional |= f.type.needsOptional;
            needsString |= f.type.needsString;
            needsExtended |= f.type.needsExtendedTypes;
            needsTTMath |= f.type.needsTTMath;
            needsChrono |= f.type.needsChrono;
            needsCppDecimal |= f.type.needsCppDecimal;

            if (f.type.scaledInfo.has_value()) {
                ttNumericAliases.insert({f.type.scaledInfo->intWords, f.type.scaledInfo->scale});
            }
        }
        for (const auto& f : q.outputs) {
            needsOptional |= f.type.needsOptional;
            needsString |= f.type.needsString;
            needsExtended |= f.type.needsExtendedTypes;
            needsTTMath |= f.type.needsTTMath;
            needsChrono |= f.type.needsChrono;
            needsCppDecimal |= f.type.needsCppDecimal;

            if (f.type.scaledInfo.has_value()) {
                ttNumericAliases.insert({f.type.scaledInfo->intWords, f.type.scaledInfo->scale});
            }
        }
    }

    std::ostringstream out;

    out << "#pragma once\n\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    out << "#include <string_view>\n";
    if (needsOptional) {
        out << "#include <optional>\n";
    }
    if (needsChrono) {
        out << "#include <chrono>\n";
        out << "#include \"fbpp/adapters/chrono_datetime.hpp\"\n";
    }
    if (needsExtended) {
        out << "#include \"fbpp/core/extended_types.hpp\"\n";
    }
    if (needsTTMath) {
        out << "#include \"fbpp/adapters/ttmath_int128.hpp\"\n";
        out << "#include \"fbpp/adapters/ttmath_numeric.hpp\"\n";
    }
    if (needsCppDecimal) {
        out << "#include \"fbpp/adapters/cppdecimal_decfloat.hpp\"\n";
    }
    out << "#include \"fbpp/core/query_executor.hpp\"\n\n";

    // Generate type aliases for TTNumeric types
    if (config.generateAliases && !ttNumericAliases.empty()) {
        out << "// Type aliases for scaled numeric types\n";
        for (const auto& [intWords, scale] : ttNumericAliases) {
            int precision = (intWords == 1) ? 18 : 38;
            int decimalPlaces = -scale;
            std::string aliasName = std::format("Numeric{}_{}", precision, decimalPlaces);
            out << std::format("using {} = fbpp::adapters::TTNumeric<{}, {}>;\n",
                              aliasName, intWords, scale);
        }
        out << "\n";
    }

    out << "namespace generated::queries {\n\n";

    out << "enum class QueryId {\n";
    out << "    None";
    if (!queries.empty()) {
        out << ",\n";
        for (std::size_t i = 0; i < queries.size(); ++i) {
            out << std::format("    {}", queries[i].name);
            if (i + 1 != queries.size()) {
                out << ",\n";
            } else {
                out << '\n';
            }
        }
    } else {
        out << '\n';
    }
    out << "};\n\n";

    out << "template<QueryId Q>\nstruct QueryDescriptor;\n\n";

    for (const auto& q : queries) {
        auto writeStruct = [&](bool isInput) {
            const auto& fields = isInput ? q.inputs : q.outputs;
            std::string structName = makeStructName(q.name, isInput);
            out << std::format("struct {} {{\n", structName);
            if (fields.empty()) {
                out << "    // no fields\n";
            } else {
                for (const auto& f : fields) {
                    out << std::format("    {} {};\n", f.type.cppType, f.memberName);
                }
            }
            out << "};\n\n";
        };

        writeStruct(true);
        writeStruct(false);
    }

    for (const auto& q : queries) {
        const auto escapedName = escapeBracesForFormat(q.name);
        const auto escapedOriginal = escapeBracesForFormat(escapeString(q.originalSql));
        const auto escapedPrepared = escapeBracesForFormat(escapeString(q.positionalSql));
        out << std::format("template<>\nstruct QueryDescriptor<QueryId::{}> {{\n", q.name);
        out << std::format("    static constexpr QueryId id = QueryId::{};\n", q.name);
        out << std::format("    static constexpr std::string_view name = \"{}\";\n", escapedName);
        out << std::format("    static constexpr std::string_view sql = \"{}\";\n", escapedOriginal);
        out << std::format("    static constexpr std::string_view positionalSql = \"{}\";\n", escapedPrepared);
        out << std::format("    static constexpr bool hasNamedParameters = {};\n", q.hasNamedParameters ? "true" : "false");
        out << "    using Input = " << makeStructName(q.name, true) << ";\n";
        out << "    using Output = " << makeStructName(q.name, false) << ";\n";
        out << "};\n\n";
    }

    out << "} // namespace generated::queries\n";
    out << "\n#include \"" << supportHeaderName << "\"\n";

    return out.str();
}

std::string renderSupportHeader(const std::vector<QuerySpec>& queries) {
    std::ostringstream out;
    out << "#pragma once\n\n";
    out << "#include <tuple>\n";
    out << "#include <utility>\n";
    out << "#include \"fbpp/core/struct_pack.hpp\"\n";
    out << "#include \"fbpp/core/firebird_compat.hpp\"\n";

    out << "namespace fbpp::core {\n\n";

    for (const auto& q : queries) {
        auto writeDescriptor = [&](bool isInput) {
            const auto& fields = isInput ? q.inputs : q.outputs;
            std::string structName = "generated::queries::" + makeStructName(q.name, isInput);
            out << "template<>\nstruct StructDescriptor<" << structName << "> {\n";
            if (fields.empty()) {
                out << "    static constexpr auto fields = std::make_tuple();\n";
            } else {
                out << "    static constexpr auto fields = std::make_tuple(\n";
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    const auto& f = fields[i];
                    out << std::format(
                        "        fbpp::core::makeField<&{}::{}>(\"{}\", {}, {}, {}, {}, {})",
                        structName,
                        f.memberName,
                        escapeBracesForFormat(f.sqlName),
                        f.info.type,
                        f.info.scale,
                        f.info.length,
                        f.info.nullable ? "true" : "false",
                        f.info.subType);
                    if (i + 1 != fields.size()) {
                        out << ",\n";
                    } else {
                        out << "\n";
                    }
                }
                out << "    );\n";
            }
            out << "};\n\n";
        };

        writeDescriptor(true);
        writeDescriptor(false);
    }

    out << "} // namespace fbpp::core\n";

    return out.str();
}

} // namespace

QueryGeneratorService::QueryGeneratorService(Connection& connection)
    : connection_(connection) {}

std::vector<QuerySpec> QueryGeneratorService::buildQuerySpecs(const std::vector<QueryDefinition>& definitions,
                                                              const AdapterConfig& config) const {
    std::vector<QuerySpec> querySpecs;
    querySpecs.reserve(definitions.size());

    for (const auto& definition : definitions) {
        QuerySpec spec;
        spec.name = definition.name;
        spec.originalSql = definition.sql;

        fbpp::schema::QueryAnalyzer analyzer(connection_);
        auto analysis = analyzer.analyze(definition.sql);
        spec.positionalSql = analysis.positionalSql;
        spec.hasNamedParameters = analysis.hasNamedParameters;

        spec.inputs.reserve(analysis.inputParams.size());
        for (const auto& p : analysis.inputParams) {
            FieldSpec fs;
            fs.info = p.field;
            fs.sqlName = p.sqlName;
            fs.memberName = p.memberName;
            fs.type = fbpp::schema::TypeMapper::mapField(p.field, false, config);
            spec.inputs.push_back(std::move(fs));
        }

        spec.outputs.reserve(analysis.outputFields.size());
        for (const auto& f : analysis.outputFields) {
            FieldSpec fs;
            fs.info = f.field;
            fs.sqlName = f.sqlName;
            fs.memberName = f.memberName;
            fs.type = fbpp::schema::TypeMapper::mapField(f.field, true, config);
            spec.outputs.push_back(std::move(fs));
        }

        querySpecs.push_back(std::move(spec));
    }

    std::sort(querySpecs.begin(), querySpecs.end(),
              [](const QuerySpec& a, const QuerySpec& b) { return a.name < b.name; });

    return querySpecs;
}

std::string renderQueryGeneratorMainHeader(const std::vector<QuerySpec>& specs,
                                           std::string_view supportHeaderName,
                                           const AdapterConfig& config) {
    return renderMainHeader(specs, supportHeaderName, config);
}

std::string renderQueryGeneratorSupportHeader(const std::vector<QuerySpec>& specs,
                                              const AdapterConfig& config) {
    return renderSupportHeader(specs);
}

} // namespace fbpp::core
