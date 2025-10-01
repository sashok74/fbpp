#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/environment.hpp"
#include <firebird/Interface.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace fbpp::core {

// Forward declaration
class Transaction;

namespace detail {

inline std::string decimal_to_string_i64(int64_t v, int scaleNeg /* <0 */) {
    const uint32_t s = static_cast<uint32_t>(-scaleNeg); // число знаков после точки
    bool neg = v < 0;
    uint64_t u = neg ? uint64_t(-(v + 1)) + 1 : uint64_t(v); // модуль без UB

    static const uint64_t POW10[19] = {1ull,10ull,100ull,1000ull,10000ull,100000ull,1000000ull,10000000ull,100000000ull,1000000000ull,
        10000000000ull,100000000000ull,1000000000000ull,10000000000000ull,100000000000000ull,1000000000000000ull,10000000000000000ull,
        100000000000000000ull,1000000000000000000ull};

    if (s >= 19) throw FirebirdException("Scale too large");
    uint64_t p = POW10[s];
    uint64_t hi = u / p, lo = u % p;

    char frac[32];
    std::snprintf(frac, sizeof frac, "%0*llu", int(s), (unsigned long long)lo);

    std::string out = std::to_string(hi);
    if (s) { out.push_back('.'); out += frac; }
    if (neg) out.insert(out.begin(), '-');
    return out;
}

// Helper to unpack a single value from buffer to JSON
inline void unpackValueToJson(nlohmann::json& jsonValue, const uint8_t* data_ptr, const int16_t* null_ptr,
                              const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    // Check NULL indicator
    if (*null_ptr == -1) {
        // Value is NULL
        jsonValue = nullptr;
        return;
    }
    
    // Initialize Firebird utilities once (will be used for extended types and date/time)
    auto& env = Environment::getInstance();
    Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
    Firebird::IUtil* util = env.getUtil();
    
    // Unpack value based on type - following exact same order as tuple_unpacker
    switch (field->type) {
        case SQL_TEXT:
        case SQL_VARYING: {
            // Regular VARCHAR/CHAR - first 2 bytes are length for VARCHAR
            // For SQL_TEXT (CHAR), the length is fixed and padded with spaces
            if (field->type == SQL_VARYING) {
                // VARCHAR - first 2 bytes are actual length
                uint16_t len;
                std::memcpy(&len, data_ptr, sizeof(uint16_t));
                jsonValue = std::string(reinterpret_cast<const char*>(data_ptr + sizeof(uint16_t)), len);
            } else {
                // SQL_TEXT (CHAR) - fixed length, may be padded with spaces
                // Use field->length for the size
                std::string str(reinterpret_cast<const char*>(data_ptr), field->length);
                // Trim trailing spaces for CHAR type
                str.erase(str.find_last_not_of(' ') + 1);
                jsonValue = str;
            }
            break;
        }
        
        case SQL_BOOLEAN: {
            // Boolean is stored as 1 byte
            uint8_t bool_val;
            std::memcpy(&bool_val, data_ptr, 1);
            jsonValue = (bool_val != 0);
            break;
        }
        
        case SQL_INT128: {
            // Int128 - convert to string using Firebird API with scale
            auto int128Helper = util->getInt128(&status);
            
            FB_I128 fb128;
            std::memcpy(&fb128, data_ptr, 16);
            
            char buffer[64];
            // Pass the scale from field metadata (negative scale means decimal places)
            int128Helper->toString(&status, &fb128, field->scale, sizeof(buffer), buffer);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_DEC16: {
            // DecFloat16 - convert to string using Firebird API
            auto decFloat16Helper = util->getDecFloat16(&status);
            
            FB_DEC16 fb16;
            std::memcpy(&fb16, data_ptr, 8);
            
            char buffer[64];
            decFloat16Helper->toString(&status, &fb16, sizeof(buffer), buffer);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_DEC34: {
            // DecFloat34 - convert to string using Firebird API
            auto decFloat34Helper = util->getDecFloat34(&status);
            
            FB_DEC34 fb34;
            std::memcpy(&fb34, data_ptr, 16);
            
            char buffer[64];
            decFloat34Helper->toString(&status, &fb34, sizeof(buffer), buffer);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_TIMESTAMP: {
            // Timestamp - decode using Firebird API
            ISC_TIMESTAMP timestamp;
            std::memcpy(&timestamp.timestamp_date, data_ptr, 4);
            std::memcpy(&timestamp.timestamp_time, data_ptr + 4, 4);
            
            unsigned year, month, day, hours, minutes, seconds, fractions;
            util->decodeDate(timestamp.timestamp_date, &year, &month, &day);
            util->decodeTime(timestamp.timestamp_time, &hours, &minutes, &seconds, &fractions);
            
            char buffer[30];
            snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%04u",
                    year, month, day, hours, minutes, seconds, fractions);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_TIMESTAMP_TZ: {
            // TimestampTz - decode using Firebird API
            ISC_TIMESTAMP_TZ timestampTz;
            std::memcpy(&timestampTz.utc_timestamp.timestamp_date, data_ptr, 4);
            std::memcpy(&timestampTz.utc_timestamp.timestamp_time, data_ptr + 4, 4);
            std::memcpy(&timestampTz.time_zone, data_ptr + 8, 2);
            
            unsigned year, month, day, hours, minutes, seconds, fractions;
            util->decodeDate(timestampTz.utc_timestamp.timestamp_date, &year, &month, &day);
            util->decodeTime(timestampTz.utc_timestamp.timestamp_time, &hours, &minutes, &seconds, &fractions);
            
            // Get timezone offset in minutes
            int16_t offset = timestampTz.time_zone;
            int offset_hours = offset / 60;
            int offset_minutes = abs(offset) % 60;
            
            char buffer[40];
            snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%04u%+03d:%02d",
                    year, month, day, hours, minutes, seconds, fractions, 
                    offset_hours, offset_minutes);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_TYPE_TIME: {
            // Time - decode using Firebird API
            ISC_TIME time;
            std::memcpy(&time, data_ptr, 4);
            
            unsigned hours, minutes, seconds, fractions;
            util->decodeTime(time, &hours, &minutes, &seconds, &fractions);
            
            char buffer[20];
            snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%04u",
                    hours, minutes, seconds, fractions);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_TIME_TZ: {
            // TimeTz - decode using Firebird API
            ISC_TIME_TZ timeTz;
            std::memcpy(&timeTz.utc_time, data_ptr, 4);
            std::memcpy(&timeTz.time_zone, data_ptr + 4, 2);
            
            unsigned hours, minutes, seconds, fractions;
            util->decodeTime(timeTz.utc_time, &hours, &minutes, &seconds, &fractions);
            
            // Get timezone offset in minutes
            int16_t offset = timeTz.time_zone;
            int offset_hours = offset / 60;
            int offset_minutes = abs(offset) % 60;
            
            char buffer[30];
            snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%04u%+03d:%02d",
                    hours, minutes, seconds, fractions,
                    offset_hours, offset_minutes);
            jsonValue = std::string(buffer);
            break;
        }
        
        case SQL_BLOB: {
            if (field->subType == 1 && transaction) {
                // Text BLOB
                ISC_QUAD blob_id;
                std::memcpy(&blob_id, data_ptr, sizeof(ISC_QUAD));
                auto blobData = transaction->loadBlob(&blob_id);
                jsonValue = std::string(reinterpret_cast<const char*>(blobData.data()), blobData.size());
            } else {
                // Binary BLOB - just store the BLOB ID as string for now
                // TODO: Could load and encode to base64 if needed
                ISC_QUAD blob_id;
                std::memcpy(&blob_id, data_ptr, sizeof(ISC_QUAD));
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "BLOB_ID[%u:%u]", 
                        blob_id.gds_quad_high, blob_id.gds_quad_low);
                jsonValue = std::string(buffer);
            }
            break;
        }
        
        case SQL_SHORT: {
            // SMALLINT - 2 bytes
            int16_t value;
            std::memcpy(&value, data_ptr, sizeof(int16_t));
            
            // Check for NUMERIC/DECIMAL with scale
            if (field->scale < 0) {
                // NUMERIC/DECIMAL - use safe decimal conversion
                jsonValue = decimal_to_string_i64(static_cast<int64_t>(value), field->scale);
            } else {
                jsonValue = value;
            }
            break;
        }
        
        case SQL_LONG: {
            // INTEGER - 4 bytes
            int32_t value;
            std::memcpy(&value, data_ptr, sizeof(int32_t));
            
            // Check for NUMERIC/DECIMAL with scale
            if (field->scale < 0) {
                // NUMERIC/DECIMAL - use safe decimal conversion
                jsonValue = decimal_to_string_i64(static_cast<int64_t>(value), field->scale);
            } else {
                jsonValue = value;
            }
            break;
        }
        
        case SQL_INT64: {
            // BIGINT - 8 bytes
            int64_t value;
            std::memcpy(&value, data_ptr, sizeof(int64_t));

            // Check for NUMERIC/DECIMAL with scale
            if (field->scale < 0) {
                // NUMERIC/DECIMAL - use safe decimal conversion
                jsonValue = decimal_to_string_i64(value, field->scale);
            } else {
                jsonValue = value;
            }
            break;
        }
        
        case SQL_FLOAT: {
            // FLOAT - 4 bytes
            float value;
            std::memcpy(&value, data_ptr, sizeof(float));
            jsonValue = value;
            break;
        }
        
        case SQL_DOUBLE:
        case SQL_D_FLOAT: {
            // DOUBLE PRECISION - 8 bytes
            double value;
            std::memcpy(&value, data_ptr, sizeof(double));
            jsonValue = value;
            break;
        }
        
        case SQL_TYPE_DATE: {
            // DATE - decode using Firebird API
            ISC_DATE date;
            std::memcpy(&date, data_ptr, sizeof(ISC_DATE));
            
            unsigned year, month, day;
            util->decodeDate(date, &year, &month, &day);
            
            char buffer[11];  // YYYY-MM-DD\0
            snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", year, month, day);
            jsonValue = std::string(buffer);
            break;
        }
        
        default:
            throw FirebirdException("Unsupported SQL type for JSON unpacking: " + std::to_string(field->type));
    }
}

// Helper to iterate through all fields and unpack to JSON
inline void unpackFieldsToJson(nlohmann::json& result, const uint8_t* buffer, const MessageMetadata* metadata,
                               ::fbpp::core::Transaction* transaction = nullptr) {
    unsigned fieldCount = metadata->getCount();
    
    for (unsigned i = 0; i < fieldCount; ++i) {
        // Get field info and offsets from metadata
        FieldInfo fieldInfo = metadata->getField(i);
        unsigned data_offset = metadata->getOffset(i);
        unsigned null_offset = metadata->getNullOffset(i);
        
        const uint8_t* data_ptr = buffer + data_offset;
        const int16_t* null_ptr = reinterpret_cast<const int16_t*>(buffer + null_offset);
        
        // Get field name - prefer alias over name for better SQL compatibility
        std::string fieldName;
        if (!fieldInfo.alias.empty()) {
            // Use alias if available (e.g., "LAST_ID" from "MAX(ID) AS LAST_ID")
            fieldName = fieldInfo.alias;
        } else if (!fieldInfo.name.empty()) {
            // Fall back to field name if no alias
            fieldName = fieldInfo.name;
        } else {
            // Last resort - use generic field name
            fieldName = "FIELD_" + std::to_string(i);
        }
        
        // Unpack this field to JSON
        nlohmann::json fieldValue;
        unpackValueToJson(fieldValue, data_ptr, null_ptr, &fieldInfo, transaction);
        
        // Add to result object
        result[fieldName] = fieldValue;
    }
}

} // namespace detail

// JsonUnpacker implementation - follows same pattern as TupleUnpacker
class JsonUnpacker {
public:
    JsonUnpacker() = default;
    
    void unpack(const uint8_t* buffer, const MessageMetadata* metadata, nlohmann::json& result,
                Transaction* transaction = nullptr) {
        if (!buffer || !metadata) {
            throw FirebirdException("Invalid parameters for JSON unpack");
        }
        
        // Clear result
        result.clear();
        
        // Unpack all fields to JSON object
        detail::unpackFieldsToJson(result, buffer, metadata, transaction);
    }
    
    nlohmann::json unpack(const uint8_t* buffer, const MessageMetadata* metadata,
                         Transaction* transaction = nullptr) {
        nlohmann::json result;
        unpack(buffer, metadata, result, transaction);
        return result;
    }
};

// Factory function for creating JSON unpacker
inline std::unique_ptr<JsonUnpacker> makeJsonUnpacker() {
    return std::make_unique<JsonUnpacker>();
}

} // namespace fbpp::core