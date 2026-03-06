#include <gtest/gtest.h>

#include "fbpp/core/timestamp_utils.hpp"

#include <chrono>

namespace {

using fbpp::core::timestamp_utils::from_firebird_date;
using fbpp::core::timestamp_utils::from_firebird_timestamp;
using fbpp::core::timestamp_utils::to_firebird_date;
using fbpp::core::timestamp_utils::to_firebird_timestamp;

TEST(TimestampUtilsTest, TimestampRoundTripsRelativeToUnixEpoch) {
    using namespace std::chrono;

    const auto unixEpoch = system_clock::from_time_t(0);
    const auto original = system_clock::from_time_t(1705320645) + microseconds(123456);

    const auto [fbDate, fbTime] = to_firebird_timestamp(original);
    const auto restored = from_firebird_timestamp(fbDate, fbTime);

    const auto originalMicros =
        duration_cast<microseconds>(original - unixEpoch).count();
    const auto restoredMicros =
        duration_cast<microseconds>(restored - unixEpoch).count();

    // Firebird TIMESTAMP stores time in 1/10000 second units (100 microseconds).
    EXPECT_EQ(restoredMicros, (originalMicros / 100) * 100);
}

TEST(TimestampUtilsTest, DateRoundTripsCalendarValue) {
    const std::chrono::year_month_day original{
        std::chrono::year{2024}, std::chrono::June, std::chrono::day{15}};

    const auto fbDate = to_firebird_date(original);
    const auto restored = from_firebird_date(fbDate);

    EXPECT_EQ(restored, original);
}

} // namespace
