#include "fbpp/query_generator_service.hpp"

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/named_param_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

TypeMapping mapFieldType(const FieldInfo& field, bool isOutput, const AdapterConfig& config) {
    const auto baseType = static_cast<unsigned>(field.type);
    const bool fieldIsNullable = (baseType & 1U) != 0U;
    const auto sqlType = baseType & ~1U;
    bool nullable = field.nullable || (isOutput && fieldIsNullable);

    TypeMapping result;
    switch (sqlType) {
        case SQL_SHORT:
            result.cppType = "std::int16_t";
            break;
        case SQL_LONG:
            if (field.scale < 0) {
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<1, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = TypeMapping::ScaledNumericInfo{
                        .intWords = 1,
                        .scale = static_cast<std::int16_t>(field.scale)
                    };
                } else {
                    result.cppType = "std::int32_t";
                }
            } else {
                result.cppType = "std::int32_t";
            }
            break;
        case SQL_INT64:
            if (field.scale < 0) {
                // NUMERIC(18,x) with scale
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<1, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = TypeMapping::ScaledNumericInfo{
                        .intWords = 1,
                        .scale = static_cast<std::int16_t>(field.scale)
                    };
                } else {
                    result.cppType = "std::int64_t";
                }
            } else {
                result.cppType = "std::int64_t";
            }
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
        case SQL_BOOLEAN:
            result.cppType = "bool";
            break;
        case SQL_INT128:
            if (field.scale < 0) {
                // NUMERIC(38,x) with scale
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<2, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = TypeMapping::ScaledNumericInfo{
                        .intWords = 2,
                        .scale = static_cast<std::int16_t>(field.scale)
                    };
                } else {
                    result.cppType = "fbpp::core::Int128";
                    result.needsExtendedTypes = true;
                }
            } else {
                // Pure INT128 (no scale)
                if (config.useTTMathInt128) {
                    result.cppType = "fbpp::adapters::Int128";
                    result.needsTTMath = true;
                } else {
                    result.cppType = "fbpp::core::Int128";
                    result.needsExtendedTypes = true;
                }
            }
            break;
        case SQL_DEC16:
            if (config.useCppDecimalDecFloat) {
                result.cppType = "fbpp::adapters::DecFloat16";
                result.needsCppDecimal = true;
            } else {
                result.cppType = "fbpp::core::DecFloat16";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_DEC34:
            if (config.useCppDecimalDecFloat) {
                result.cppType = "fbpp::adapters::DecFloat34";
                result.needsCppDecimal = true;
            } else {
                result.cppType = "fbpp::core::DecFloat34";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_TYPE_DATE:
            if (config.useChronoDatetime) {
                result.cppType = "std::chrono::year_month_day";
                result.needsChrono = true;
            } else {
                result.cppType = "fbpp::core::Date";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_TIMESTAMP:
            if (config.useChronoDatetime) {
                result.cppType = "std::chrono::system_clock::time_point";
                result.needsChrono = true;
            } else {
                result.cppType = "fbpp::core::Timestamp";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_TIMESTAMP_TZ:
            if (config.useChronoDatetime) {
                result.cppType = "std::chrono::zoned_time<std::chrono::microseconds>";
                result.needsChrono = true;
            } else {
                result.cppType = "fbpp::core::TimestampTz";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_TYPE_TIME:
            if (config.useChronoDatetime) {
                result.cppType = "std::chrono::hh_mm_ss<std::chrono::microseconds>";
                result.needsChrono = true;
            } else {
                result.cppType = "fbpp::core::Time";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_TIME_TZ:
            // No direct chrono equivalent - keep using core type
            result.cppType = "fbpp::core::TimeTz";
            result.needsExtendedTypes = true;
            break;
        case SQL_BLOB:
            if (field.subType == 1) {
                // Text BLOB (SUB_TYPE 1)
                if (config.useStringForTextBlob) {
                    result.cppType = "std::string";
                    result.needsString = true;
                } else {
                    result.cppType = "fbpp::core::TextBlob";
                    result.needsExtendedTypes = true;
                }
            } else {
                // Binary BLOB
                result.cppType = "fbpp::core::Blob";
                result.needsExtendedTypes = true;
            }
            break;
        case SQL_ARRAY:
        default:
            throw std::runtime_error("Unsupported SQL type: " + std::to_string(field.type));
    }

    result.needsOptional = nullable;
    if (nullable) {
        result.cppType = "std::optional<" + result.cppType + ">";
    }

    return result;
}

std::vector<FieldSpec> buildFieldSpecs(const std::vector<FieldInfo>& fields, bool isOutput, const AdapterConfig& config) {
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
        spec.type = mapFieldType(fields[i], isOutput, config);

        specs.push_back(std::move(spec));
    }
    return specs;
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

        auto parseResult = NamedParamParser::parse(spec.originalSql);
        spec.positionalSql = parseResult.hasNamedParams ? parseResult.convertedSql : spec.originalSql;
        spec.hasNamedParameters = parseResult.hasNamedParams;

        auto meta = connection_.describeQuery(spec.originalSql);
        spec.inputs = buildFieldSpecs(meta.inputFields, false, config);
        spec.outputs = buildFieldSpecs(meta.outputFields, true, config);

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
