#pragma once

// PR-11: std::optional<T> ↔ System::Variant glue helpers.
//
// std::nullopt / std::optional<T>{} round-trips through varNull/varEmpty.
// Engaged optional unwraps to the variant of T. Supports the seven
// types that show up in HiTek's metadata UI / generic forms; extending
// to additional types is a one-line specialization.

#include "fbpp/core/firebird_compat.hpp"

#ifdef FBPP_WITH_RAD_DATASET

#include <System.hpp>
#include <System.SysUtils.hpp>
#include <System.Variants.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

namespace fbpp::ext {

namespace detail {
inline System::UnicodeString utf8ToWide(const std::string& s) {
    return System::UnicodeString(System::UTF8String(s.c_str()));
}
inline std::string wideToUtf8(const System::UnicodeString& s) {
    System::UTF8String u8(s);
    return std::string(u8.c_str());
}
} // namespace detail

template<typename T>
inline System::Variant optionalToVariant(const std::optional<T>& value) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if (!value.has_value()) {
        return System::Variants::Null();
    }
    if constexpr (std::is_same_v<U, std::int32_t> ||
                  std::is_same_v<U, int>) {
        return System::Variant(static_cast<int>(*value));
    } else if constexpr (std::is_same_v<U, std::int64_t>) {
        return System::Variant(static_cast<__int64>(*value));
    } else if constexpr (std::is_same_v<U, double>) {
        return System::Variant(static_cast<double>(*value));
    } else if constexpr (std::is_same_v<U, bool>) {
        return System::Variant(static_cast<bool>(*value));
    } else if constexpr (std::is_same_v<U, std::string>) {
        return System::Variant(detail::utf8ToWide(*value));
    } else if constexpr (std::is_same_v<U, System::UnicodeString>) {
        return System::Variant(*value);
    } else if constexpr (std::is_same_v<U, System::TDateTime>) {
        return System::Variant(*value);
    } else {
        static_assert(!std::is_same_v<U, U>,
            "optionalToVariant<T>: unsupported T (extend the if-chain)");
    }
}

template<typename T>
inline std::optional<T> variantToOptional(const System::Variant& value) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if (System::Variants::VarIsNull(value) ||
        System::Variants::VarIsEmpty(value)) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<U, std::int32_t> ||
                  std::is_same_v<U, int>) {
        return static_cast<U>(static_cast<int>(value));
    } else if constexpr (std::is_same_v<U, std::int64_t>) {
        return static_cast<U>(static_cast<__int64>(value));
    } else if constexpr (std::is_same_v<U, double>) {
        return static_cast<U>(static_cast<double>(value));
    } else if constexpr (std::is_same_v<U, bool>) {
        return static_cast<U>(static_cast<bool>(value));
    } else if constexpr (std::is_same_v<U, std::string>) {
        System::UnicodeString w = value;
        return detail::wideToUtf8(w);
    } else if constexpr (std::is_same_v<U, System::UnicodeString>) {
        return static_cast<System::UnicodeString>(value);
    } else if constexpr (std::is_same_v<U, System::TDateTime>) {
        return System::TDateTime(static_cast<double>(value));
    } else {
        static_assert(!std::is_same_v<U, U>,
            "variantToOptional<T>: unsupported T (extend the if-chain)");
    }
}

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET
