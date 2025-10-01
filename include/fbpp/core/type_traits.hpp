#pragma once

#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/firebird_compat.hpp"  // Firebird header compatibility layer
#include <cstdint>
#include <string>
#include <optional>
#include <type_traits>
#include <tuple>
#include <nlohmann/json_fwd.hpp>

namespace fbpp::core {

// Forward declaration for SFINAE
template<typename T, typename = void>
struct FirebirdTypeTraits;

// Primary template - disabled for unsupported types
template<typename T>
struct FirebirdTypeTraits<T, void> {
    // No members - type is not supported
    // We don't use static_assert here to allow SFINAE to work
};

// Specialization for int16_t (SMALLINT)
template<>
struct FirebirdTypeTraits<int16_t> {
    static constexpr int sql_type = SQL_SHORT;
    static constexpr size_t size = sizeof(int16_t);
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "SMALLINT";
    static constexpr int scale = 0;
    using native_type = int16_t;
};

// Specialization for int32_t/int (INTEGER)
template<>
struct FirebirdTypeTraits<int32_t> {
    static constexpr int sql_type = SQL_LONG;
    static constexpr size_t size = sizeof(int32_t);
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "INTEGER";
    static constexpr int scale = 0;
    using native_type = int32_t;
};

// Specialization for int64_t (BIGINT)
template<>
struct FirebirdTypeTraits<int64_t> {
    static constexpr int sql_type = SQL_INT64;
    static constexpr size_t size = sizeof(int64_t);
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "BIGINT";
    static constexpr int scale = 0;
    using native_type = int64_t;
};

// Specialization for float
template<>
struct FirebirdTypeTraits<float> {
    static constexpr int sql_type = SQL_FLOAT;
    static constexpr size_t size = sizeof(float);
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "FLOAT";
    static constexpr int scale = 0;
    using native_type = float;
};

// Specialization for double
template<>
struct FirebirdTypeTraits<double> {
    static constexpr int sql_type = SQL_DOUBLE;
    static constexpr size_t size = sizeof(double);
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "DOUBLE PRECISION";
    static constexpr int scale = 0;
    using native_type = double;
};

// Specialization for bool
template<>
struct FirebirdTypeTraits<bool> {
    static constexpr int sql_type = SQL_BOOLEAN;
    static constexpr size_t size = 1;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "BOOLEAN";
    static constexpr int scale = 0;
    using native_type = bool;
};

// Specialization for std::string (VARCHAR)
template<>
struct FirebirdTypeTraits<std::string> {
    static constexpr int sql_type = SQL_VARYING;
    static constexpr size_t size = 0; // Variable size
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "VARCHAR";
    static constexpr int scale = 0;
    using native_type = std::string;
    
    // Additional properties for string types
    static constexpr size_t default_length = 255;
    static constexpr bool is_varying = true;
};

// Specialization for const char* (treated as VARCHAR)
template<>
struct FirebirdTypeTraits<const char*> {
    static constexpr int sql_type = SQL_VARYING;
    static constexpr size_t size = 0; // Variable size
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "VARCHAR";
    static constexpr int scale = 0;
    using native_type = const char*;
    
