#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/tuple_packer.hpp"
#include "fbpp/core/json_packer.hpp"
#include "fbpp/core/tuple_unpacker.hpp"
#include "fbpp/core/json_unpacker.hpp"
#include <nlohmann/json.hpp>

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
    if constexpr (is_tuple_v<T>) {
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
        static_assert(!sizeof(T), "Unsupported type for packing. Use tuple or json");
    }
}

// Helper for extracting tuple types
template<typename T>
struct UnpackerHelper {
    static T unpack(const uint8_t*, const MessageMetadata*, Transaction*) {
        static_assert(!sizeof(T), "Unsupported type for unpacking");
        return T{};
    }
};

template<typename... Args>
struct UnpackerHelper<std::tuple<Args...>> {
    static std::tuple<Args...> unpack(const uint8_t* buffer,
                                      const MessageMetadata* metadata,
                                      Transaction* transaction) {
        TupleUnpacker<Args...> unpacker;
        return unpacker.unpack(buffer, metadata, transaction);
    }
};

template<>
struct UnpackerHelper<nlohmann::json> {
    static nlohmann::json unpack(const uint8_t* buffer,
                                 const MessageMetadata* metadata,
                                 Transaction* transaction) {
        JsonUnpacker unpacker;
        return unpacker.unpack(buffer, metadata, transaction);
    }
};

/**
 * @brief Universal unpack function - unpacks data from Firebird buffer
 * @tparam T Type to unpack to (tuple, json, etc)
 * @param buffer Input buffer with data
 * @param metadata Message metadata describing the buffer structure
 * @param transaction Transaction for BLOB operations (optional)
 * @return Unpacked data of type T
 */
template<typename T>
inline T unpack(const uint8_t* buffer,
               const MessageMetadata* metadata,
               Transaction* transaction = nullptr) {
    return UnpackerHelper<T>::unpack(buffer, metadata, transaction);
}

} // namespace fbpp::core