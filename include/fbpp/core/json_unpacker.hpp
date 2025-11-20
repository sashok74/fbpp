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
#include "fbpp/core/detail/conversion_utils.hpp"
#include "fbpp/core/detail/sql_value_codec.hpp"

namespace fbpp::core {
namespace detail {

// Helper to unpack a single value from buffer to JSON
inline void unpackValueToJson(nlohmann::json& jsonValue, const uint8_t* data_ptr, const int16_t* null_ptr,
                              const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    // Check NULL indicator
    if (*null_ptr == -1) {
        // Value is NULL
        jsonValue = nullptr;
        return;
    }

    // Create context for codec
    sql_value_codec::SqlReadContext ctx{field, transaction, null_ptr};

    switch (field->type) {
        case SQL_TEXT:
        case SQL_VARYING:
        case SQL_BLOB: {
            std::string val;
            sql_value_codec::read_sql_value(ctx, data_ptr, val);
            jsonValue = val;
            break;
        }
        
        case SQL_BOOLEAN: {
            bool val;
            sql_value_codec::read_sql_value(ctx, data_ptr, val);
            jsonValue = val;
            break;
        }
        
        case SQL_INT128:
        case SQL_DEC16:
        case SQL_DEC34:
        case SQL_TIMESTAMP:
        case SQL_TIMESTAMP_TZ:
        case SQL_TYPE_TIME:
        case SQL_TIME_TZ:
        case SQL_TYPE_DATE: {
            // Extended types -> String
            std::string val;
            sql_value_codec::read_sql_value(ctx, data_ptr, val);
            jsonValue = val;
            break;
        }
        
        case SQL_SHORT: {
            if (field->scale < 0) {
                std::string val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            } else {
                int16_t val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            }
            break;
        }
        
        case SQL_LONG: {
            if (field->scale < 0) {
                std::string val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            } else {
                int32_t val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            }
            break;
        }
        
        case SQL_INT64: {
            if (field->scale < 0) {
                std::string val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            } else {
                int64_t val;
                sql_value_codec::read_sql_value(ctx, data_ptr, val);
                jsonValue = val;
            }
            break;
        }
        
        case SQL_FLOAT: {
            float val;
            sql_value_codec::read_sql_value(ctx, data_ptr, val);
            jsonValue = val;
            break;
        }
        
        case SQL_DOUBLE:
        case SQL_D_FLOAT: {
            double val;
            sql_value_codec::read_sql_value(ctx, data_ptr, val);
            jsonValue = val;
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