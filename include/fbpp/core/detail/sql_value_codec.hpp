#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp_util/trace.h"
#include "fbpp/core/detail/conversion_utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <iomanip>
#include <sstream>

namespace fbpp::core::detail::sql_value_codec {

struct SqlWriteContext {
    const FieldInfo* field;
    Transaction* transaction;
    int16_t* nullIndicator;
};

struct SqlReadContext {
    const FieldInfo* field;
    Transaction* transaction;
    const int16_t* nullIndicator;
};

inline void setNull(int16_t* ptr) {
    if (ptr) {
        *ptr = -1;
    }
}

inline void setNotNull(int16_t* ptr) {
    if (ptr) {
        *ptr = 0;
    }
}

inline bool isNull(const int16_t* ptr) {
    return ptr && *ptr == -1;
}

inline bool isTextBlob(const FieldInfo* field) {
    return field && field->type == SQL_BLOB && field->subType == 1;
}

inline ISC_QUAD createBlob(Transaction* transaction, const std::vector<uint8_t>& data) {
    if (!transaction) {
        ISC_QUAD nullId{};
        nullId.gds_quad_high = 0;
        nullId.gds_quad_low = 0;
        return nullId;
    }
    return transaction->createBlob(data);
}

inline int64_t pow10_int(int scale) {
    int64_t result = 1;
    for (int i = 0; i < scale; ++i) {
        result *= 10;
    }
    return result;
}

inline int64_t round_scaled(double value) {
    if (value >= 0.0) {
        return static_cast<int64_t>(value + 0.5);
    }
    return static_cast<int64_t>(value - 0.5);
}

// Expand scientific notation to plain decimal string (accuracy limited by provided digits)
inline std::string normalize_scientific(const std::string& str) {
    auto ePos = str.find_first_of("eE");
    if (ePos == std::string::npos) {
        return str;
    }

    std::string mantissa = str.substr(0, ePos);
    int exp = std::stoi(str.substr(ePos + 1));

    bool negative = false;
    if (!mantissa.empty() && (mantissa[0] == '+' || mantissa[0] == '-')) {
        negative = mantissa[0] == '-';
        mantissa.erase(mantissa.begin());
    }

    auto dotPos = mantissa.find('.');
    int fracDigits = 0;
    if (dotPos != std::string::npos) {
        fracDigits = static_cast<int>(mantissa.size() - dotPos - 1);
        mantissa.erase(dotPos, 1);
    }

    int newDotPos = static_cast<int>(mantissa.size()) - fracDigits + exp;
    if (newDotPos < 0) {
        mantissa.insert(mantissa.begin(), static_cast<size_t>(-newDotPos), '0');
        newDotPos = 0;
    } else if (newDotPos > static_cast<int>(mantissa.size())) {
        mantissa.append(static_cast<size_t>(newDotPos - static_cast<int>(mantissa.size())), '0');
    }

    if (newDotPos == 0) {
        mantissa.insert(mantissa.begin(), {'0', '.'});
    } else if (newDotPos < static_cast<int>(mantissa.size())) {
        mantissa.insert(mantissa.begin() + newDotPos, '.');
    }

    // Trim leading zeros (keep one before dot)
    auto nonZero = mantissa.find_first_not_of('0');
    if (nonZero != std::string::npos && mantissa[nonZero] != '.') {
        mantissa.erase(0, nonZero);
    } else if (nonZero != std::string::npos && mantissa[nonZero] == '.') {
        // keep one zero before dot
        mantissa.erase(0, nonZero - 1);
    }

    if (mantissa.empty()) {
        mantissa = "0";
    }
    if (negative && mantissa != "0") {
        mantissa.insert(mantissa.begin(), '-');
    }
    return mantissa;
}

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const T& value, uint8_t* dataPtr);

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const std::optional<T>& value, uint8_t* dataPtr) {
    if (!value.has_value()) {
        setNull(ctx.nullIndicator);
        return;
    }
    write_sql_value(SqlWriteContext{ctx.field, ctx.transaction, ctx.nullIndicator}, *value, dataPtr);
}

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const T& value, uint8_t* dataPtr) {
    using ValueType = std::decay_t<T>;

    if constexpr (has_type_adapter_v<ValueType>) {
        if constexpr (adapter_has_ctx_to_from_v<ValueType>) {
            int16_t fbScale = ctx.field ? ctx.field->scale : 0;
            unsigned fbLength = ctx.field ? ctx.field->length : sizeof(ValueType);
            adapt_to_firebird_ctx(value, fbScale, fbLength, dataPtr);
            setNotNull(ctx.nullIndicator);
            return;
        } else if constexpr (adapter_has_basic_to_from_v<ValueType>) {
            auto fbValue = adapt_to_firebird(value);
            write_sql_value(SqlWriteContext{ctx.field, ctx.transaction, ctx.nullIndicator}, fbValue, dataPtr);
            return;
        }
    }

    if constexpr (std::is_same_v<ValueType, double>) {
        if (ctx.field && ctx.field->scale < 0) {
            const int scale = -ctx.field->scale;
            const int64_t factor = pow10_int(scale);
            int64_t scaled = round_scaled(value * static_cast<double>(factor));
            if (ctx.field->type == SQL_SHORT || ctx.field->type == (SQL_SHORT | 1)) {
                int16_t v = static_cast<int16_t>(scaled);
                std::memcpy(dataPtr, &v, sizeof(int16_t));
            } else if (ctx.field->type == SQL_LONG || ctx.field->type == (SQL_LONG | 1)) {
                int32_t v = static_cast<int32_t>(scaled);
                std::memcpy(dataPtr, &v, sizeof(int32_t));
            } else {
                std::memcpy(dataPtr, &scaled, sizeof(int64_t));
            }
            setNotNull(ctx.nullIndicator);
            return;
        }
    } else if constexpr (std::is_same_v<ValueType, std::string> || std::is_same_v<ValueType, const char*>) {
        std::string strValue;
        if constexpr (std::is_same_v<ValueType, const char*>) {
            if (value) strValue = value;
        } else {
            strValue = value;
        }

        if (isTextBlob(ctx.field)) {
            std::vector<uint8_t> textData(strValue.begin(), strValue.end());
            ISC_QUAD blobId = createBlob(ctx.transaction, textData);
            std::memcpy(dataPtr, &blobId, sizeof(ISC_QUAD));
        } else if (ctx.field && (ctx.field->type == SQL_TEXT || ctx.field->type == SQL_VARYING)) {
            if (ctx.field->type == SQL_TEXT) {
                size_t copyLen = std::min(strValue.size(), static_cast<size_t>(ctx.field->length));
                std::memcpy(dataPtr, strValue.data(), copyLen);
                if (copyLen < ctx.field->length) {
                    std::memset(dataPtr + copyLen, ' ', ctx.field->length - copyLen);
                }
            } else { // SQL_VARYING
                unsigned maxDataLength = ctx.field->length - sizeof(uint16_t);
                size_t actualLen = strValue.length();
                if (actualLen > maxDataLength) {
                    // Truncate
                    actualLen = maxDataLength;
                }
                uint16_t len = static_cast<uint16_t>(actualLen);
                std::memcpy(dataPtr, &len, sizeof(uint16_t));
                std::memcpy(dataPtr + sizeof(uint16_t), strValue.data(), actualLen);
            }
        } else if (ctx.field) {
            // Handle extended types conversion from string
            auto& env = Environment::getInstance();
            Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
            Firebird::IUtil* util = env.getUtil();

            switch (ctx.field->type) {
                case SQL_INT128: {
                    auto int128Helper = util->getInt128(&status);
                    FB_I128 fb128;
                    int128Helper->fromString(&status, ctx.field->scale, strValue.c_str(), &fb128);
                    std::memcpy(dataPtr, &fb128, 16);
                    break;
                }
                case SQL_DEC16: {
                    auto decFloat16Helper = util->getDecFloat16(&status);
                    FB_DEC16 fb16;
                    decFloat16Helper->fromString(&status, strValue.c_str(), &fb16);
                    std::memcpy(dataPtr, &fb16, 8);
                    break;
                }
                case SQL_DEC34: {
                    auto decFloat34Helper = util->getDecFloat34(&status);
                    FB_DEC34 fb34;
                    decFloat34Helper->fromString(&status, strValue.c_str(), &fb34);
                    std::memcpy(dataPtr, &fb34, 16);
                    break;
                }
                case SQL_TIMESTAMP: {
                    unsigned year, month, day, hours, minutes, seconds, fractions;
                    // Try to parse ISO format first
                    try {
                        if (strValue.length() >= 19 && strValue[10] == 'T') {
                             parseIsoDate(strValue.substr(0, 10), year, month, day);
                             parseIsoTime(strValue.substr(11), hours, minutes, seconds, fractions);
                        } else {
                             // Fallback or other formats? For now assume ISO or throw
                             throw FirebirdException("Expected ISO timestamp format YYYY-MM-DDTHH:MM:SS");
                        }
                    } catch (...) {
                        throw FirebirdException("Invalid timestamp format: " + strValue);
                    }
                    ISC_DATE date = util->encodeDate(year, month, day);
                    ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
                    std::memcpy(dataPtr, &date, 4);
                    std::memcpy(dataPtr + 4, &time, 4);
                    break;
                }
                case SQL_TIMESTAMP_TZ: {
                    unsigned year, month, day, hours, minutes, seconds, fractions;
                    size_t tzPos = strValue.find_last_of("+-");
                    if (tzPos == std::string::npos || tzPos < 19) {
                        throw FirebirdException("TIMESTAMP_TZ requires timezone offset: " + strValue);
                    }
                    std::string dtStr = strValue.substr(0, tzPos);
                    std::string tzStr = strValue.substr(tzPos);
                    
                    parseIsoDate(dtStr.substr(0, 10), year, month, day);
                    parseIsoTime(dtStr.substr(11), hours, minutes, seconds, fractions);
                    
                    ISC_DATE date = util->encodeDate(year, month, day);
                    ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
                    int16_t offset = parseTimezoneOffset(tzStr);
                    uint16_t zoneId = static_cast<uint16_t>(offset); // fallback: mirror offset into zone id for offset-only usage
                    
                    std::memcpy(dataPtr, &date, 4);
                    std::memcpy(dataPtr + 4, &time, 4);
                    std::memcpy(dataPtr + 8, &zoneId, 2);
                    std::memcpy(dataPtr + 10, &offset, 2);
                    break;
                }
                case SQL_TYPE_TIME: {
                    unsigned hours, minutes, seconds, fractions;
                    parseIsoTime(strValue, hours, minutes, seconds, fractions);
                    ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
                    std::memcpy(dataPtr, &time, 4);
                    break;
                }
                case SQL_TIME_TZ: {
                    size_t tzPos = strValue.find_last_of("+-");
                    if (tzPos == std::string::npos || tzPos < 8) {
                        throw FirebirdException("TIME_TZ requires timezone offset: " + strValue);
                    }
                    std::string timeStr = strValue.substr(0, tzPos);
                    std::string tzStr = strValue.substr(tzPos);
                    
                    unsigned hours, minutes, seconds, fractions;
                    parseIsoTime(timeStr, hours, minutes, seconds, fractions);
                    
                    ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
                    int16_t offset = parseTimezoneOffset(tzStr);
                    uint16_t zoneId = static_cast<uint16_t>(offset); // store offset in zone id as fallback
                    
                    std::memcpy(dataPtr, &time, 4);
                    std::memcpy(dataPtr + 4, &zoneId, 2);
                    std::memcpy(dataPtr + 6, &offset, 2);
                    break;
                }
                case SQL_TYPE_DATE: {
                    unsigned year, month, day;
                    parseIsoDate(strValue, year, month, day);
                    ISC_DATE date = util->encodeDate(year, month, day);
                    std::memcpy(dataPtr, &date, 4);
                    break;
                }
                case SQL_SHORT: {
                    int64_t val = string_to_decimal_i64(strValue, ctx.field->scale);
                    if (val > INT16_MAX || val < INT16_MIN) throw FirebirdException("Value out of range for SMALLINT");
                    int16_t v = static_cast<int16_t>(val);
                    std::memcpy(dataPtr, &v, 2);
                    break;
                }
                case SQL_LONG: {
                    int64_t val = string_to_decimal_i64(strValue, ctx.field->scale);
                    if (val > INT32_MAX || val < INT32_MIN) throw FirebirdException("Value out of range for INTEGER");
                    int32_t v = static_cast<int32_t>(val);
                    std::memcpy(dataPtr, &v, 4);
                    break;
                }
                case SQL_INT64: {
                    int64_t val = string_to_decimal_i64(strValue, ctx.field->scale);
                    std::memcpy(dataPtr, &val, 8);
                    break;
                }
                case SQL_FLOAT: {
                    float val = std::stof(strValue);
                    std::memcpy(dataPtr, &val, 4);
                    break;
                }
                case SQL_DOUBLE:
                case SQL_D_FLOAT: {
                    double val = std::stod(strValue);
                    std::memcpy(dataPtr, &val, 8);
                    break;
                }
                case SQL_BOOLEAN: {
                    // Simple boolean parsing
                    bool val = (strValue == "true" || strValue == "1" || strValue == "TRUE");
                    uint8_t b = val ? 1 : 0;
                    std::memcpy(dataPtr, &b, 1);
                    break;
                }
                default:
                    throw FirebirdException("Unsupported type for string conversion: " + std::to_string(ctx.field->type));
            }
        }
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        uint8_t boolVal = value ? 1 : 0;
        std::memcpy(dataPtr, &boolVal, 1);
    } else if constexpr (std::is_same_v<ValueType, Int128>) {
        std::memcpy(dataPtr, value.data(), 16);
    } else if constexpr (std::is_same_v<ValueType, DecFloat16>) {
        std::memcpy(dataPtr, value.data(), 8);
    } else if constexpr (std::is_same_v<ValueType, DecFloat34>) {
        std::memcpy(dataPtr, value.data(), 16);
    } else if constexpr (std::is_same_v<ValueType, Timestamp>) {
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        std::memcpy(dataPtr, &date, 4);
        std::memcpy(dataPtr + 4, &time, 4);
    } else if constexpr (std::is_same_v<ValueType, TimestampTz>) {
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(dataPtr, &date, 4);
        std::memcpy(dataPtr + 4, &time, 4);
        std::memcpy(dataPtr + 8, &zone, 2);
        std::memcpy(dataPtr + 10, &offset, 2);
    } else if constexpr (std::is_same_v<ValueType, Time>) {
        uint32_t time = value.getTime();
        std::memcpy(dataPtr, &time, 4);
    } else if constexpr (std::is_same_v<ValueType, TimeTz>) {
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(dataPtr, &time, 4);
        std::memcpy(dataPtr + 4, &zone, 2);
        std::memcpy(dataPtr + 6, &offset, 2);
    } else if constexpr (std::is_same_v<ValueType, Blob>) {
        std::memcpy(dataPtr, value.getId(), 8);
    } else if constexpr (std::is_same_v<ValueType, TextBlob>) {
        std::memcpy(dataPtr, value.getId(), 8);
    } else {
        std::memcpy(dataPtr, &value, sizeof(ValueType));
    }

    setNotNull(ctx.nullIndicator);
}

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, T& value);

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, std::optional<T>& value) {
    if (isNull(ctx.nullIndicator)) {
        value.reset();
        return;
    }

    T tmp{};
    read_sql_value(SqlReadContext{ctx.field, ctx.transaction, ctx.nullIndicator}, dataPtr, tmp);
    value = std::move(tmp);
}

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, T& value) {
    using ValueType = std::decay_t<T>;

    if (isNull(ctx.nullIndicator)) {
        std::string fieldName = ctx.field ? ctx.field->name : "<unknown>";
        throw FirebirdException("NULL value for non-nullable field: " + fieldName);
    }

    if constexpr (has_type_adapter_v<ValueType>) {
        if constexpr (adapter_has_ctx_to_from_v<ValueType>) {
            int16_t fbScale = ctx.field ? ctx.field->scale : 0;
            unsigned fbLength = ctx.field ? ctx.field->length : sizeof(ValueType);
            value = adapt_from_firebird_ctx<ValueType>(dataPtr, fbLength, fbScale);
            return;
        } else if constexpr (adapter_has_basic_to_from_v<ValueType>) {
            using firebird_type = firebird_equivalent_t<ValueType>;
            firebird_type fbValue{};
            read_sql_value(SqlReadContext{ctx.field, ctx.transaction, ctx.nullIndicator}, dataPtr, fbValue);
            value = adapt_from_firebird<ValueType>(fbValue);
            return;
        }
    }

    if constexpr (std::is_same_v<ValueType, double>) {
        if (ctx.field && ctx.field->scale < 0) {
            const int scale = -ctx.field->scale;
            int64_t raw = 0;
            if (ctx.field->type == SQL_SHORT || ctx.field->type == (SQL_SHORT | 1)) {
                int16_t v{};
                std::memcpy(&v, dataPtr, sizeof(int16_t));
                raw = v;
            } else if (ctx.field->type == SQL_LONG || ctx.field->type == (SQL_LONG | 1)) {
                int32_t v{};
                std::memcpy(&v, dataPtr, sizeof(int32_t));
                raw = v;
            } else {
                std::memcpy(&raw, dataPtr, sizeof(int64_t));
            }
            const int64_t factor = pow10_int(scale);
            value = static_cast<double>(raw) / static_cast<double>(factor);
            return;
        }
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
        if (isTextBlob(ctx.field) && ctx.transaction) {
            ISC_QUAD blobId{};
            std::memcpy(&blobId, dataPtr, sizeof(ISC_QUAD));
            auto blobData = ctx.transaction->loadBlob(&blobId);
            if (!blobData.empty()) {
                value.assign(reinterpret_cast<const char*>(blobData.data()), blobData.size());
            } else {
                value.clear();
            }
        } else if (ctx.field && (ctx.field->type == SQL_TEXT || ctx.field->type == SQL_VARYING)) {
             if (ctx.field->type == SQL_VARYING) {
                uint16_t len{};
                std::memcpy(&len, dataPtr, sizeof(uint16_t));
                value.assign(reinterpret_cast<const char*>(dataPtr + sizeof(uint16_t)), len);
            } else {
                // SQL_TEXT
                value.assign(reinterpret_cast<const char*>(dataPtr), ctx.field->length);
                // Trim trailing spaces
                value.erase(value.find_last_not_of(' ') + 1);
            }
        } else if (ctx.field) {
            // Handle extended types conversion to string
            auto& env = Environment::getInstance();
            Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
            Firebird::IUtil* util = env.getUtil();
            char buffer[128]; // Buffer for string conversions

            switch (ctx.field->type) {
                case SQL_INT128: {
                    auto int128Helper = util->getInt128(&status);
                    FB_I128 fb128;
                    std::memcpy(&fb128, dataPtr, 16);
                    int128Helper->toString(&status, &fb128, ctx.field->scale, sizeof(buffer), buffer);
                    value = buffer;
                    break;
                }
                case SQL_DEC16: {
                    auto decFloat16Helper = util->getDecFloat16(&status);
                    FB_DEC16 fb16;
                    std::memcpy(&fb16, dataPtr, 8);
                    decFloat16Helper->toString(&status, &fb16, sizeof(buffer), buffer);
                    value = normalize_scientific(buffer);
                    break;
                }
                case SQL_DEC34: {
                    auto decFloat34Helper = util->getDecFloat34(&status);
                    FB_DEC34 fb34;
                    std::memcpy(&fb34, dataPtr, 16);
                    decFloat34Helper->toString(&status, &fb34, sizeof(buffer), buffer);
                    value = normalize_scientific(buffer);
                    break;
                }
                case SQL_TIMESTAMP: {
                    ISC_TIMESTAMP timestamp;
                    std::memcpy(&timestamp.timestamp_date, dataPtr, 4);
                    std::memcpy(&timestamp.timestamp_time, dataPtr + 4, 4);
                    unsigned year, month, day, hours, minutes, seconds, fractions;
                    util->decodeDate(timestamp.timestamp_date, &year, &month, &day);
                    util->decodeTime(timestamp.timestamp_time, &hours, &minutes, &seconds, &fractions);
                    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%04u",
                                  year, month, day, hours, minutes, seconds, fractions);
                    value = buffer;
                    break;
                }
                case SQL_TIMESTAMP_TZ: {
                    ISC_TIMESTAMP utcTimestamp{};
                    std::memcpy(&utcTimestamp.timestamp_date, dataPtr, 4);
                    std::memcpy(&utcTimestamp.timestamp_time, dataPtr + 4, 4);
                    int16_t offset{};
                    std::memcpy(&offset, dataPtr + 10, 2);
                    if (offset == 0) {
                        uint16_t zoneRaw{};
                        std::memcpy(&zoneRaw, dataPtr + 8, 2);
                        offset = static_cast<int16_t>(zoneRaw); // fallback if server echoed offset in zone field
                    }
                    unsigned year, month, day, hours, minutes, seconds, fractions;
                    util->decodeDate(utcTimestamp.timestamp_date, &year, &month, &day);
                    util->decodeTime(utcTimestamp.timestamp_time, &hours, &minutes, &seconds, &fractions);
                    int offset_hours = offset / 60;
                    int offset_minutes = std::abs(offset) % 60;
                    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%04u%+03d:%02d",
                                  year, month, day, hours, minutes, seconds, fractions, offset_hours, offset_minutes);
                    value = buffer;
                    break;
                }
                case SQL_TYPE_TIME: {
                    ISC_TIME time;
                    std::memcpy(&time, dataPtr, 4);
                    unsigned hours, minutes, seconds, fractions;
                    util->decodeTime(time, &hours, &minutes, &seconds, &fractions);
                    std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%04u",
                                  hours, minutes, seconds, fractions);
                    value = buffer;
                    break;
                }
                case SQL_TIME_TZ: {
                    ISC_TIME utcTime{};
                    std::memcpy(&utcTime, dataPtr, 4);
                    int16_t offset{};
                    std::memcpy(&offset, dataPtr + 6, 2);
                    if (offset == 0) {
                        uint16_t zoneRaw{};
                        std::memcpy(&zoneRaw, dataPtr + 4, 2);
                        offset = static_cast<int16_t>(zoneRaw);
                    }
                    unsigned hours, minutes, seconds, fractions;
                    util->decodeTime(utcTime, &hours, &minutes, &seconds, &fractions);
                    int offset_hours = offset / 60;
                    int offset_minutes = std::abs(offset) % 60;
                    std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%04u%+03d:%02d",
                                  hours, minutes, seconds, fractions, offset_hours, offset_minutes);
                    value = buffer;
                    break;
                }
                case SQL_TYPE_DATE: {
                    ISC_DATE date;
                    std::memcpy(&date, dataPtr, 4);
                    unsigned year, month, day;
                    util->decodeDate(date, &year, &month, &day);
                    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", year, month, day);
                    value = buffer;
                    break;
                }
                case SQL_SHORT: {
                    int16_t v;
                    std::memcpy(&v, dataPtr, 2);
                    if (ctx.field->scale < 0) {
                        value = decimal_to_string_i64(v, ctx.field->scale);
                    } else {
                        value = std::to_string(v);
                    }
                    break;
                }
                case SQL_LONG: {
                    int32_t v;
                    std::memcpy(&v, dataPtr, 4);
                    if (ctx.field->scale < 0) {
                        value = decimal_to_string_i64(v, ctx.field->scale);
                    } else {
                        value = std::to_string(v);
                    }
                    break;
                }
                case SQL_INT64: {
                    int64_t v;
                    std::memcpy(&v, dataPtr, 8);
                    if (ctx.field->scale < 0) {
                        value = decimal_to_string_i64(v, ctx.field->scale);
                    } else {
                        value = std::to_string(v);
                    }
                    break;
                }
                case SQL_FLOAT: {
                    float v;
                    std::memcpy(&v, dataPtr, 4);
                    value = std::to_string(v);
                    break;
                }
                case SQL_DOUBLE:
                case SQL_D_FLOAT: {
                    double v;
                    std::memcpy(&v, dataPtr, 8);
                    value = std::to_string(v);
                    break;
                }
                case SQL_BOOLEAN: {
                    uint8_t b;
                    std::memcpy(&b, dataPtr, 1);
                    value = b ? "true" : "false";
                    break;
                }
                default:
                     throw FirebirdException("Unsupported type for string conversion: " + std::to_string(ctx.field->type));
            }
        }
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        uint8_t boolVal{};
        std::memcpy(&boolVal, dataPtr, 1);
        value = boolVal != 0;
    } else if constexpr (std::is_same_v<ValueType, Int128>) {
        value = Int128(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, DecFloat16>) {
        value = DecFloat16(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, DecFloat34>) {
        value = DecFloat34(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, Timestamp>) {
        uint32_t date{};
        uint32_t time{};
        std::memcpy(&date, dataPtr, 4);
        std::memcpy(&time, dataPtr + 4, 4);
        value = Timestamp(date, time);
    } else if constexpr (std::is_same_v<ValueType, TimestampTz>) {
        uint32_t date{};
        uint32_t time{};
        uint16_t zone{};
        int16_t offset{};
        std::memcpy(&date, dataPtr, 4);
        std::memcpy(&time, dataPtr + 4, 4);
        std::memcpy(&zone, dataPtr + 8, 2);
        std::memcpy(&offset, dataPtr + 10, 2);
        value = TimestampTz(date, time, zone, offset);
    } else if constexpr (std::is_same_v<ValueType, Time>) {
        uint32_t time{};
        std::memcpy(&time, dataPtr, 4);
        value = Time(time);
    } else if constexpr (std::is_same_v<ValueType, TimeTz>) {
        uint32_t time{};
        uint16_t zone{};
        int16_t offset{};
        std::memcpy(&time, dataPtr, 4);
        std::memcpy(&zone, dataPtr + 4, 2);
        std::memcpy(&offset, dataPtr + 6, 2);
        value = TimeTz(time, zone, offset);
    } else if constexpr (std::is_same_v<ValueType, Blob>) {
        value = Blob(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, TextBlob>) {
        value = TextBlob(dataPtr);
    } else {
        std::memcpy(&value, dataPtr, sizeof(ValueType));
    }
}

} // namespace fbpp::core::detail::sql_value_codec
