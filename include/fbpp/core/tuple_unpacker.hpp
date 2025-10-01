#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <tuple>
#include <cstring>
#include <optional>
#include <type_traits>

namespace fbpp::core {

// Forward declaration
class Transaction;

namespace detail {

// Helper to unpack a single value from buffer
template<typename T>
void unpackValue(T& value, const uint8_t* data_ptr, const int16_t* null_ptr,
                const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    // Check NULL indicator
    if (*null_ptr == -1) {
        // This shouldn't happen for non-optional types
        throw FirebirdException("NULL value for non-nullable field");
    }

    // Check if type has an adapter
    if constexpr (has_type_adapter_v<T>) {
        // Check if adapter uses context interface (scale/length aware)
        if constexpr (adapter_has_ctx_to_from_v<T>) {
            // Use context-aware adapter
            int16_t fb_scale = field ? field->scale : 0;
            unsigned fb_length = field ? field->length : sizeof(T);
            value = adapt_from_firebird_ctx<T>(data_ptr, fb_length, fb_scale);
            return;
        } else if constexpr (adapter_has_basic_to_from_v<T>) {
            // Get the Firebird type that this user type is adapted from
            using firebird_type = typename TypeAdapter<T>::firebird_type;

            // Unpack to Firebird type first
            firebird_type fb_value;
            unpackValue(fb_value, data_ptr, null_ptr, field, transaction);

            // Convert from Firebird type to user type using adapter
            value = adapt_from_firebird<T>(fb_value);
            return;
        }
    }

    // Unpack value based on type
    if constexpr (std::is_same_v<T, std::string>) {
        // Check if this is a text BLOB field
        if (field && field->type == SQL_BLOB && field->subType == 1 && transaction) {
            // Text BLOB - load text from blob
            ISC_QUAD blob_id;
            std::memcpy(&blob_id, data_ptr, sizeof(ISC_QUAD));
            auto blobData = transaction->loadBlob(&blob_id);
            value.assign(reinterpret_cast<const char*>(blobData.data()), blobData.size());
        } else {
            // Regular VARCHAR - first 2 bytes are length
            uint16_t len;
            std::memcpy(&len, data_ptr, sizeof(uint16_t));
            value.assign(reinterpret_cast<const char*>(data_ptr + sizeof(uint16_t)), len);
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        // Boolean is stored as 1 byte
        uint8_t bool_val;
        std::memcpy(&bool_val, data_ptr, 1);
        value = (bool_val != 0);
    } else if constexpr (std::is_same_v<T, Int128>) {
        // Int128 - copy 16 bytes
        // TODO: For NUMERIC(38,x) with scale != 0, this should interpret raw bytes according to scale
        // Currently returns raw Int128 value
        value = Int128(data_ptr);
    } else if constexpr (std::is_same_v<T, DecFloat16>) {
        // DecFloat16 - copy 8 bytes
        value = DecFloat16(data_ptr);
    } else if constexpr (std::is_same_v<T, DecFloat34>) {
        // DecFloat34 - copy 16 bytes
        value = DecFloat34(data_ptr);
    } else if constexpr (std::is_same_v<T, Timestamp>) {
        // Timestamp - read date and time (8 bytes total: 4 + 4)
        uint32_t date;
        uint32_t time;
        std::memcpy(&date, data_ptr, 4);
        std::memcpy(&time, data_ptr + 4, 4);
        value = Timestamp(date, time);
    } else if constexpr (std::is_same_v<T, TimestampTz>) {
        // TimestampTz - read date, time, zone and offset (12 bytes total: 4 + 4 + 2 + 2)
        uint32_t date;
        uint32_t time;
        uint16_t zone;
        int16_t offset;
        std::memcpy(&date, data_ptr, 4);
        std::memcpy(&time, data_ptr + 4, 4);
        std::memcpy(&zone, data_ptr + 8, 2);
        std::memcpy(&offset, data_ptr + 10, 2);
        value = TimestampTz(date, time, zone, offset);
    } else if constexpr (std::is_same_v<T, Time>) {
        // Time - read time value (4 bytes)
        uint32_t time;
        std::memcpy(&time, data_ptr, 4);
        value = Time(time);
    } else if constexpr (std::is_same_v<T, TimeTz>) {
        // TimeTz - read time + zone + offset (8 bytes)
        uint32_t time;
        uint16_t zone;
        int16_t offset;
        std::memcpy(&time, data_ptr, 4);
        std::memcpy(&zone, data_ptr + 4, 2);
        std::memcpy(&offset, data_ptr + 6, 2);
        value = TimeTz(time, zone, offset);
    } else if constexpr (std::is_same_v<T, Blob>) {
        // Blob - read blob ID (8 bytes)
        value = Blob(data_ptr);
    } else if constexpr (std::is_same_v<T, TextBlob>) {
        // TextBlob - read blob ID (8 bytes)
        // Note: Text reading from BLOB happens in TupleUnpacker::unpack with IAttachment
        value = TextBlob(data_ptr);
    } else {
        // For numeric types, direct copy
        std::memcpy(&value, data_ptr, sizeof(T));
    }
}

// Specialization for std::optional
template<typename T>
void unpackValue(std::optional<T>& value, const uint8_t* data_ptr, const int16_t* null_ptr,
                const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    // Check NULL indicator
    if (*null_ptr == -1) {
        // Value is NULL
        value.reset();
    } else {
        // Value is not NULL
        // Check if T has a context-aware adapter
        if constexpr (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>) {
            // Use context-aware adapter directly
            int16_t fb_scale = field ? field->scale : 0;
            unsigned fb_length = field ? field->length : sizeof(T);
            value = adapt_from_firebird_ctx<T>(data_ptr, fb_length, fb_scale);
        } else {
            // Use regular unpacking
            T temp;
            unpackValue(temp, data_ptr, null_ptr, field, transaction);
            value = std::move(temp);
        }
    }
}

// Helper to recursively unpack tuple elements
template<size_t Index, typename... Args>
struct TupleUnpackHelper {
    static void unpack(std::tuple<Args...>& tuple, const uint8_t* buffer, const MessageMetadata* metadata, 
                      ::fbpp::core::Transaction* transaction = nullptr) {
        constexpr size_t idx = sizeof...(Args) - Index;
        
        // Get field info and offsets from metadata
        FieldInfo fieldInfo = metadata->getField(idx);
        unsigned data_offset = metadata->getOffset(idx);
        unsigned null_offset = metadata->getNullOffset(idx);
        
        const uint8_t* data_ptr = buffer + data_offset;
        const int16_t* null_ptr = reinterpret_cast<const int16_t*>(buffer + null_offset);
        
        // Unpack this element with field info and transaction
        unpackValue(std::get<idx>(tuple), data_ptr, null_ptr, &fieldInfo, transaction);
        
        // Recursively unpack remaining elements
        if constexpr (Index > 1) {
            TupleUnpackHelper<Index - 1, Args...>::unpack(tuple, buffer, metadata, transaction);
        }
    }
};

// Base case specialization
template<typename... Args>
struct TupleUnpackHelper<0, Args...> {
    static void unpack(std::tuple<Args...>&, const uint8_t*, const MessageMetadata*, ::fbpp::core::Transaction* = nullptr) {}
};

} // namespace detail

// TupleUnpacker implementation
template<typename... Args>
class TupleUnpacker {
public:
    using TupleType = std::tuple<Args...>;
    
