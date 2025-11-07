#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/message_builder.hpp"
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/detail/sql_value_codec.hpp"
#include <tuple>
#include <cstring>
#include <type_traits>

namespace fbpp::core {

// Forward declaration at namespace level
class Transaction;

namespace detail {

template<typename T>
void packValueWithCodec(const T& value,
                        uint8_t* dataPtr,
                        int16_t* nullPtr,
                        const FieldInfo* field,
                        ::fbpp::core::Transaction* transaction) {
    sql_value_codec::SqlWriteContext ctx{field, transaction, nullPtr};
    sql_value_codec::write_sql_value(ctx, value, dataPtr);
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
        
        // Pack this element using shared SQL codec
        packValueWithCodec(std::get<idx>(tuple), data_ptr, null_ptr, &fieldInfo, transaction);
        
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
