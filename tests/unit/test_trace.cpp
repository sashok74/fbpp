#include <gtest/gtest.h>
#include <fbpp_util/trace.h>

#include <string>
#include <tuple>
#include <vector>

namespace {

struct CapturingSink : fbpp::util::TraceSink {
    std::vector<std::tuple<fbpp::util::TraceLevel, std::string, std::string>> entries;

    void log(fbpp::util::TraceLevel level,
             std::string_view component,
             std::string_view message) override {
        entries.emplace_back(level, std::string(component), std::string(message));
    }
};

} // namespace

TEST(TraceTest, CapturesFormattedMessage) {
    CapturingSink sink;
    fbpp::util::setTraceSink(&sink);

    fbpp::util::trace(fbpp::util::TraceLevel::info, "TestComponent",
                      [](auto& oss) { oss << "Value=" << 42; });

    fbpp::util::setTraceSink(nullptr);

    ASSERT_EQ(sink.entries.size(), 1);
    EXPECT_EQ(std::get<0>(sink.entries[0]), fbpp::util::TraceLevel::info);
    EXPECT_EQ(std::get<1>(sink.entries[0]), "TestComponent");
    EXPECT_EQ(std::get<2>(sink.entries[0]), "Value=42");
}

TEST(TraceTest, TraceMessageBypassesFormatter) {
    CapturingSink sink;
    fbpp::util::setTraceSink(&sink);

    fbpp::util::traceMessage(fbpp::util::TraceLevel::warn, "TraceTest", "preformatted");

    fbpp::util::setTraceSink(nullptr);

    ASSERT_EQ(sink.entries.size(), 1);
    EXPECT_EQ(std::get<0>(sink.entries[0]), fbpp::util::TraceLevel::warn);
    EXPECT_EQ(std::get<2>(sink.entries[0]), "preformatted");
}

