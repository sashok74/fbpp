#pragma once

#include "fbpp/core/type_adapter.hpp"
#include "fbpp/adapters/ttmath_int128.hpp"
#include <ttmath/ttmath.h>
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace fbpp::core::detail {

// mul/div by 10^n for ttmath::Int
template<class BigInt>
inline void mul_pow10(BigInt& v, unsigned n) { for (unsigned i = 0; i < n; ++i) v.MulInt(10); }

template<class BigInt>
inline void div_pow10_trunc(BigInt& v, unsigned n) { for (unsigned i = 0; i < n; ++i) v.DivInt(10); }

// crude sign detection via text (API-нейтрально к ttmath)
template<class BigInt>
inline bool is_negative(const BigInt& v) {
    auto s = v.ToString();
    return !s.empty() && s[0] == '-';
}

} //namespace fbpp::core::detail

namespace fbpp::adapters {

/**
 * @brief Fixed-point decimal type using TTMath for NUMERIC/DECIMAL support
 *
 * @tparam IntWords Number of machine words for the integer part (2 for 128-bit on 64-bit systems)
 * @tparam Scale The scale (negative for decimal places, e.g., -2 for 2 decimal places)
 *
 * This template provides a fixed-point decimal type that maps to Firebird's NUMERIC/DECIMAL types.
 * The value is stored as an integer with an implicit decimal point position determined by Scale.
 *
 * Examples:
 * - TTNumeric<2, -2> for NUMERIC(38,2) - money with 2 decimal places
 * - TTNumeric<2, -4> for NUMERIC(38,4) - percentages with 4 decimal places
 * - TTNumeric<2, -6> for NUMERIC(38,6) - high precision calculations
 */
template<int IntWords, int Scale>
class TTNumeric {
public:
    using IntType = ttmath::Int<IntWords>;
    static constexpr int scale = Scale;

private:
    IntType value_;  // Stored as integer with scale applied

public:
    // Default constructor - zero value
    TTNumeric() : value_(0) {}

    // Constructor from raw integer value (already scaled)
    explicit TTNumeric(const IntType& raw_value) : value_(raw_value) {}

    // Constructor from C-string (decimal representation)
    explicit TTNumeric(const char* str) {
        from_string(std::string(str));
    }

    // Constructor from string (decimal representation)
    explicit TTNumeric(const std::string& str) {
        from_string(str);
    }

    // Constructor from double (for convenience, but may lose precision)
    explicit TTNumeric(double d) {
        from_double(d);
    }

    // Get raw integer value (with scale applied)
    const IntType& raw_value() const { return value_; }
    IntType& raw_value() { return value_; }

    // Convert to string with decimal point
    std::string to_string() const {
        if (scale >= 0) {
            // No decimal places
            return value_.ToString();
        }

        std::string int_str = value_.ToString();
        bool is_negative = false;

        // Handle negative numbers
        if (!int_str.empty() && int_str[0] == '-') {
            is_negative = true;
            int_str = int_str.substr(1);
        }

        int decimal_places = -scale;

        // Pad with zeros if necessary
        while (int_str.length() <= static_cast<size_t>(decimal_places)) {
            int_str = "0" + int_str;
        }

        // Insert decimal point
        size_t point_pos = int_str.length() - decimal_places;
        std::string int_part = int_str.substr(0, point_pos);
        std::string frac_part = int_str.substr(point_pos);

        // Check if fractional part is all zeros
        bool all_zeros = true;
        for (char c : frac_part) {
            if (c != '0') {
                all_zeros = false;
                break;
            }
        }

        std::string result;
        if (all_zeros) {
            // No decimal point needed
            result = int_part;
        } else {
            // Add decimal point and remove trailing zeros
            result = int_part + "." + frac_part;
            while (result.back() == '0' && result[result.length() - 2] != '.') {
                result.pop_back();
            }
        }

        if (is_negative) {
            result = "-" + result;
        }

        return result;
    }

    // Parse from string
    void from_string(const std::string& str) {
        size_t dot_pos = str.find('.');

        if (dot_pos == std::string::npos) {
            // No decimal point - integer value
            value_.FromString(str);

            // Apply scale
            if (scale < 0) {
                for (int i = 0; i < -scale; ++i) {
                    value_ *= 10;
                }
            }
        } else {
            // Has decimal point
            std::string int_part = str.substr(0, dot_pos);
            std::string frac_part = str.substr(dot_pos + 1);

            // Remove trailing zeros from fractional part
            while (!frac_part.empty() && frac_part.back() == '0') {
                frac_part.pop_back();
            }

            // Combine parts
            std::string combined = int_part + frac_part;
            value_.FromString(combined);

            // Adjust for scale difference
            int current_scale = -static_cast<int>(frac_part.length());
            int scale_diff = scale - current_scale;

            if (scale_diff < 0) {
                // Need more decimal places
                for (int i = 0; i < -scale_diff; ++i) {
                    value_ *= 10;
                }
            } else if (scale_diff > 0) {
                // Need fewer decimal places (truncate)
                for (int i = 0; i < scale_diff; ++i) {
                    value_ /= 10;
                }
            }
        }
    }

