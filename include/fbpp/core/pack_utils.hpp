#pragma once

#include "fbpp/core/detail/struct_pack.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/tuple_packer.hpp"
#include "fbpp/core/json_packer.hpp"
#include "fbpp/core/tuple_unpacker.hpp"
#include "fbpp/core/json_unpacker.hpp"
#include <nlohmann/json.hpp>
#include <type_traits>

namespace fbpp::core {

// Forward declaration
class Transaction;

/**
 * @brief Universal pack function - packs data of any supported type into Firebird buffer
 * @tparam T Type to pack (tuple, json, etc)
 * @param data Data to pack
 * @param buffer Output buffer
 * @param metadata Message metadata describing the buffer structure
 * @param transaction Transaction for BLOB operations (optional)
 */
template<typename T>
inline void pack(const T& data,
                 uint8_t* buffer,
                 const MessageMetadata* metadata,
                 Transaction* transaction = nullptr) {
    if constexpr (detail::has_struct_descriptor_v<T>) {
        detail::packStruct(data, buffer, metadata, transaction);
    }
    else if constexpr (is_tuple_v<T>) {
        // For tuple - use TuplePacker with proper template arguments
        std::apply([&](auto&&... args) {
            TuplePacker<std::decay_t<decltype(args)>...> packer;
            packer.pack(data, buffer, metadata, transaction);
        }, data);
    }
    else if constexpr (is_json_v<T>) {
        // For JSON - use JsonPacker
        JsonPacker packer;
        packer.pack(data, buffer, metadata, transaction);
    }
    else {
        static_assert(detail::dependent_false_v<T>, "Unsupported type for packing. Use tuple, json, or struct with StructDescriptor");
    }
}

template<typename T, typename Enable = void>
struct UnpackerHelper;

template<typename T>
struct UnpackerHelper<T, std::enable_if_t<detail::has_struct_descriptor_v<T>>> {
    static T unpack(const uint8_t* buffer,
                    const MessageMetadata* metadata,
                    Transaction* transaction) {
        return detail::unpackStruct<T>(buffer, metadata, transaction);
    }
};

template<typename... Args>
struct UnpackerHelper<std::tuple<Args...>, void> {
    static std::tuple<Args...> unpack(const uint8_t* buffer,
                                      const MessageMetadata* metadata,
                                      Transaction* transaction) {
        TupleUnpacker<Args...> unpacker;
        return unpacker.unpack(buffer, metadata, transaction);
    }
};

template<>
struct UnpackerHelper<nlohmann::json, void> {
    static nlohmann::json unpack(const uint8_t* buffer,
                                 const MessageMetadata* metadata,
                                 Transaction* transaction) {
        JsonUnpacker unpacker;
        return unpacker.unpack(buffer, metadata, transaction);
    }
};

template<typename T>
struct UnpackerHelper<T, std::enable_if_t<!detail::has_struct_descriptor_v<T> && !is_tuple_v<T> && !is_json_v<T>>> {
    static T unpack(const uint8_t*,
                    const MessageMetadata*,
                    Transaction*) {
        static_assert(detail::dependent_false_v<T>,
                      "Unsupported type for unpacking. Use tuple, json, or struct with StructDescriptor");
        return T{};
    }
};

template<typename T>
inline T unpack(const uint8_t* buffer,
               const MessageMetadata* metadata,
               Transaction* transaction = nullptr) {
    return UnpackerHelper<T>::unpack(buffer, metadata, transaction);
}

} // namespace fbpp::core
