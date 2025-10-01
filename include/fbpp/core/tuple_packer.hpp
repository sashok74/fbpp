#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/message_builder.hpp"
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <firebird/Interface.h>
#include <firebird/impl/sqlda_pub.h>  // For SQL_TEXT, SQL_VARYING constants
#include <tuple>
#include <cstring>
#include <type_traits>

namespace fbpp::core {

// Forward declaration at namespace level
class Transaction;

// Forward declare helper function - will be defined after Transaction is included
ISC_QUAD createBlobForTransaction(Transaction* transaction, const std::vector<uint8_t>& data);

namespace detail {

// Helper trait to check if type is std::optional
template<typename T>
struct is_optional : std::false_type {};

template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

// Helper to pack a single value into buffer with metadata for proper truncation
template<typename T>
void packValue(const T& value, uint8_t* data_ptr, int16_t* null_ptr,
               const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    // Check if type has an adapter
    if constexpr (has_type_adapter_v<T>) {
        // Check if adapter uses context interface (scale/length aware)
        if constexpr (adapter_has_ctx_to_from_v<T>) {
            // Use context-aware adapter
            int16_t fb_scale = field ? field->scale : 0;
            unsigned fb_length = field ? field->length : sizeof(T);
            adapt_to_firebird_ctx(value, fb_scale, fb_length, data_ptr);
            // Clear null indicator
            if (null_ptr) *null_ptr = 0;
            return;
        } else if constexpr (adapter_has_basic_to_from_v<T>) {
            // Use basic adapter to convert to Firebird type
            auto fb_value = adapt_to_firebird(value);
            // Pack the converted value recursively
            packValue(fb_value, data_ptr, null_ptr, field, transaction);
            return;
        }
    }

    using traits = FirebirdTypeTraits<T>;

    // Set value
    if constexpr (std::is_same_v<T, std::string>) {
        // Check if this is a text BLOB field
        if (field && field->type == SQL_BLOB && field->subType == 1 && transaction) {
            // Text BLOB - create BLOB through transaction
            std::vector<uint8_t> textData(value.begin(), value.end());
            ISC_QUAD blob_id = createBlobForTransaction(transaction, textData);
            std::memcpy(data_ptr, &blob_id, sizeof(ISC_QUAD));
        } else {
            // Regular VARCHAR/CHAR - first 2 bytes are length for VARCHAR
            // For SQL_TEXT (CHAR), Firebird uses fixed-length fields, for SQL_VARYING uses variable
            if (field && field->type == SQL_TEXT) {
                // CHAR - fixed length, space-padded
                // Firebird returns max byte length considering charset (e.g., UTF8 = 4 bytes per char)
                // For CHAR(10) in UTF8, Firebird may report length=40 (10 chars * 4 bytes)
                // We should just copy the actual string and pad with spaces
                size_t copyLen = std::min(value.length(), static_cast<size_t>(field->length));
                std::memcpy(data_ptr, value.data(), copyLen);
                // Pad with spaces if needed
                if (copyLen < field->length) {
                    std::memset(data_ptr + copyLen, ' ', field->length - copyLen);
                }
            } else {
                // VARCHAR or numeric types represented as strings
                // Check if this is a numeric type that uses string representation
                bool isNumericAsString = false;
                if (field) {
                    // These types are passed as strings but are not VARCHAR/CHAR
                    switch (field->type) {
                        case 32752:  // SQL_INT128
                        case 32762:  // SQL_DEC16 (DECFLOAT16)
                        case 32760:  // SQL_DEC34 (DECFLOAT34)
                        case 570:    // SQL_DATE
                        case 560:    // SQL_TIME
                        case 510:    // SQL_TIMESTAMP
                        case 32756:  // SQL_TIME_TZ
                        case 32754:  // SQL_TIMESTAMP_TZ
                            isNumericAsString = true;
                            break;
                        case 580:    // SQL_INT64 with scale (NUMERIC/DECIMAL)
                            if (field->scale != 0) isNumericAsString = true;
                            break;
                    }
                }
                
                if (isNumericAsString) {
                    // For numeric types as strings, just copy the value without truncation checks
                    // Firebird will validate the format
                    uint16_t len = static_cast<uint16_t>(value.length());
                    std::memcpy(data_ptr, &len, sizeof(uint16_t));
                    std::memcpy(data_ptr + sizeof(uint16_t), value.data(), value.length());
                } else {
                    // Regular VARCHAR - apply length checks
                    unsigned maxDataLength = field ? field->length - sizeof(uint16_t) : 0;
                    size_t actualLen = value.length();
                    
                    if (maxDataLength > 0 && actualLen > maxDataLength) {
                        actualLen = maxDataLength;
                        auto logger = util::Logging::get();
                        if (logger) {
                            logger->warn("String truncated from {} to {} bytes", value.length(), maxDataLength);
                        }
                    }
                    
                    uint16_t len = static_cast<uint16_t>(actualLen);
                    std::memcpy(data_ptr, &len, sizeof(uint16_t));
                    std::memcpy(data_ptr + sizeof(uint16_t), value.data(), actualLen);
                }
            }
        }
    } else if constexpr (std::is_same_v<T, const char*>) {
        // Check if this is a text BLOB field
        if (field && field->type == SQL_BLOB && field->subType == 1 && transaction) {
            // Text BLOB - create BLOB through transaction
            size_t len = std::strlen(value);
            std::vector<uint8_t> textData(value, value + len);
            ISC_QUAD blob_id = createBlobForTransaction(transaction, textData);
            std::memcpy(data_ptr, &blob_id, sizeof(ISC_QUAD));
        } else {
            // Regular VARCHAR/CHAR
            size_t strLen = std::strlen(value);
            
            if (field && field->type == SQL_TEXT) {
                // CHAR - fixed length, space-padded
                size_t copyLen = std::min(strLen, static_cast<size_t>(field->length));
                std::memcpy(data_ptr, value, copyLen);
                // Pad with spaces if needed
                if (copyLen < field->length) {
                    std::memset(data_ptr + copyLen, ' ', field->length - copyLen);
                }
            } else {
                // VARCHAR - variable length with 2-byte length prefix
                unsigned maxDataLength = field ? field->length - sizeof(uint16_t) : 0;
                size_t actualLen = strLen;
                
                if (maxDataLength > 0 && actualLen > maxDataLength) {
                    actualLen = maxDataLength;
                    auto logger = util::Logging::get();
                    if (logger) {
                        logger->warn("C-string truncated from {} to {} bytes", strLen, maxDataLength);
                    }
                }
                
                uint16_t varchar_len = static_cast<uint16_t>(actualLen);
                std::memcpy(data_ptr, &varchar_len, sizeof(uint16_t));
                std::memcpy(data_ptr + sizeof(uint16_t), value, actualLen);
            }
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        // Boolean is stored as 1 byte
        uint8_t bool_val = value ? 1 : 0;
        std::memcpy(data_ptr, &bool_val, 1);
    } else if constexpr (std::is_same_v<T, Int128>) {
        // Int128 - copy 16 bytes
        // TODO: For NUMERIC(38,x) with scale != 0, this needs Firebird IUtil for proper formatting
        // Currently assumes the Int128 value is already properly scaled
        std::memcpy(data_ptr, value.data(), 16);
    } else if constexpr (std::is_same_v<T, DecFloat16>) {
        // DecFloat16 - copy 8 bytes
        std::memcpy(data_ptr, value.data(), 8);
    } else if constexpr (std::is_same_v<T, DecFloat34>) {
        // DecFloat34 - copy 16 bytes
        std::memcpy(data_ptr, value.data(), 16);
    } else if constexpr (std::is_same_v<T, Timestamp>) {
        // Firebird TIMESTAMP is encoded as ISC_TIMESTAMP structure:
        // First 4 bytes: date (days since November 17, 1858)
        // Next 4 bytes: time (fraction of day in 1/10000 of a second)
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        std::memcpy(data_ptr, &date, 4);
        std::memcpy(data_ptr + 4, &time, 4);
    } else if constexpr (std::is_same_v<T, TimestampTz>) {
        // TimestampTz - copy date (4 bytes) + time (4 bytes) + zone (2 bytes) + offset (2 bytes)
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(data_ptr, &date, 4);
        std::memcpy(data_ptr + 4, &time, 4);
        std::memcpy(data_ptr + 8, &zone, 2);
        std::memcpy(data_ptr + 10, &offset, 2);
    } else if constexpr (std::is_same_v<T, Time>) {
        // Time - copy time value (4 bytes)
        uint32_t time = value.getTime();
        std::memcpy(data_ptr, &time, 4);
    } else if constexpr (std::is_same_v<T, TimeTz>) {
        // TimeTz - copy time + zone + offset (8 bytes)
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(data_ptr, &time, 4);
        std::memcpy(data_ptr + 4, &zone, 2);
        std::memcpy(data_ptr + 6, &offset, 2);
    } else if constexpr (std::is_same_v<T, Blob>) {
        // Blob - copy blob ID (8 bytes)
        std::memcpy(data_ptr, value.getId(), 8);
    } else if constexpr (std::is_same_v<T, TextBlob>) {
        // TextBlob - copy blob ID (8 bytes)
        // Note: Text creation from cached text happens in TuplePacker::pack with IAttachment
        std::memcpy(data_ptr, value.getId(), 8);
    } else {
        // For numeric types, direct copy
        std::memcpy(data_ptr, &value, sizeof(T));
    }
    
    // Set NULL indicator (0 = not null)
    *null_ptr = 0;
}

// Specialization for std::optional
template<typename T>
void packValue(const std::optional<T>& value, uint8_t* data_ptr, int16_t* null_ptr,
               const FieldInfo* field = nullptr, ::fbpp::core::Transaction* transaction = nullptr) {
    if (value.has_value()) {
        // Check if T has a context-aware adapter
        if constexpr (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>) {
            // Use context-aware adapter directly
            int16_t fb_scale = field ? field->scale : 0;
            unsigned fb_length = field ? field->length : sizeof(T);
            adapt_to_firebird_ctx(*value, fb_scale, fb_length, data_ptr);
            // Clear null indicator
            *null_ptr = 0;
        } else {
            // Use regular packing
            packValue(*value, data_ptr, null_ptr, field, transaction);
        }
    } else {
        // Set NULL indicator (-1 = null)
        *null_ptr = -1;
        // Don't write to data_ptr for NULL values
    }
}

// Helper to recursively pack tuple elements
template<size_t Index, typename... Args>
struct TuplePackHelper {
    static void pack(const std::tuple<Args...>& tuple, uint8_t* buffer, const MessageMetadata* metadata, ::fbpp::core::Transaction* transaction = nullptr) {
        constexpr size_t idx = sizeof...(Args) - Index;
        
        // Get field info and offsets from metadata
        FieldInfo fieldInfo = metadata->getField(idx);
        unsigned data_offset = metadata->getOffset(idx);
        unsigned null_offset = metadata->getNullOffset(idx);
        
        
        uint8_t* data_ptr = buffer + data_offset;
        int16_t* null_ptr = reinterpret_cast<int16_t*>(buffer + null_offset);
        
        // Pack this element with field info and transaction
        packValue(std::get<idx>(tuple), data_ptr, null_ptr, &fieldInfo, transaction);
        
        // Recursively pack remaining elements
        if constexpr (Index > 1) {
            TuplePackHelper<Index - 1, Args...>::pack(tuple, buffer, metadata, transaction);
        }
    }
    
    static void buildMetadata(MessageBuilder& builder, unsigned& index) {
        constexpr size_t idx = sizeof...(Args) - Index;
        using ElemType = std::tuple_element_t<idx, std::tuple<Args...>>;
        
        // Generate field name
        std::string field_name = "PARAM_" + std::to_string(index);
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("About to add field {} for type index {}", field_name, idx);
        }
        
        try {
            // Add field based on type
            if constexpr (std::is_same_v<ElemType, std::string> || std::is_same_v<ElemType, const char*>) {
                // For strings, use a reasonable default length
                builder.addFieldWithLength<std::string>(field_name, 255);
            } else {
                builder.addField<ElemType>(field_name);
            }
        } catch (const std::exception& e) {
            throw FirebirdException(std::string("Failed to add field ") + field_name + ": " + e.what());
        }
        
        index++;
        
        // Recursively add remaining fields
        if constexpr (Index > 1) {
            TuplePackHelper<Index - 1, Args...>::buildMetadata(builder, index);
        }
    }
};

// Base case specialization
template<typename... Args>
struct TuplePackHelper<0, Args...> {
    static void pack(const std::tuple<Args...>&, uint8_t*, const MessageMetadata*, ::fbpp::core::Transaction* = nullptr) {}
    static void buildMetadata(MessageBuilder&, unsigned&) {}
};

} // namespace detail

// TuplePacker implementation - no longer inherits from ParameterPacker
template<typename... Args>
class TuplePacker {
public:
    using TupleType = std::tuple<Args...>;
    
