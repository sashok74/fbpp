#include "fbpp/schema/type_mapper.hpp"

#include "fbpp/core/firebird_compat.hpp"

#include <format>
#include <stdexcept>
#include <string>

namespace fbpp::schema {

CppTypeInfo TypeMapper::mapField(const fbpp::core::FieldInfo& field,
                                 bool isOutput,
                                 const AdapterConfig& config) {
    const auto baseType = static_cast<unsigned>(field.type);
    const bool fieldIsNullable = (baseType & 1U) != 0U;
    const auto sqlType = baseType & ~1U;
    bool nullable = field.nullable || (isOutput && fieldIsNullable);

    CppTypeInfo result;
    switch (sqlType) {
        case SQL_SHORT:
            result.cppType = "std::int16_t";
            break;
        case SQL_LONG:
            if (field.scale < 0) {
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<1, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = CppTypeInfo::ScaledNumericInfo{
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
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<1, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = CppTypeInfo::ScaledNumericInfo{
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
                if (config.useTTMathNumeric) {
                    result.cppType = std::format("fbpp::adapters::TTNumeric<2, {}>", field.scale);
                    result.needsTTMath = true;
                    result.scaledInfo = CppTypeInfo::ScaledNumericInfo{
                        .intWords = 2,
                        .scale = static_cast<std::int16_t>(field.scale)
                    };
                } else {
                    result.cppType = "fbpp::core::Int128";
                    result.needsExtendedTypes = true;
                }
            } else {
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
                result.cppType = "fbpp::core::ZonedTimestamp";
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
            result.cppType = "fbpp::core::TimeTz";
            result.needsExtendedTypes = true;
            break;
        case SQL_BLOB:
            if (field.subType == 1) {
                if (config.useStringForTextBlob) {
                    result.cppType = "std::string";
                    result.needsString = true;
                } else {
                    result.cppType = "fbpp::core::TextBlob";
                    result.needsExtendedTypes = true;
                }
            } else {
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

} // namespace fbpp::schema
