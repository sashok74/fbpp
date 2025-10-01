#pragma once

#include "fbpp/core/type_adapter.hpp"
#include <ttmath/ttmath.h>
#include <array>
#include <cstring>
#include <string>

namespace fbpp::adapters {

// Use ttmath::Int for 128-bit integers
// On 64-bit systems: Int<2> = 2 * 64-bit = 128-bit
// On 32-bit systems: Int<4> = 4 * 32-bit = 128-bit
#ifdef TTMATH_PLATFORM64
using Int128 = ttmath::Int<2>;  // 2 x 64-bit words
#else
using Int128 = ttmath::Int<4>;  // 4 x 32-bit words
#endif

} // namespace fbpp::adapters

namespace fbpp::core {

// Adapter specialization for ttmath::Int (INT128)
template<>
struct TypeAdapter<fbpp::adapters::Int128> {
    static constexpr bool is_specialized = true;
    using firebird_type = std::array<uint8_t, 16>; // INT128 is 16 bytes in Firebird
    using user_type = fbpp::adapters::Int128;

    // Convert from ttmath::Int to Firebird INT128 (16 bytes, little-endian)
    static firebird_type to_firebird(const user_type& value) {
        firebird_type result{};

        // Direct memory copy - TTMath already stores in little-endian on x86/x64
        std::memcpy(result.data(), value.table, 16);

        return result;
    }

    // Convert from Firebird INT128 (16 bytes, little-endian) to ttmath::Int
    static user_type from_firebird(const firebird_type& value) {
        user_type result;

        // Direct memory copy - TTMath uses little-endian on x86/x64
        std::memcpy(result.table, value.data(), 16);

        return result;
    }

    // Optional support
    static std::optional<firebird_type> to_firebird_opt(const std::optional<user_type>& value) {
        if (value.has_value()) {
            return to_firebird(*value);
        }
        return std::nullopt;
    }

    static std::optional<user_type> from_firebird_opt(const std::optional<firebird_type>& value) {
        if (value.has_value()) {
            return from_firebird(*value);
        }
        return std::nullopt;
    }
};

// Helper function to create Int128 from string
inline fbpp::adapters::Int128 make_int128(const std::string& str, int base = 10) {
    fbpp::adapters::Int128 result;
    result.FromString(str, base);
    return result;
}

// Helper function to create Int128 from int64_t
inline fbpp::adapters::Int128 make_int128(int64_t value) {
    fbpp::adapters::Int128 result;
    if (value >= 0) {
        result = static_cast<ttmath::uint>(value);
    } else {
        // Handle negative numbers
        result = static_cast<ttmath::uint>(-value);
        result.ChangeSign();
    }
    return result;
}

} // namespace fbpp::core