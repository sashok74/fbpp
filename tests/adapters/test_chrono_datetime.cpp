#include <gtest/gtest.h>

#include "fbpp/adapters/chrono_datetime.hpp"

#include <chrono>
#include <string>

using fbpp::core::LocalTimestamp;
using fbpp::core::ZonedTimestamp;

namespace {

std::chrono::sys_time<std::chrono::microseconds> sampleUtcTime() {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    return sys_days{year{2024} / June / day{1}} + 12h + 34min + 56s + 123400us;
}

} // namespace

TEST(ChronoDateTimeAdapterTest, MakeZonedTimestampFromUtcTime) {
    auto value = fbpp::core::makeZonedTimestamp("Europe/Berlin", sampleUtcTime());

    EXPECT_EQ(std::string(value.get_time_zone()->name()), "Europe/Berlin");
    EXPECT_EQ(value.get_sys_time(), sampleUtcTime());
}

TEST(ChronoDateTimeAdapterTest, MakeZonedTimestampFromLocalTime) {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    LocalTimestamp local = local_days{year{2024} / June / day{1}} + 14h + 34min + 56s + 123400us;
    auto value = fbpp::core::makeZonedTimestamp("Europe/Berlin", local);

    EXPECT_EQ(std::string(value.get_time_zone()->name()), "Europe/Berlin");
    EXPECT_EQ(value.get_local_time(), local);
}

TEST(ChronoDateTimeAdapterTest, ZonedTimestampRoundTripsThroughTimestampTz) {
    auto original = fbpp::core::makeZonedTimestamp("Europe/Berlin", sampleUtcTime());

    auto fbValue = fbpp::core::adapt_to_firebird(original);
    auto restored = fbpp::core::adapt_from_firebird<ZonedTimestamp>(fbValue);

    EXPECT_EQ(restored.get_sys_time(), original.get_sys_time());
    EXPECT_EQ(std::string(restored.get_time_zone()->name()), "Europe/Berlin");
}

TEST(ChronoDateTimeAdapterTest, SupportsZonesOutsideLegacyBuiltInSubset) {
    auto original = fbpp::core::makeZonedTimestamp("Europe/Warsaw", sampleUtcTime());

    auto fbValue = fbpp::core::adapt_to_firebird(original);
    auto restored = fbpp::core::adapt_from_firebird<ZonedTimestamp>(fbValue);

    EXPECT_EQ(restored.get_sys_time(), original.get_sys_time());
    EXPECT_EQ(std::string(restored.get_time_zone()->name()), "Europe/Warsaw");
}