    static constexpr size_t default_length = 255;
    static constexpr bool is_varying = true;
};

// Specialization for Int128
template<>
struct FirebirdTypeTraits<Int128> {
    static constexpr int sql_type = SQL_INT128;
    static constexpr size_t size = 16;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "INT128";
    static constexpr int scale = 0;
    using native_type = Int128;
};

// Specialization for DecFloat16
template<>
struct FirebirdTypeTraits<DecFloat16> {
    static constexpr int sql_type = SQL_DEC16;  // 32760 - ИСПРАВЛЕНО!
    static constexpr size_t size = 8;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "DECFLOAT(16)";
    static constexpr int scale = 0;
    using native_type = DecFloat16;
};

// Specialization for DecFloat34
template<>
struct FirebirdTypeTraits<DecFloat34> {
    static constexpr int sql_type = SQL_DEC34;  // 32762 - ИСПРАВЛЕНО!
    static constexpr size_t size = 16;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "DECFLOAT(34)";
    static constexpr int scale = 0;
    using native_type = DecFloat34;
};

// Specialization for Timestamp
template<>
struct FirebirdTypeTraits<Timestamp> {
    static constexpr int sql_type = SQL_TIMESTAMP;
    static constexpr size_t size = 8;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "TIMESTAMP";
    static constexpr int scale = 0;
    using native_type = Timestamp;
};

// Specialization for TimestampTz
template<>
struct FirebirdTypeTraits<TimestampTz> {
    static constexpr int sql_type = SQL_TIMESTAMP_TZ;
    static constexpr size_t size = 12;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "TIMESTAMP WITH TIME ZONE";
    static constexpr int scale = 0;
    using native_type = TimestampTz;
};

// Specialization for Date
template<>
struct FirebirdTypeTraits<Date> {
    static constexpr int sql_type = SQL_TYPE_DATE;  // 570
    static constexpr size_t size = 4;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "DATE";
    static constexpr int scale = 0;
    using native_type = Date;
};

// Specialization for Time
template<>
struct FirebirdTypeTraits<Time> {
    static constexpr int sql_type = SQL_TYPE_TIME;  // 560 - ИСПРАВЛЕНО!
    static constexpr size_t size = 4;
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "TIME";
    static constexpr int scale = 0;
    using native_type = Time;
};

// Specialization for TimeTz
template<>
struct FirebirdTypeTraits<TimeTz> {
    static constexpr int sql_type = SQL_TIME_TZ;  // 32756
    static constexpr size_t size = 8;  // 4 bytes time + 2 bytes zone + 2 bytes offset
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "TIME WITH TIME ZONE";
    static constexpr int scale = 0;
    using native_type = TimeTz;
};

// Specialization for Blob
template<>
struct FirebirdTypeTraits<Blob> {
    static constexpr int sql_type = SQL_BLOB;
    static constexpr size_t size = 8;  // Blob ID size
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "BLOB";
    static constexpr int scale = 0;
    using native_type = Blob;
};

// Specialization for TextBlob
template<>
struct FirebirdTypeTraits<TextBlob> {
    static constexpr int sql_type = SQL_BLOB;
    static constexpr size_t size = 8;  // Blob ID size
    static constexpr bool is_nullable = false;
    static constexpr const char* type_name = "BLOB SUB_TYPE TEXT";
    static constexpr int scale = 0;
    static constexpr int sub_type = 1;  // Text BLOB
    using native_type = TextBlob;
};

// Support for nullable types via std::optional
// In Firebird, nullable types are encoded as base_type + 1, not base_type | SQL_NULL
template<typename T>
struct FirebirdTypeTraits<std::optional<T>> {
    using base_traits = FirebirdTypeTraits<T>;
    static constexpr int sql_type = base_traits::sql_type + 1;  // Firebird nullable = type + 1
    static constexpr size_t size = base_traits::size;
    static constexpr bool is_nullable = true;
    static constexpr const char* type_name = base_traits::type_name;
    static constexpr int scale = base_traits::scale;
    using native_type = std::optional<T>;
    using value_type = T;
    
    // Add default_length for optional strings
    static constexpr size_t default_length = std::conditional_t<
        std::is_same_v<T, std::string>,
        std::integral_constant<size_t, 255>,
        std::integral_constant<size_t, 0>
    >::value;
};

// Type checking helpers
template<typename T>
constexpr bool is_firebird_type_v = false;

template<typename T>
    requires requires { typename FirebirdTypeTraits<T>::native_type; }
constexpr bool is_firebird_type_v<T> = true;

// Helper to get SQL type for a C++ type
template<typename T>
constexpr int getSqlType() {
    return FirebirdTypeTraits<T>::sql_type;
}

// Helper to check if type is nullable
template<typename T>
constexpr bool isNullableType() {
    return FirebirdTypeTraits<T>::is_nullable;
}

// Helper to make a type nullable (add 1 to make it nullable in Firebird)
constexpr int makeNullable(int sqlType) {
    // Check if already nullable (odd number in Firebird convention)
    return (sqlType % 2 == 0) ? sqlType + 1 : sqlType;
}

// Helper to get type name
template<typename T>
constexpr const char* getTypeName() {
    return FirebirdTypeTraits<T>::type_name;
}

// Type traits for tuple and json detection
template<typename T>
struct is_tuple : std::false_type {};

template<typename... Args>
struct is_tuple<std::tuple<Args...>> : std::true_type {};

template<typename T>
inline constexpr bool is_tuple_v = is_tuple<T>::value;

// Type traits for JSON detection
template<typename T>
struct is_json : std::false_type {};

template<>
struct is_json<nlohmann::json> : std::true_type {};

template<typename T>
inline constexpr bool is_json_v = is_json<T>::value;

// Specialization for adapted types - forward to their Firebird equivalent
template<typename T>
    requires has_type_adapter_v<T>
struct FirebirdTypeTraits<T> : FirebirdTypeTraits<firebird_equivalent_t<T>> {
    using adapted_type = T;
    using base_traits = FirebirdTypeTraits<firebird_equivalent_t<T>>;

    // Override type name to indicate it's adapted
    static constexpr const char* type_name = "ADAPTED";
};

// Helper to get the actual Firebird type for any type (adapted or not)
template<typename T>
using effective_firebird_type = std::conditional_t<
    has_type_adapter_v<T>,
    firebird_equivalent_t<T>,
    T
>;

// Check if a type can be used with Firebird (directly or through adapter)
template<typename T>
struct is_firebird_compatible {
private:
    // Helper to check if FirebirdTypeTraits is specialized for a type
    template<typename U, typename = void>
    struct has_traits : std::false_type {};

    template<typename U>
    struct has_traits<U, std::void_t<decltype(FirebirdTypeTraits<U>::sql_type)>>
        : std::true_type {};

public:
    static constexpr bool value = has_traits<effective_firebird_type<T>>::value;
};

template<typename T>
inline constexpr bool is_firebird_compatible_v = is_firebird_compatible<T>::value;

} // namespace fbpp::core