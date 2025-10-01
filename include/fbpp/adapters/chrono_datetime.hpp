#pragma once

#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/timestamp_utils.hpp"
#include "fbpp/core/extended_types.hpp"
#include <chrono>
#include <string>
#include <map>

#if __cplusplus >= 202002L

namespace fbpp::core {

// Type adapter for std::chrono::year_month_day
template<>
struct TypeAdapter<std::chrono::year_month_day> {
    static constexpr bool is_specialized = true;

    using user_type = std::chrono::year_month_day;
    using firebird_type = Date;

    static firebird_type to_firebird(const user_type& ymd) {
        auto dp = std::chrono::sys_days{ymd};
        auto tp = std::chrono::system_clock::time_point{dp};
        auto [fb_date, _] = timestamp_utils::to_firebird_timestamp(tp);
        return Date(fb_date);
    }

    static user_type from_firebird(const firebird_type& date) {
        auto tp = timestamp_utils::from_firebird_timestamp(date.getDate(), 0);
        auto dp = std::chrono::floor<std::chrono::days>(tp);
        return std::chrono::year_month_day{dp};
    }
};

// Type adapter for std::chrono::hh_mm_ss<std::chrono::microseconds>
template<>
struct TypeAdapter<std::chrono::hh_mm_ss<std::chrono::microseconds>> {
    static constexpr bool is_specialized = true;

    using user_type = std::chrono::hh_mm_ss<std::chrono::microseconds>;
    using firebird_type = Time;

    static firebird_type to_firebird(const user_type& hms) {
        return Time(timestamp_utils::to_firebird_time(hms.to_duration()));
    }

    static user_type from_firebird(const firebird_type& time) {
        auto micros = timestamp_utils::from_firebird_time(time.getTime());
        return user_type{micros};
    }
};

// Type adapter for std::chrono::system_clock::time_point (for TIMESTAMP)
template<>
struct TypeAdapter<std::chrono::system_clock::time_point> {
    static constexpr bool is_specialized = true;

    using user_type = std::chrono::system_clock::time_point;
    using firebird_type = Timestamp;

    static firebird_type to_firebird(const user_type& tp) {
        auto [fb_date, fb_time] = timestamp_utils::to_firebird_timestamp(tp);
        return Timestamp(fb_date, fb_time);
    }

    static user_type from_firebird(const firebird_type& ts) {
        return timestamp_utils::from_firebird_timestamp(ts.getDate(), ts.getTime());
    }
};

// Type adapter for std::chrono::zoned_time<std::chrono::microseconds>
template<>
struct TypeAdapter<std::chrono::zoned_time<std::chrono::microseconds>> {
    static constexpr bool is_specialized = true;

    using user_type = std::chrono::zoned_time<std::chrono::microseconds>;
    using firebird_type = TimestampTz;

    // Simple IANA timezone name to ID mapping (subset for demo)
    static uint16_t get_timezone_id(const std::string& tz_name) {
        static const std::map<std::string, uint16_t> tz_map = {
            {"UTC", 0},
            {"Europe/London", 1},
            {"Europe/Paris", 2},
            {"Europe/Berlin", 3},
            {"Europe/Moscow", 4},
            {"America/New_York", 100},
            {"America/Chicago", 101},
            {"America/Denver", 102},
            {"America/Los_Angeles", 103},
            {"Asia/Tokyo", 200},
            {"Asia/Shanghai", 201},
            {"Asia/Kolkata", 202},
            {"Australia/Sydney", 300},
            {"Pacific/Auckland", 400}
        };

        auto it = tz_map.find(tz_name);
        return (it != tz_map.end()) ? it->second : 0;
    }

    static std::string get_timezone_name(uint16_t zone_id) {
        static const std::map<uint16_t, std::string> id_map = {
            {0, "UTC"},
            {1, "Europe/London"},
            {2, "Europe/Paris"},
            {3, "Europe/Berlin"},
            {4, "Europe/Moscow"},
            {100, "America/New_York"},
            {101, "America/Chicago"},
            {102, "America/Denver"},
            {103, "America/Los_Angeles"},
            {200, "Asia/Tokyo"},
            {201, "Asia/Shanghai"},
            {202, "Asia/Kolkata"},
            {300, "Australia/Sydney"},
            {400, "Pacific/Auckland"}
        };

        auto it = id_map.find(zone_id);
        return (it != id_map.end()) ? it->second : "UTC";
    }

    static firebird_type to_firebird(const user_type& zt) {
        // Get UTC time
        auto utc_tp = zt.get_sys_time();
        auto [fb_date, fb_time] = timestamp_utils::to_firebird_timestamp(utc_tp);

        // Get timezone info
        std::string tz_name(zt.get_time_zone()->name());
        uint16_t zone_id = get_timezone_id(tz_name);

        // Calculate offset in minutes
        auto info = zt.get_time_zone()->get_info(utc_tp);
        auto offset = std::chrono::duration_cast<std::chrono::minutes>(info.offset);
        int16_t offset_minutes = static_cast<int16_t>(offset.count());

        return TimestampTz(fb_date, fb_time, zone_id, offset_minutes);
    }

    static user_type from_firebird(const firebird_type& fb_tz) {
        // Reconstruct UTC time
        auto utc_tp = timestamp_utils::from_firebird_timestamp(
            fb_tz.getDate(), fb_tz.getTime()
        );

        // Find timezone name from ID
        std::string tz_name = get_timezone_name(fb_tz.getZoneId());

        return user_type{tz_name,
                        std::chrono::time_point_cast<std::chrono::microseconds>(utc_tp)};
    }
};

// Type adapter for TIME WITH TIME ZONE using pair of time + timezone name
// Since C++20 doesn't have a standard type for time-only with timezone,
// we use a pair of hh_mm_ss and timezone name
using TimeWithTz = std::pair<std::chrono::hh_mm_ss<std::chrono::microseconds>, std::string>;

template<>
struct TypeAdapter<TimeWithTz> {
    static constexpr bool is_specialized = true;

    using user_type = TimeWithTz;
    using firebird_type = TimeTz;

    static firebird_type to_firebird(const user_type& time_tz) {
        // Convert time to Firebird format
        uint32_t fb_time = timestamp_utils::to_firebird_time(time_tz.first.to_duration());

        // Get timezone ID from name
        uint16_t zone_id = TypeAdapter<std::chrono::zoned_time<std::chrono::microseconds>>::get_timezone_id(time_tz.second);

        // For time-only, we need to calculate offset based on current date
        // This is a simplification - real implementation might need a reference date
        auto now = std::chrono::system_clock::now();
        auto now_micros = std::chrono::time_point_cast<std::chrono::microseconds>(now);
        std::chrono::zoned_time<std::chrono::microseconds> zt{time_tz.second, now_micros};
        auto info = zt.get_time_zone()->get_info(now_micros);
        auto offset_minutes = std::chrono::duration_cast<std::chrono::minutes>(info.offset);

        return TimeTz(fb_time, zone_id, static_cast<int16_t>(offset_minutes.count()));
    }

    static user_type from_firebird(const firebird_type& time_tz) {
        // Convert time from Firebird format
        auto micros = timestamp_utils::from_firebird_time(time_tz.getTime());
        std::chrono::hh_mm_ss<std::chrono::microseconds> hms{micros};

        // Get timezone name from ID
        std::string tz_name = TypeAdapter<std::chrono::zoned_time<std::chrono::microseconds>>::get_timezone_name(time_tz.getZoneId());

        return std::make_pair(hms, tz_name);
    }
};

} // namespace fbpp::core

#endif // C++20