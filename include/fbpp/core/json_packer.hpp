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
#include "fbpp/core/detail/sql_value_codec.hpp"

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
    
    // Create context for codec
    sql_value_codec::SqlWriteContext ctx{&field, transaction, null_ptr};

    // Dispatch based on JSON value type
    if (value.is_string()) {
        sql_value_codec::write_sql_value(ctx, value.get<std::string>(), data_ptr);
    } else if (value.is_boolean()) {
        sql_value_codec::write_sql_value(ctx, value.get<bool>(), data_ptr);
    } else if (value.is_number_integer()) {
        if (field.type == SQL_INT128 || field.type == SQL_DEC16 || field.type == SQL_DEC34) {
            // Convert to string for extended types to ensure correct parsing/conversion via Firebird API
            sql_value_codec::write_sql_value(ctx, std::to_string(value.get<int64_t>()), data_ptr);
        } else {
            // Standard integer types
            sql_value_codec::write_sql_value(ctx, value.get<int64_t>(), data_ptr);
        }
    } else if (value.is_number_float()) {
        // Pass as double
        sql_value_codec::write_sql_value(ctx, value.get<double>(), data_ptr);
    } else {
        throw FirebirdException("Unsupported JSON type for field " + field.name);
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