    // Convert from double (may lose precision)
    void from_double(double d) {
        if (scale >= 0) {
            value_ = static_cast<int64_t>(d);
        } else {
            double scaled = d * std::pow(10.0, -scale);
            value_ = static_cast<int64_t>(std::round(scaled));
        }
    }

    // Convert to double (may lose precision for large values)
    double to_double() const {
        if (scale >= 0) {
            return static_cast<double>(value_.ToInt());
        } else {
            double raw = static_cast<double>(value_.ToInt());
            return raw / std::pow(10.0, -scale);
        }
    }

    // Arithmetic operators
    TTNumeric operator+(const TTNumeric& other) const {
        return TTNumeric(value_ + other.value_);
    }

    TTNumeric operator-(const TTNumeric& other) const {
        return TTNumeric(value_ - other.value_);
    }

    TTNumeric operator*(const TTNumeric& other) const {
        // For multiplication, we need to adjust the scale
        IntType result = value_ * other.value_;

        // Divide by 10^(-scale) to maintain correct scale
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                result /= 10;
            }
        }

        return TTNumeric(result);
    }

    TTNumeric operator/(const TTNumeric& other) const {
        // For division, we need to adjust the scale
        IntType result = value_;

        // Multiply by 10^(-scale) before division to maintain precision
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                result *= 10;
            }
        }

        result /= other.value_;
        return TTNumeric(result);
    }

    // Assignment operators
    TTNumeric& operator+=(const TTNumeric& other) {
        value_ += other.value_;
        return *this;
    }

    TTNumeric& operator-=(const TTNumeric& other) {
        value_ -= other.value_;
        return *this;
    }

    TTNumeric& operator*=(const TTNumeric& other) {
        value_ *= other.value_;

        // Adjust scale for multiplication
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                value_ /= 10;
            }
        }
        return *this;
    }

    TTNumeric& operator/=(const TTNumeric& other) {
        // Adjust scale for division
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                value_ *= 10;
            }
        }

        value_ /= other.value_;
        return *this;
    }

    // Unary operators
    TTNumeric operator-() const {
        IntType neg = value_;
        neg.ChangeSign();
        return TTNumeric(neg);
    }

    TTNumeric operator+() const {
        return *this;
    }

    // Prefix increment/decrement
    TTNumeric& operator++() {
        IntType one(1);
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                one *= 10;
            }
        }
        value_ += one;
        return *this;
    }

    TTNumeric& operator--() {
        IntType one(1);
        if (scale < 0) {
            for (int i = 0; i < -scale; ++i) {
                one *= 10;
            }
        }
        value_ -= one;
        return *this;
    }

    // Postfix increment/decrement
    TTNumeric operator++(int) {
        TTNumeric temp = *this;
        ++(*this);
        return temp;
    }

    TTNumeric operator--(int) {
        TTNumeric temp = *this;
        --(*this);
        return temp;
    }

    // Modulo operator
    TTNumeric operator%(const TTNumeric& other) const {
        return TTNumeric(value_ % other.value_);
    }

    TTNumeric& operator%=(const TTNumeric& other) {
        value_ %= other.value_;
        return *this;
    }

    // Utility functions
    bool is_zero() const {
        return value_.IsZero();
    }

    bool is_negative() const {
        return value_.IsSign();
    }

    bool is_positive() const {
        return !value_.IsZero() && !value_.IsSign();
    }

    TTNumeric abs() const {
        if (!is_negative()) {
            return *this;
        }
        IntType result = value_;
        result.Abs();
        return TTNumeric(result);
    }

    // Comparison operators
    bool operator==(const TTNumeric& other) const {
        return value_ == other.value_;
    }

    bool operator!=(const TTNumeric& other) const {
        return value_ != other.value_;
    }

    bool operator<(const TTNumeric& other) const {
        return value_ < other.value_;
    }

    bool operator<=(const TTNumeric& other) const {
        return value_ <= other.value_;
    }

    bool operator>(const TTNumeric& other) const {
        return value_ > other.value_;
    }

    bool operator>=(const TTNumeric& other) const {
        return value_ >= other.value_;
    }
};

