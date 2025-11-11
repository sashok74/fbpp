#pragma once

// Compatibility layer - forward to new struct_descriptor.hpp
#include "fbpp/core/struct_descriptor.hpp"

namespace fbpp::core {
namespace detail {

// Legacy functions - forward to new implementation in struct_descriptor.hpp
template<typename T>
void packStruct(const T& value,
                uint8_t* buffer,
                const MessageMetadata* metadata,
                Transaction* transaction) {
    fbpp::core::packStruct(value, buffer, metadata, transaction);
}

template<typename T>
T unpackStruct(const uint8_t* buffer,
               const MessageMetadata* metadata,
               Transaction* transaction) {
    return fbpp::core::unpackStruct<T>(buffer, metadata, transaction);
}

} // namespace detail
} // namespace fbpp::core
