#pragma once

// Conversion helpers between VCL's TDateTime (RAD Studio / C++Builder) and
// std::chrono::system_clock::time_point.
//
// VCL TDateTime is a plain `double`:
//   - integer part = days since 1899-12-30 (the Delphi epoch),
//   - fractional part = fraction of a day.
//
// std::chrono::system_clock uses the Unix epoch (1970-01-01). The offset
// between the two epochs is exactly 25569 days.
//
// This header has zero dependency on VCL — callers pass a plain double.
// On the VCL side: `chrono_from_tdatetime(static_cast<double>(myTDateTime))`.
//
// Precision: result is rounded to microseconds. Firebird TIMESTAMP itself
// stores at 100-microsecond granularity, so this does not lose Firebird-side
// precision. Round-trip through this helper is microsecond-stable for any
// date a few centuries from 1900.
//
// Negative TDateTime values (dates before 1899-12-30) are NOT handled — VCL
// uses an unusual sign convention where the fractional part is always added
// in positive direction. For those cases use Sysutils' DateTimeToSystemTime
// on the Delphi side first.

#include <chrono>
#include <cmath>
#include <cstdint>

namespace fbpp::util {

// Days from 1899-12-30 to 1970-01-01.
inline constexpr int64_t kTDateTimeEpochOffsetDays = 25569;

// Microseconds per day = 86400 * 1'000'000.
inline constexpr int64_t kMicrosPerDay = 86'400'000'000LL;

// Convert VCL TDateTime (days-since-1899-12-30, as double) to a chrono
// time_point on the system clock. Returns micro-second precision.
inline std::chrono::system_clock::time_point
chrono_from_tdatetime(double tdt) noexcept {
    using namespace std::chrono;
    const double daysSinceUnix = tdt - static_cast<double>(kTDateTimeEpochOffsetDays);
    const double micros = daysSinceUnix * static_cast<double>(kMicrosPerDay);
    const auto rounded = static_cast<int64_t>(std::llround(micros));
    return system_clock::time_point(microseconds(rounded));
}

// Convert chrono time_point back to VCL TDateTime (a plain double).
// The function preserves microsecond precision for typical present-day dates;
// for dates very far from 1900 the trailing micro digits may drift by a few
// microseconds due to double's limited mantissa.
inline double
tdatetime_from_chrono(std::chrono::system_clock::time_point tp) noexcept {
    using namespace std::chrono;
    const auto micros = duration_cast<microseconds>(tp.time_since_epoch()).count();
    return static_cast<double>(micros) / static_cast<double>(kMicrosPerDay)
         + static_cast<double>(kTDateTimeEpochOffsetDays);
}

} // namespace fbpp::util