    TupleUnpacker() = default;
    
    void unpack(const uint8_t* buffer, const MessageMetadata* metadata, TupleType& result,
               Transaction* transaction = nullptr) {
        if (!buffer || !metadata) {
            throw FirebirdException("Invalid parameters for unpack");
        }
        
        // Verify field count matches
        if (metadata->getCount() != sizeof...(Args)) {
            throw FirebirdException(
                "Field count mismatch: expected " + std::to_string(sizeof...(Args)) +
                ", got " + std::to_string(metadata->getCount())
            );
        }
        
        // Unpack all tuple elements
        if constexpr (sizeof...(Args) > 0) {
            detail::TupleUnpackHelper<sizeof...(Args), Args...>::unpack(result, buffer, metadata, transaction);
        }
    }
    
    TupleType unpack(const uint8_t* buffer, const MessageMetadata* metadata,
                    Transaction* transaction = nullptr) {
        TupleType result;
        unpack(buffer, metadata, result, transaction);
        return result;
    }
    
    // Deprecated: Use unpack() with Transaction parameter instead
    template<typename IAttachment, typename ITransaction, typename IStatus>
    [[deprecated("Use unpack() with Transaction* parameter instead")]]
    void unpackWithBlobs(const uint8_t* buffer, const MessageMetadata* metadata, TupleType& result,
                        IAttachment* att, ITransaction* tra, IStatus* status) {
        // The new unpack() method handles everything automatically when Transaction is passed
        // We don't have access to Transaction here, so we can't use the new functionality
        unpack(buffer, metadata, result, nullptr);
    }
    
    // Deprecated: Use unpack() with Transaction parameter instead
    template<typename IAttachment, typename ITransaction, typename IStatus>
    [[deprecated("Use unpack() with Transaction* parameter instead")]]
    TupleType unpackWithBlobs(const uint8_t* buffer, const MessageMetadata* metadata,
                              IAttachment* att, ITransaction* tra, IStatus* status) {
        return unpack(buffer, metadata, nullptr);
    }
    
};

// Factory function for creating tuple unpacker
template<typename... Args>
std::unique_ptr<TupleUnpacker<Args...>> makeTupleUnpacker() {
    return std::make_unique<TupleUnpacker<Args...>>();
}

} // namespace fbpp::core