// Common type aliases for financial calculations
using Money = TTNumeric<2, -2>;      // NUMERIC(38,2) for money
using Percent = TTNumeric<2, -4>;    // NUMERIC(38,4) for percentages
using HighPrecision = TTNumeric<2, -6>; // NUMERIC(38,6) for high precision

} // namespace fbpp::adapters

namespace fbpp::core {

// Scale/length-aware adapter for TTNumeric<IntWords, Scale>
template<int IntWords, int Scale>
struct TypeAdapter<fbpp::adapters::TTNumeric<IntWords, Scale>> {
    static constexpr bool is_specialized = true;

    using user_type = fbpp::adapters::TTNumeric<IntWords, Scale>;
    using IntType   = typename user_type::IntType;

    // Пишем little-endian two's complement с учётом масштаба/длины хранения
    //  fb_scale  — scale из Firebird (обычно ОТРИЦАТЕЛЬНЫЙ: -2 => 2 знака)
    //  fb_length — реальная длина хранения целого: 2/4/8/16 байт
    //  out_le    — куда писать байты (little-endian)
    static void to_firebird(const user_type& value,
                            int16_t fb_scale,
                            unsigned fb_length,
                            uint8_t* out_le)
    {
        if (!out_le) return;
        using fbpp::core::detail::mul_pow10;
        using fbpp::core::detail::div_pow10_trunc;
        using fbpp::core::detail::is_negative;

        // 1) Приводим масштаб значения к масштабу столбца БД
        auto big = value.raw_value(); // копия, чтобы не менять source
        const int type_scale_abs = (user_type::scale < 0) ? -user_type::scale : user_type::scale;
        const int db_scale_abs   = (fb_scale         < 0) ? -fb_scale         : fb_scale;

        if (db_scale_abs > type_scale_abs) {
            mul_pow10(big, static_cast<unsigned>(db_scale_abs - type_scale_abs));
        } else if (db_scale_abs < type_scale_abs) {
            div_pow10_trunc(big, static_cast<unsigned>(type_scale_abs - db_scale_abs));
        }

        // 2) Записываем two's complement в fb_length байт (LE) с корректным знаковым заполнением
        const bool neg = is_negative(big);
        std::memset(out_le, neg ? 0xFF : 0x00, fb_length);
        const size_t avail = std::min<size_t>(fb_length, sizeof(big.table));
        std::memcpy(out_le, big.table, avail);

        // TODO (по желанию): проверить переполнение по precision колонки
    }

    // Читаем little-endian two's complement из Firebird и приводим к масштабe типа
    static user_type from_firebird(const uint8_t* in_le,
                                   unsigned fb_length,
                                   int16_t fb_scale)
    {
        using fbpp::core::detail::mul_pow10;
        using fbpp::core::detail::div_pow10_trunc;

        IntType raw{};
        // sign-extend вход в буфер ttmath::Int
        const bool neg = (fb_length > 0) && (in_le[fb_length - 1] & 0x80);
        std::memset(raw.table, neg ? 0xFF : 0x00, sizeof(raw.table));
        const size_t avail = std::min<size_t>(fb_length, sizeof(raw.table));
        std::memcpy(raw.table, in_le, avail);

        user_type val(raw);

        // Привести масштаб БД к масштабу типа
        const int type_scale_abs = (user_type::scale < 0) ? -user_type::scale : user_type::scale;
        const int db_scale_abs   = (fb_scale         < 0) ? -fb_scale         : fb_scale;

        if (type_scale_abs > db_scale_abs) {
            mul_pow10(val.raw_value(), static_cast<unsigned>(type_scale_abs - db_scale_abs));
        } else if (type_scale_abs < db_scale_abs) {
            div_pow10_trunc(val.raw_value(), static_cast<unsigned>(db_scale_abs - type_scale_abs));
        }

        return val;
    }
};

// Helper functions for creating TTNumeric values
template<int IntWords, int Scale>
inline fbpp::adapters::TTNumeric<IntWords, Scale> make_numeric(const std::string& str) {
    return fbpp::adapters::TTNumeric<IntWords, Scale>(str);
}

template<int IntWords, int Scale>
inline fbpp::adapters::TTNumeric<IntWords, Scale> make_numeric(double value) {
    return fbpp::adapters::TTNumeric<IntWords, Scale>(value);
}

// Specialized helpers for common types
inline fbpp::adapters::Money make_money(const std::string& str) {
    return fbpp::adapters::Money(str);
}

inline fbpp::adapters::Money make_money(double value) {
    return fbpp::adapters::Money(value);
}

inline fbpp::adapters::Percent make_percent(const std::string& str) {
    return fbpp::adapters::Percent(str);
}

inline fbpp::adapters::Percent make_percent(double value) {
    return fbpp::adapters::Percent(value);
}

} // namespace fbpp::core