#pragma once

#include <type_traits>
#include <optional>
#include <cstdint>
#include <cstddef>   // std::byte (если понадобится)
#include <utility>
#include <array>     // For std::array in optional support

namespace fbpp::core {

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
template<typename T> struct TypeAdapter;

// -----------------------------------------------------------------------------
// Базовый шаблон адаптера (по умолчанию — нет адаптера)
// -----------------------------------------------------------------------------
template<typename T>
struct TypeAdapter {
    static constexpr bool is_specialized = false;
};

// -----------------------------------------------------------------------------
// Метадетектирование наличия адаптера
// -----------------------------------------------------------------------------
template<typename T>
struct has_type_adapter : std::false_type {};

template<typename T>
    requires (TypeAdapter<T>::is_specialized)
struct has_type_adapter<T> : std::true_type {};

template<typename T>
inline constexpr bool has_type_adapter_v = has_type_adapter<T>::value;

// -----------------------------------------------------------------------------
// Детектор: у адаптера есть alias firebird_type?
// (для «старых» адаптеров, не зависящих от масштаба/длины)
// -----------------------------------------------------------------------------
template<typename T, typename = void>
struct adapter_has_firebird_type : std::false_type {};

template<typename T>
struct adapter_has_firebird_type<T, std::void_t<typename TypeAdapter<T>::firebird_type>> : std::true_type {};

template<typename T>
inline constexpr bool adapter_has_firebird_type_v = adapter_has_firebird_type<T>::value;

// -----------------------------------------------------------------------------
// Эквивалент в Firebird: если у адаптера есть firebird_type — берём его,
// иначе fallback на сам T (для контекстных адаптеров он игнорируется)
// -----------------------------------------------------------------------------
template<typename T, typename = void>
struct firebird_equivalent { using type = T; };

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_firebird_type_v<T>)
struct firebird_equivalent<T> {
    using type = typename TypeAdapter<T>::firebird_type;
};

template<typename T>
using firebird_equivalent_t = typename firebird_equivalent<T>::type;

// -----------------------------------------------------------------------------
// Детекторы наличия методов старого интерфейса адаптера
// -----------------------------------------------------------------------------
template<typename T, typename = void>
struct adapter_has_basic_to_from : std::false_type {};

template<typename T>
struct adapter_has_basic_to_from<T, std::void_t<
    decltype( TypeAdapter<T>::to_firebird( std::declval<const T&>() ) ),
    decltype( TypeAdapter<T>::from_firebird( std::declval<const firebird_equivalent_t<T>&>() ) )
>> : std::true_type {};

template<typename T>
inline constexpr bool adapter_has_basic_to_from_v = adapter_has_basic_to_from<T>::value;

// -----------------------------------------------------------------------------
// Детекторы наличия методов КОНТЕКСТНОГО интерфейса адаптера
// to_firebird(const T&, int16_t fb_scale, unsigned fb_length, uint8_t* out_le)
// from_firebird(const uint8_t* in_le, unsigned fb_length, int16_t fb_scale) -> T
// -----------------------------------------------------------------------------
template<typename T, typename = void>
struct adapter_has_ctx_to_from : std::false_type {};

template<typename T>
struct adapter_has_ctx_to_from<T, std::void_t<
    decltype( TypeAdapter<T>::to_firebird( std::declval<const T&>(), std::declval<int16_t>(), std::declval<unsigned>(), std::declval<uint8_t*>() ) ),
    decltype( TypeAdapter<T>::from_firebird( std::declval<const uint8_t*>(), std::declval<unsigned>(), std::declval<int16_t>() ) )
>> : std::true_type {};

template<typename T>
inline constexpr bool adapter_has_ctx_to_from_v = adapter_has_ctx_to_from<T>::value;

// -----------------------------------------------------------------------------
// Хелперы для старого (базового) интерфейса
// -----------------------------------------------------------------------------
template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_basic_to_from_v<T>)
auto adapt_to_firebird(const T& value) {
    return TypeAdapter<T>::to_firebird(value);
}

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_basic_to_from_v<T>)
T adapt_from_firebird(const firebird_equivalent_t<T>& value) {
    return TypeAdapter<T>::from_firebird(value);
}

