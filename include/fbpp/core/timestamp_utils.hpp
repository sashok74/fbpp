#pragma once

#include <chrono>
#include <utility>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <stdexcept>

#if defined(_MSVC_LANG) && (_MSVC_LANG > __cplusplus)
#define FBPP_EFFECTIVE_CPLUSPLUS _MSVC_LANG
#else
#define FBPP_EFFECTIVE_CPLUSPLUS __cplusplus
#endif

namespace fbpp::core::timestamp_utils {

// Константы
// Разница между Firebird epoch (17.11.1858) и Unix epoch (01.01.1970) в днях
constexpr int32_t FIREBIRD_EPOCH_DIFF = 40587;

// Количество единиц времени в сутках (1/10000 секунды)
constexpr uint32_t TIME_UNITS_PER_DAY = 864000000;

// Секунд в сутках
constexpr int64_t SECONDS_PER_DAY = 86400;

// Микросекунд в сутках
constexpr int64_t MICROS_PER_DAY = 86400000000LL;

/**
 * Преобразует std::chrono::system_clock::time_point в Firebird ISC_TIMESTAMP
 * @param tp Точка времени Unix
 * @return Пара (ISC_DATE, ISC_TIME) для Firebird
 */
inline std::pair<uint32_t, uint32_t> to_firebird_timestamp(
    std::chrono::system_clock::time_point tp) {
    
    // Получаем микросекунды с Unix эпохи
    auto since_epoch = tp.time_since_epoch();
    auto total_micros = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
    
    // Вычисляем дни и время дня
    int64_t days = total_micros / MICROS_PER_DAY;
    int64_t time_of_day_micros = total_micros % MICROS_PER_DAY;
    
    // Обработка отрицательного времени дня (для дат до Unix epoch)
    if (time_of_day_micros < 0) {
        days--;
        time_of_day_micros += MICROS_PER_DAY;
    }
    
    // Конвертируем в формат Firebird
    uint32_t fb_date = static_cast<uint32_t>(days + FIREBIRD_EPOCH_DIFF);
    
    // Конвертируем микросекунды в единицы 1/10000 секунды
    uint32_t fb_time = static_cast<uint32_t>(time_of_day_micros / 100);
    
    return {fb_date, fb_time};
}

/**
 * Преобразует Firebird ISC_TIMESTAMP в std::chrono::system_clock::time_point
 * @param fb_date ISC_DATE (дни с 17.11.1858)
 * @param fb_time ISC_TIME (доли дня в 1/10000 сек)
 * @return Точка времени Unix
 */
inline std::chrono::system_clock::time_point from_firebird_timestamp(
    uint32_t fb_date, uint32_t fb_time) {
    
    // Дни с Unix эпохи
    int64_t days_since_unix = static_cast<int64_t>(fb_date) - FIREBIRD_EPOCH_DIFF;
    
    // Конвертируем время в микросекунды
    int64_t time_micros = static_cast<int64_t>(fb_time) * 100;
    
    // Общее количество микросекунд
    int64_t total_micros = days_since_unix * MICROS_PER_DAY + time_micros;
    
    return std::chrono::system_clock::time_point(
        std::chrono::microseconds(total_micros)
    );
}

/**
 * Преобразует std::chrono::duration в Firebird ISC_TIME
 * @param duration Длительность (время дня)
 * @return ISC_TIME (единицы 1/10000 секунды)
 */
template<typename Rep, typename Period>
inline uint32_t to_firebird_time(std::chrono::duration<Rep, Period> duration) {
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    // Конвертируем микросекунды в единицы 1/10000 секунды
    return static_cast<uint32_t>((micros.count() / 100) % TIME_UNITS_PER_DAY);
}

/**
 * Преобразует Firebird ISC_TIME в std::chrono::duration
 * @param fb_time ISC_TIME (единицы 1/10000 секунды)
 * @return Длительность в микросекундах
 */
inline std::chrono::microseconds from_firebird_time(uint32_t fb_time) {
    return std::chrono::microseconds(static_cast<int64_t>(fb_time) * 100);
}

/**
 * Получает текущее время дня как ISC_TIME
 * @return ISC_TIME для текущего времени дня
 */
inline uint32_t current_time_of_day() {
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now.time_since_epoch());
    auto time_since_midnight = now.time_since_epoch() - today;
    return to_firebird_time(time_since_midnight);
}

#if FBPP_EFFECTIVE_CPLUSPLUS >= 202002L
// C++20 enhanced date/time functions

/**
 * Convert C++20 year_month_day to Firebird DATE
 * @param ymd Calendar date
 * @return ISC_DATE (days since 17.11.1858)
 */
inline uint32_t to_firebird_date(std::chrono::year_month_day ymd) {
    auto dp = std::chrono::sys_days{ymd};
    auto tp = std::chrono::system_clock::time_point{dp};
    auto [fb_date, _] = to_firebird_timestamp(tp);
    return fb_date;
}

/**
 * Convert Firebird DATE to C++20 year_month_day
 * @param fb_date ISC_DATE (days since 17.11.1858)
 * @return Calendar date
 */
inline std::chrono::year_month_day from_firebird_date(uint32_t fb_date) {
    auto tp = from_firebird_timestamp(fb_date, 0);
    auto dp = std::chrono::floor<std::chrono::days>(tp);
    return std::chrono::year_month_day{dp};
}

/**
 * Parse ISO 8601 date/time string
 * @param str ISO 8601 formatted string
 * @return Parsed time_point
 */
inline std::chrono::system_clock::time_point parse_iso8601(const std::string& str) {
    // Simple parser for ISO 8601 format: "2024-12-30T14:30:45"
    // Full implementation would use std::chrono::parse (C++20)

    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail()) {
        throw std::runtime_error("Failed to parse ISO 8601 date: " + str);
    }

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

/**
 * Format time_point as ISO 8601 string
 * @param tp Time point
 * @return ISO 8601 formatted string
 */
inline std::string format_iso8601(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

    // Add microseconds if needed
    auto since_epoch = tp.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch) % 1000000;
    if (micros.count() > 0) {
        oss << '.' << std::setfill('0') << std::setw(6) << micros.count();
    }

    return oss.str();
}

/**
 * Get the current year_month_day
 * @return Current date as year_month_day
 */
inline std::chrono::year_month_day current_date() {
    auto now = std::chrono::system_clock::now();
    auto dp = std::chrono::floor<std::chrono::days>(now);
    return std::chrono::year_month_day{dp};
}

/**
 * Calculate days between two dates
 * @param from Start date
 * @param to End date
 * @return Number of days
 */
inline int days_between(std::chrono::year_month_day from, std::chrono::year_month_day to) {
    auto from_days = std::chrono::sys_days{from};
    auto to_days = std::chrono::sys_days{to};
    auto diff = to_days - from_days;
    return static_cast<int>(diff.count());
}

#endif // FBPP_EFFECTIVE_CPLUSPLUS >= 202002L

} // namespace fbpp::core::timestamp_utils

#undef FBPP_EFFECTIVE_CPLUSPLUS
