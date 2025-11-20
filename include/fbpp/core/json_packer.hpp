#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <iomanip>
#include "fbpp/core/detail/conversion_utils.hpp"

namespace fbpp::core {
namespace detail {

// Helper to pack a single JSON value into buffer
inline void packJsonValue(const nlohmann::json& value, uint8_t* buffer,
                          const FieldInfo& field, ::fbpp::core::Transaction* transaction) {
    
    uint8_t* data_ptr = buffer + field.offset;
    int16_t* null_ptr = reinterpret_cast<int16_t*>(buffer + field.nullOffset);
    
    // Check NULL
    if (value.is_null()) {
        *null_ptr = -1;  // NULL indicator
        return;
    }
    
    *null_ptr = 0;  // NOT NULL
    
    // Initialize Firebird utilities (will be used for extended types and date/time)
    auto& env = Environment::getInstance();
    Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
    Firebird::IUtil* util = env.getUtil();
    
    // Pack value based on field type - following same order as tuple_packer
    switch (field.type) {
        case SQL_TEXT:
        case SQL_VARYING: {
            if (!value.is_string()) {
                throw FirebirdException("Expected string for VARCHAR/CHAR field");
            }
            
            std::string str = value.get<std::string>();
            
            if (field.type == SQL_VARYING) {
                // VARCHAR - first 2 bytes are length
                unsigned maxLength = field.length - sizeof(uint16_t);
                size_t actualLen = str.length();
                if (actualLen > maxLength) {
                    actualLen = maxLength;
                    // TODO: Add warning about truncation
                }
                uint16_t len = static_cast<uint16_t>(actualLen);
                std::memcpy(data_ptr, &len, sizeof(uint16_t));
                std::memcpy(data_ptr + sizeof(uint16_t), str.data(), actualLen);
            } else {
                // SQL_TEXT (CHAR) - fixed length, space-padded
                size_t copyLen = std::min(str.length(), static_cast<size_t>(field.length));
                std::memcpy(data_ptr, str.data(), copyLen);
                // Pad with spaces if needed
                if (copyLen < field.length) {
                    std::memset(data_ptr + copyLen, ' ', field.length - copyLen);
                }
            }
            break;
        }
        
        case SQL_BOOLEAN: {
            if (!value.is_boolean()) {
                throw FirebirdException("Expected boolean for BOOLEAN field");
            }
            uint8_t bool_val = value.get<bool>() ? 1 : 0;
            std::memcpy(data_ptr, &bool_val, 1);
            break;
        }
        
        case SQL_INT128: {
            // Int128 - parse from string using Firebird API
            if (!value.is_string()) {
                throw FirebirdException("Expected string for INT128 field");
            }
            
            auto int128Helper = util->getInt128(&status);
            FB_I128 fb128;
            
            std::string str = value.get<std::string>();
            // fromString requires scale parameter (use field.scale for NUMERIC/DECIMAL)
            int128Helper->fromString(&status, field.scale, str.c_str(), &fb128);
            
            std::memcpy(data_ptr, &fb128, 16);
            break;
        }
        
        case SQL_DEC16: {
            // DecFloat16 - parse from string using Firebird API
            if (!value.is_string() && !value.is_number()) {
                throw FirebirdException("Expected string or number for DECFLOAT16 field");
            }
            
            auto decFloat16Helper = util->getDecFloat16(&status);
            FB_DEC16 fb16;
            
            if (value.is_string()) {
                std::string str = value.get<std::string>();
                decFloat16Helper->fromString(&status, str.c_str(), &fb16);
            } else {
                // Convert number to string first
                std::ostringstream oss;
                oss << std::setprecision(16) << value.get<double>();
                decFloat16Helper->fromString(&status, oss.str().c_str(), &fb16);
            }
            
            std::memcpy(data_ptr, &fb16, 8);
            break;
        }
        
        case SQL_DEC34: {
            // DecFloat34 - parse from string using Firebird API
            if (!value.is_string() && !value.is_number()) {
                throw FirebirdException("Expected string or number for DECFLOAT34 field");
            }
            
            auto decFloat34Helper = util->getDecFloat34(&status);
            FB_DEC34 fb34;
            
            if (value.is_string()) {
                std::string str = value.get<std::string>();
                decFloat34Helper->fromString(&status, str.c_str(), &fb34);
            } else {
                // Convert number to string first
                std::ostringstream oss;
                oss << std::setprecision(34) << value.get<double>();
                decFloat34Helper->fromString(&status, oss.str().c_str(), &fb34);
            }
            
            std::memcpy(data_ptr, &fb34, 16);
            break;
        }
        
        case SQL_TIMESTAMP: {
            // Timestamp - parse ISO string and encode using Firebird API
            if (!value.is_string()) {
                throw FirebirdException("Expected ISO datetime string for TIMESTAMP field");
            }
            
            std::string str = value.get<std::string>();
            
            // Parse "YYYY-MM-DDTHH:MM:SS[.fraction]"
            if (str.length() < 19 || str[10] != 'T') {
                throw FirebirdException("Invalid timestamp format, expected YYYY-MM-DDTHH:MM:SS: " + str);
            }
            
            unsigned year, month, day, hours, minutes, seconds, fractions;
            parseIsoDate(str.substr(0, 10), year, month, day);
            parseIsoTime(str.substr(11), hours, minutes, seconds, fractions);
            
            ISC_DATE date = util->encodeDate(year, month, day);
            ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
            
            std::memcpy(data_ptr, &date, 4);
            std::memcpy(data_ptr + 4, &time, 4);
            break;
        }
        
        case SQL_TIMESTAMP_TZ: {
            // TimestampTz - parse ISO string with timezone
            if (!value.is_string()) {
                throw FirebirdException("Expected ISO datetime string with timezone for TIMESTAMP_TZ field");
            }
            
            std::string str = value.get<std::string>();
            
            // Parse "YYYY-MM-DDTHH:MM:SS[.fraction][+/-HH:MM]"
            size_t tzPos = str.find_last_of("+-");
            if (tzPos == std::string::npos || tzPos < 19) {
                throw FirebirdException("TIMESTAMP_TZ requires timezone offset: " + str);
            }
            
            std::string dtStr = str.substr(0, tzPos);
            std::string tzStr = str.substr(tzPos);
            
            unsigned year, month, day, hours, minutes, seconds, fractions;
            parseIsoDate(dtStr.substr(0, 10), year, month, day);
            parseIsoTime(dtStr.substr(11), hours, minutes, seconds, fractions);
            
            ISC_DATE date = util->encodeDate(year, month, day);
            ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
            int16_t offset = parseTimezoneOffset(tzStr);
            
            std::memcpy(data_ptr, &date, 4);
            std::memcpy(data_ptr + 4, &time, 4);
            std::memcpy(data_ptr + 8, &offset, 2);
            break;
        }
        
        case SQL_TYPE_TIME: {
            // Time - parse time string and encode
            if (!value.is_string()) {
                throw FirebirdException("Expected time string for TIME field");
            }
            
            std::string str = value.get<std::string>();
            unsigned hours, minutes, seconds, fractions;
            parseIsoTime(str, hours, minutes, seconds, fractions);
            
            ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
            std::memcpy(data_ptr, &time, 4);
            break;
        }
        
        case SQL_TIME_TZ: {
            // TimeTz - parse time string with timezone
            if (!value.is_string()) {
                throw FirebirdException("Expected time string with timezone for TIME_TZ field");
            }
            
            std::string str = value.get<std::string>();
            
            // Parse "HH:MM:SS[.fraction][+/-HH:MM]"
            size_t tzPos = str.find_last_of("+-");
            if (tzPos == std::string::npos || tzPos < 8) {
                throw FirebirdException("TIME_TZ requires timezone offset: " + str);
            }
            
            std::string timeStr = str.substr(0, tzPos);
            std::string tzStr = str.substr(tzPos);
            
            unsigned hours, minutes, seconds, fractions;
            parseIsoTime(timeStr, hours, minutes, seconds, fractions);
            
            ISC_TIME time = util->encodeTime(hours, minutes, seconds, fractions);
            int16_t offset = parseTimezoneOffset(tzStr);
            
            std::memcpy(data_ptr, &time, 4);
            std::memcpy(data_ptr + 4, &offset, 2);
            break;
        }
        
        case SQL_BLOB: {
            if (field.subType == 1 && transaction) {
                // Text BLOB
                if (!value.is_string()) {
                    throw FirebirdException("Expected string for text BLOB field");
                }
                
                std::string str = value.get<std::string>();
                std::vector<uint8_t> textData(str.begin(), str.end());
                ISC_QUAD blob_id = transaction->createBlob(textData);
                std::memcpy(data_ptr, &blob_id, sizeof(ISC_QUAD));
            } else {
                // Binary BLOB - expect base64 string or null
                // For now, just throw an error for non-text BLOBs
                throw FirebirdException("Binary BLOB from JSON not yet implemented");
            }
            break;
        }
        
        case SQL_SHORT: {
            // SMALLINT - 2 bytes
            int16_t val;
            
            if (field.scale < 0) {
                // NUMERIC/DECIMAL - parse from string
                if (value.is_string()) {
                    int64_t scaled = string_to_decimal_i64(value.get<std::string>(), field.scale);
                    if (scaled > INT16_MAX || scaled < INT16_MIN) {
                        throw FirebirdException("Value out of range for SMALLINT");
                    }
                    val = static_cast<int16_t>(scaled);
                } else if (value.is_number()) {
                    // Convert number considering scale
                    double dval = value.get<double>();
                    int64_t scaled = static_cast<int64_t>(dval * std::pow(10, -field.scale));
                    if (scaled > INT16_MAX || scaled < INT16_MIN) {
                        throw FirebirdException("Value out of range for SMALLINT");
                    }
                    val = static_cast<int16_t>(scaled);
                } else {
                    throw FirebirdException("Expected number or string for NUMERIC field");
                }
            } else {
                // Regular integer
                if (!value.is_number_integer()) {
                    throw FirebirdException("Expected integer for SMALLINT field");
                }
                val = value.get<int16_t>();
            }
            
            std::memcpy(data_ptr, &val, sizeof(int16_t));
            break;
        }
        
        case SQL_LONG: {
            // INTEGER - 4 bytes
            int32_t val;
            
            if (field.scale < 0) {
                // NUMERIC/DECIMAL - parse from string
                if (value.is_string()) {
                    int64_t scaled = string_to_decimal_i64(value.get<std::string>(), field.scale);
                    if (scaled > INT32_MAX || scaled < INT32_MIN) {
                        throw FirebirdException("Value out of range for INTEGER");
                    }
                    val = static_cast<int32_t>(scaled);
                } else if (value.is_number()) {
                    // Convert number considering scale
                    double dval = value.get<double>();
                    int64_t scaled = static_cast<int64_t>(dval * std::pow(10, -field.scale));
                    if (scaled > INT32_MAX || scaled < INT32_MIN) {
                        throw FirebirdException("Value out of range for INTEGER");
                    }
                    val = static_cast<int32_t>(scaled);
                } else {
                    throw FirebirdException("Expected number or string for NUMERIC field");
                }
            } else {
                // Regular integer
                if (!value.is_number_integer()) {
                    throw FirebirdException("Expected integer for INTEGER field");
                }
                val = value.get<int32_t>();
            }
            
            std::memcpy(data_ptr, &val, sizeof(int32_t));
            break;
        }
        
        case SQL_INT64: {
            // BIGINT - 8 bytes
            int64_t val;
            
            if (field.scale < 0) {
                // NUMERIC/DECIMAL - parse from string
                if (value.is_string()) {
                    val = string_to_decimal_i64(value.get<std::string>(), field.scale);
                } else if (value.is_number()) {
                    // Convert number considering scale
                    double dval = value.get<double>();
                    val = static_cast<int64_t>(dval * std::pow(10, -field.scale));
                } else {
                    throw FirebirdException("Expected number or string for NUMERIC field");
                }
            } else {
                // Regular integer
                if (!value.is_number_integer()) {
                    throw FirebirdException("Expected integer for BIGINT field");
                }
                val = value.get<int64_t>();
            }
            
            std::memcpy(data_ptr, &val, sizeof(int64_t));
            break;
        }
        
        case SQL_FLOAT: {
            // FLOAT - 4 bytes
            if (!value.is_number()) {
                throw FirebirdException("Expected number for FLOAT field");
            }
            float val = value.get<float>();
            std::memcpy(data_ptr, &val, sizeof(float));
            break;
        }
        
        case SQL_DOUBLE:
        case SQL_D_FLOAT: {
            // DOUBLE PRECISION - 8 bytes
            if (!value.is_number()) {
                throw FirebirdException("Expected number for DOUBLE field");
            }
            double val = value.get<double>();
            std::memcpy(data_ptr, &val, sizeof(double));
            break;
        }
        
        case SQL_TYPE_DATE: {
            // DATE - parse date string and encode
            if (!value.is_string()) {
                throw FirebirdException("Expected date string for DATE field");
            }
            
            std::string str = value.get<std::string>();
            unsigned year, month, day;
            parseIsoDate(str, year, month, day);
            
            ISC_DATE date = util->encodeDate(year, month, day);
            std::memcpy(data_ptr, &date, sizeof(ISC_DATE));
            break;
        }
        
        default:
            throw FirebirdException("Unsupported SQL type for JSON packing: " + std::to_string(field.type));
    }
}

// Helper to iterate through JSON and pack to buffer
inline void packJsonToBuffer(const nlohmann::json& jsonData, uint8_t* buffer, 
                             const MessageMetadata* metadata, 
                             ::fbpp::core::Transaction* transaction) {
    unsigned fieldCount = metadata->getCount();
    
    // Check if JSON is array or object
    bool isArray = jsonData.is_array();
    bool isObject = jsonData.is_object();
    
    if (!isArray && !isObject) {
        throw FirebirdException("JSON data must be array or object");
    }
    
    if (isArray && jsonData.size() != fieldCount) {
        throw FirebirdException(
            "JSON array size mismatch: array has " + std::to_string(jsonData.size()) +
            " elements, but query expects " + std::to_string(fieldCount) + " parameters"
        );
    }
    
    // Pack each field
    for (unsigned i = 0; i < fieldCount; ++i) {
        // Get field info from metadata
        FieldInfo fieldInfo = metadata->getField(i);
        
        // Get value from JSON
        nlohmann::json value;
        
        if (isArray) {
            value = jsonData[i];
        } else if (isObject) {
            // For now, use positional index as key (string)
            // In future, this will support named parameters
            std::string key = std::to_string(i);
            if (jsonData.contains(key)) {
                value = jsonData[key];
            } else {
                // If key not found, treat as NULL
                value = nullptr;
            }
        }
        
        // Pack this field to buffer
        packJsonValue(value, buffer, fieldInfo, transaction);
    }
}

} // namespace detail

// JsonPacker implementation
class JsonPacker {
public:
    JsonPacker() = default;
    
    void pack(const nlohmann::json& jsonData, uint8_t* buffer, 
              const MessageMetadata* metadata, Transaction* transaction = nullptr) {
        if (!buffer || !metadata) {
            throw FirebirdException("Invalid parameters for JSON pack");
        }
        
        // Get buffer size from metadata
        size_t bufSize = metadata->getMessageLength();
        
        // Clear buffer first
        std::memset(buffer, 0, bufSize);
        
        // Pack JSON data to buffer
        detail::packJsonToBuffer(jsonData, buffer, metadata, transaction);
    }
    
    nlohmann::json pack(const nlohmann::json& jsonData, const MessageMetadata* metadata,
                        Transaction* transaction = nullptr) {
        // This overload doesn't make sense for packing, but keeping for API consistency
        throw FirebirdException("Use pack(json, buffer, metadata, transaction) instead");
    }
};

// Factory function for creating JSON packer
inline std::unique_ptr<JsonPacker> makeJsonPacker() {
    return std::make_unique<JsonPacker>();
}

} // namespace fbpp::core