    TuplePacker() : metadata_(nullptr), buffer_size_(0) {}
    
    // Main pack function using input metadata from prepared statement
    void pack(const TupleType& tuple, uint8_t* buffer, const MessageMetadata* metadata, ::fbpp::core::Transaction* transaction = nullptr) {
        if (!buffer || !metadata) {
            throw FirebirdException("Invalid parameters for pack");
        }
        
        // Validate tuple arity matches metadata
        if (sizeof...(Args) != metadata->getCount()) {
            throw FirebirdException(
                "Tuple arity mismatch: tuple has " + std::to_string(sizeof...(Args)) +
                " elements, but query expects " + std::to_string(metadata->getCount()) + " parameters"
            );
        }
        
        // Get buffer size from metadata
        size_t bufSize = metadata->getMessageLength();
        
        // Clear buffer first
        std::memset(buffer, 0, bufSize);
        
        // Pack all tuple elements
        if constexpr (sizeof...(Args) > 0) {
            detail::TuplePackHelper<sizeof...(Args), Args...>::pack(tuple, buffer, metadata, transaction);
        }
    }
    
     size_t getBufferSize() const {
        return buffer_size_;
    }
    
    bool isValid() const {
        return metadata_ != nullptr;
    }
    
private:
    void buildMetadataInternal() {
        if (metadata_) {
            return; // Already built
        }
        
        try {
            MessageBuilder builder(sizeof...(Args));
            unsigned index = 0;
            
            if constexpr (sizeof...(Args) > 0) {
                detail::TuplePackHelper<sizeof...(Args), Args...>::buildMetadata(builder, index);
            }
            
            metadata_ = builder.build();
            if (metadata_) {
                buffer_size_ = metadata_->getMessageLength();
            }
        } catch (const std::exception& e) {
            throw FirebirdException(std::string("Failed to build metadata: ") + e.what());
        }
    }
    
    std::unique_ptr<MessageMetadata> metadata_;
    size_t buffer_size_;
};

// Factory function for creating tuple packer
template<typename... Args>
std::unique_ptr<TuplePacker<Args...>> makeTuplePacker() {
    return std::make_unique<TuplePacker<Args...>>();
}

} // namespace fbpp::core

// Include transaction.hpp after main definitions
#include "fbpp/core/transaction.hpp"

// Now we can define the helper function inline
namespace fbpp::core {
inline ISC_QUAD createBlobForTransaction(Transaction* transaction, const std::vector<uint8_t>& data) {
    if (!transaction) {
        ISC_QUAD null_id = {0, 0};
        return null_id;
    }
    return transaction->createBlob(data);
}
} // namespace fbpp::core