// Optional value adaptation (старый интерфейс)
template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_basic_to_from_v<T>)
auto adapt_to_firebird_opt(const std::optional<T>& value) {
    if constexpr (requires { TypeAdapter<T>::to_firebird_opt(value); }) {
        return TypeAdapter<T>::to_firebird_opt(value);
    } else {
        if (value.has_value()) {
            return std::optional<firebird_equivalent_t<T>>{TypeAdapter<T>::to_firebird(*value)};
        }
        return std::optional<firebird_equivalent_t<T>>{std::nullopt};
    }
}

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_basic_to_from_v<T>)
std::optional<T> adapt_from_firebird_opt(const std::optional<firebird_equivalent_t<T>>& value) {
    if constexpr (requires { TypeAdapter<T>::from_firebird_opt(value); }) {
        return TypeAdapter<T>::from_firebird_opt(value);
    } else {
        if (value.has_value()) {
            return std::optional<T>{TypeAdapter<T>::from_firebird(*value)};
        }
        return std::optional<T>{std::nullopt};
    }
}

// -----------------------------------------------------------------------------
// Хелперы для КОНТЕКСТНОГО интерфейса (scale/length/buffer)
// Эти функции доступны только для типов, чей адаптер объявил такие методы.
// -----------------------------------------------------------------------------
template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
void adapt_to_firebird_ctx(const T& value,
                           int16_t fb_scale,
                           unsigned fb_length,
                           uint8_t* out_little_endian)
{
    TypeAdapter<T>::to_firebird(value, fb_scale, fb_length, out_little_endian);
}

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
T adapt_from_firebird_ctx(const uint8_t* in_little_endian,
                          unsigned fb_length,
                          int16_t fb_scale)
{
    return TypeAdapter<T>::from_firebird(in_little_endian, fb_length, fb_scale);
}

// -----------------------------------------------------------------------------
// Optional support for КОНТЕКСТНЫХ адаптеров
// -----------------------------------------------------------------------------
template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
void adapt_to_firebird_ctx_opt(const std::optional<T>& value,
                               int16_t fb_scale,
                               unsigned fb_length,
                               uint8_t* out_little_endian,
                               bool* is_null)
{
    if (value.has_value()) {
        adapt_to_firebird_ctx(*value, fb_scale, fb_length, out_little_endian);
        *is_null = false;
    } else {
        *is_null = true;
        // Don't touch the buffer for NULL values
    }
}

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
std::optional<T> adapt_from_firebird_ctx_opt(const uint8_t* in_little_endian,
                                             unsigned fb_length,
                                             int16_t fb_scale,
                                             bool is_null)
{
    if (is_null) {
        return std::nullopt;
    }
    return adapt_from_firebird_ctx<T>(in_little_endian, fb_length, fb_scale);
}

// Convenience overloads for testing (without explicit null flag)
template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
std::optional<std::array<uint8_t, 16>> adapt_to_firebird_opt(const std::optional<T>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }

    std::array<uint8_t, 16> buffer{};
    // Use default scale and length for the type
    int16_t fb_scale = T::scale;
    unsigned fb_length = sizeof(typename T::IntType);
    adapt_to_firebird_ctx(*value, fb_scale, fb_length, buffer.data());
    return buffer;
}

template<typename T>
    requires (has_type_adapter_v<T> && adapter_has_ctx_to_from_v<T>)
std::optional<T> adapt_from_firebird_opt(const std::optional<std::array<uint8_t, 16>>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }

    // Use default scale and length for the type
    int16_t fb_scale = T::scale;
    unsigned fb_length = sizeof(typename T::IntType);
    return adapt_from_firebird_ctx<T>(value->data(), fb_length, fb_scale);
}

// -----------------------------------------------------------------------------
// Concepts
// -----------------------------------------------------------------------------
template<typename T>
concept Adaptable = has_type_adapter_v<T>;

// Тип «совместим с Firebird»: базовые числа/строка или адаптируемые типы.
// (Контекстные адаптеры тоже сюда попадают через has_type_adapter_v<T>.)
template<typename T>
concept FirebirdCompatible =
    std::is_arithmetic_v<T> ||
    std::is_same_v<T, std::string> ||
    has_type_adapter_v<T>;

} // namespace fbpp::core
