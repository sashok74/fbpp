#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/detail/sql_value_codec.hpp"
#include <tuple>
#include <cstring>
#include <optional>
#include <type_traits>

namespace fbpp::core {

// Forward declaration
class Transaction;

namespace detail {

template<typename T>
void unpackValueWithCodec(T& value,
                          const uint8_t* dataPtr,
                          const int16_t* nullPtr,
                          const FieldInfo* field,
                          ::fbpp::core::Transaction* transaction) {
    sql_value_codec::SqlReadContext ctx{field, transaction, nullPtr};
    sql_value_codec::read_sql_value(ctx, dataPtr, value);
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
        
        // Unpack this element using shared SQL codec
        unpackValueWithCodec(std::get<idx>(tuple), data_ptr, null_ptr, &fieldInfo, transaction);
        
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
