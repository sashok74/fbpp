#pragma once

#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/extended_types.hpp"

// Include CppDecimal main header which defines the dec namespace
#include <decimal>
#include <cstring>

namespace fbpp::adapters {
    // Convenience aliases for CppDecimal types
    using DecFloat16 = dec::DecDouble;
    using DecFloat34 = dec::DecQuad;
}

namespace fbpp::core {

// Type adapter for CppDecimal DecDouble (DECFLOAT16)
template<>
struct TypeAdapter<dec::DecDouble> {
    static constexpr bool is_specialized = true;

    using user_type = dec::DecDouble;
    using firebird_type = fbpp::core::DecFloat16;  // 8-byte Firebird DECFLOAT16

    // Convert from user type (CppDecimal DecDouble) to Firebird type
    static firebird_type to_firebird(const user_type& value) {
        firebird_type result;

        // DecDouble internally uses decDouble which is 8 bytes
        // Copy the raw 8 bytes
        const void* raw_data = value.data();
        std::memcpy(result.data(), raw_data, 8);

        return result;
    }

    // Convert from Firebird type to user type (CppDecimal DecDouble)
    static user_type from_firebird(const firebird_type& value) {
        // Create DecDouble from raw bytes
        decDouble dd;
        std::memcpy(&dd, value.data(), 8);

        return user_type(dd);
    }
};

// Type adapter for CppDecimal DecQuad (DECFLOAT34)
template<>
struct TypeAdapter<dec::DecQuad> {
    static constexpr bool is_specialized = true;

    using user_type = dec::DecQuad;
    using firebird_type = fbpp::core::DecFloat34;  // 16-byte Firebird DECFLOAT34

    // Convert from user type (CppDecimal DecQuad) to Firebird type
    static firebird_type to_firebird(const user_type& value) {
        firebird_type result;

        // DecQuad internally uses decQuad which is 16 bytes
        // Copy the raw 16 bytes
        const void* raw_data = value.data();
        std::memcpy(result.data(), raw_data, 16);

        return result;
    }

    // Convert from Firebird type to user type (CppDecimal DecQuad)
    static user_type from_firebird(const firebird_type& value) {
        // Create DecQuad from raw bytes
        decQuad dq;
        std::memcpy(&dq, value.data(), 16);

        return user_type(dq);
    }
};

} // namespace fbpp::core