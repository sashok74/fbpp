#include <gtest/gtest.h>

#include "fbpp_util/tdatetime.hpp"

#include <chrono>

using namespace std::chrono;
using fbpp::util::chrono_from_tdatetime;
using fbpp::util::tdatetime_from_chrono;
using fbpp::util::kTDateTimeEpochOffsetDays;

namespace {

// Build a system_clock::time_point from a calendar date + time of day.
system_clock::time_point makeTime(int y, int mo, int d,
                                  int hh = 0, int mm = 0, int ss = 0,
                                  int us = 0) {
    sys_days date = year_month_day{year{y}, month(static_cast<unsigned>(mo)), day(static_cast<unsigned>(d))};
    auto tod = hours{hh} + minutes{mm} + seconds{ss} + microseconds{us};
    return system_clock::time_point{date.time_since_epoch() + tod};
}

} // namespace

// 1899-12-30 (Delphi epoch) corresponds to TDateTime == 0.
TEST(TDateTimeHelper, DelphiEpochIsZero) {
    auto tp = makeTime(1899, 12, 30);
    double tdt = tdatetime_from_chrono(tp);
    EXPECT_DOUBLE_EQ(tdt, 0.0);
}

// 1970-01-01 (Unix epoch) corresponds to TDateTime == 25569.
TEST(TDateTimeHelper, UnixEpochOffset) {
    auto tp = system_clock::time_point{};
    double tdt = tdatetime_from_chrono(tp);
    EXPECT_DOUBLE_EQ(tdt, static_cast<double>(kTDateTimeEpochOffsetDays));

    auto round = chrono_from_tdatetime(static_cast<double>(kTDateTimeEpochOffsetDays));
    EXPECT_EQ(round, tp);
}

// Mid-day fraction: noon corresponds to TDateTime fractional part 0.5.
TEST(TDateTimeHelper, MiddayFraction) {
    auto tp = makeTime(2024, 6, 15, 12, 0, 0);
    double tdt = tdatetime_from_chrono(tp);
    double fractional = tdt - std::floor(tdt);
    EXPECT_NEAR(fractional, 0.5, 1e-9);
}

// Round-trip: chrono → TDateTime → chrono — microsecond-stable.
TEST(TDateTimeHelper, RoundTripChronoToChrono) {
    auto original = makeTime(2024, 1, 15, 10, 30, 45, 123456);
    double tdt = tdatetime_from_chrono(original);
    auto back = chrono_from_tdatetime(tdt);

    auto delta = duration_cast<microseconds>(back - original).count();
    // For dates this close to 2000 the round-trip is exact at micro level.
    EXPECT_LE(std::abs(delta), 1) << "delta=" << delta << "us";
}

// Round-trip the other way: a known TDateTime value remains stable.
TEST(TDateTimeHelper, RoundTripTDateTimeToTDateTime) {
    // 2025-05-08 13:37:42 — pick a date with non-trivial fractional part.
    double original = 45785.0
                    + (13.0 / 24.0)
                    + (37.0 / (24.0 * 60.0))
                    + (42.0 / (24.0 * 60.0 * 60.0));
    auto tp = chrono_from_tdatetime(original);
    double back = tdatetime_from_chrono(tp);
    EXPECT_NEAR(back, original, 1e-9);
}

// One-second precision survives at year-2024 magnitudes.
TEST(TDateTimeHelper, OneSecondPrecision) {
    auto tp1 = makeTime(2024, 6, 15, 12, 0, 0);
    auto tp2 = makeTime(2024, 6, 15, 12, 0, 1);
    double t1 = tdatetime_from_chrono(tp1);
    double t2 = tdatetime_from_chrono(tp2);
    double oneSecondInDays = 1.0 / (24.0 * 60.0 * 60.0);
    // 1e-10 days ≈ 8.6 µs — comfortably above double's relative precision
    // (~1e-16) at year-2024 magnitudes.
    EXPECT_NEAR(t2 - t1, oneSecondInDays, 1e-10);
}
