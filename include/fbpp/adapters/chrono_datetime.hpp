#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/timestamp_utils.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <array>
#include <chrono>
#include <string>

#if defined(_MSVC_LANG) && (_MSVC_LANG > __cplusplus)
#define FBPP_CHRONO_DATETIME_CPLUSPLUS _MSVC_LANG
#else
#define FBPP_CHRONO_DATETIME_CPLUSPLUS __cplusplus
#endif

#if FBPP_CHRONO_DATETIME_CPLUSPLUS >= 202002L

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

// Primary chrono adapter for Firebird TIMESTAMP WITH TIME ZONE.
//
// TIME WITH TIME ZONE intentionally stays on fbpp::core::TimeTz because
// time-only values do not carry the date context needed for robust timezone
// and DST conversion.
//
// Gated off Embarcadero/Borland (bcc64x clang in RAD Studio 13) explicitly:
// its mingw-w64 libstdc++ doesn't ship zoned_time / time_zone. We do not
// use __cpp_lib_chrono here because libstdc++ 13 leaves the macro at the
// C++17 value even though it provides zoned_time.
#if !defined(__BORLANDC__)
template<>
struct TypeAdapter<std::chrono::zoned_time<std::chrono::microseconds>> {
    static constexpr bool is_specialized = true;

    using user_type = ZonedTimestamp;
    using firebird_type = TimestampTz;

    static unsigned to_firebird_fractions(std::chrono::microseconds value) {
        return static_cast<unsigned>(value.count() / 100);
    }

    static std::chrono::microseconds from_firebird_fractions(unsigned value) {
        return std::chrono::microseconds(static_cast<int64_t>(value) * 100);
    }

    static firebird_type to_firebird(const user_type& zt) {
        try {
            auto& env = Environment::getInstance();
            Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
            Firebird::IUtil* util = env.getUtil();

            const auto localTime = std::chrono::time_point_cast<std::chrono::microseconds>(zt.get_local_time());
            const auto localDay = std::chrono::floor<std::chrono::days>(localTime);
            const auto ymd = std::chrono::year_month_day{localDay};
            const auto timeOfDay = std::chrono::hh_mm_ss<std::chrono::microseconds>{
                std::chrono::duration_cast<std::chrono::microseconds>(localTime - localDay)
            };

            ISC_TIMESTAMP_TZ raw{};
            std::string timeZoneName(zt.get_time_zone()->name());
            util->encodeTimeStampTz(
                &status,
                &raw,
                static_cast<unsigned>(static_cast<int>(ymd.year())),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                static_cast<unsigned>(timeOfDay.hours().count()),
                static_cast<unsigned>(timeOfDay.minutes().count()),
                static_cast<unsigned>(timeOfDay.seconds().count()),
                to_firebird_fractions(timeOfDay.subseconds()),
                timeZoneName.c_str()
            );

            const auto info = zt.get_time_zone()->get_info(zt.get_sys_time());
            const auto offset = std::chrono::duration_cast<std::chrono::minutes>(info.offset);

            return TimestampTz(
                static_cast<uint32_t>(raw.utc_timestamp.timestamp_date),
                static_cast<uint32_t>(raw.utc_timestamp.timestamp_time),
                static_cast<uint16_t>(raw.time_zone),
                static_cast<int16_t>(offset.count())
            );
        } catch (const Firebird::FbException& e) {
            throw FirebirdException(e);
        }
    }

    static user_type from_firebird(const firebird_type& fb_tz) {
        try {
            auto& env = Environment::getInstance();
            Firebird::ThrowStatusWrapper status(env.getMaster()->getStatus());
            Firebird::IUtil* util = env.getUtil();

            ISC_TIMESTAMP_TZ raw{};
            raw.utc_timestamp.timestamp_date = static_cast<ISC_DATE>(fb_tz.getDate());
            raw.utc_timestamp.timestamp_time = static_cast<ISC_TIME>(fb_tz.getTime());
            raw.time_zone = static_cast<ISC_USHORT>(fb_tz.getZoneId());

            unsigned year{};
            unsigned month{};
            unsigned day{};
            unsigned hours{};
            unsigned minutes{};
            unsigned seconds{};
            unsigned fractions{};
            std::array<char, 128> timeZoneName{};

            util->decodeTimeStampTz(
                &status,
                &raw,
                &year,
                &month,
                &day,
                &hours,
                &minutes,
                &seconds,
                &fractions,
                static_cast<unsigned>(timeZoneName.size()),
                timeZoneName.data()
            );

            (void) year;
            (void) month;
            (void) day;
            (void) hours;
            (void) minutes;
            (void) seconds;
            (void) fractions;

            auto utcTime = timestamp_utils::from_firebird_timestamp(fb_tz.getDate(), fb_tz.getTime());
            return makeZonedTimestamp(
                std::string_view(timeZoneName.data()),
                std::chrono::time_point_cast<std::chrono::microseconds>(utcTime)
            );
        } catch (const Firebird::FbException& e) {
            throw FirebirdException(e);
        } catch (const std::runtime_error& e) {
            throw FirebirdException(std::string("Failed to construct ZonedTimestamp: ") + e.what());
        }
    }
};
#endif // !defined(__BORLANDC__)

} // namespace fbpp::core

#endif // FBPP_CHRONO_DATETIME_CPLUSPLUS >= 202002L

#undef FBPP_CHRONO_DATETIME_CPLUSPLUS
