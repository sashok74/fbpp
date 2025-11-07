#include <gtest/gtest.h>
#include <fbpp_util/trace.h>

#include <string>
#include <tuple>
#include <vector>

namespace {

struct CapturingSink : util::TraceSink {
    std::vector<std::tuple<util::TraceLevel, std::string, std::string>> entries;

    void log(util::TraceLevel level,
             std::string_view component,
             std::string_view message) override {
        entries.emplace_back(level, std::string(component), std::string(message));
    }
};

} // namespace

TEST(TraceTest, CapturesFormattedMessage) {
    CapturingSink sink;
    util::setTraceSink(&sink);

    util::trace(util::TraceLevel::info, "TestComponent",
                [](auto& oss) { oss << "Value=" << 42; });

    util::setTraceSink(nullptr);

    ASSERT_EQ(sink.entries.size(), 1);
    EXPECT_EQ(std::get<0>(sink.entries[0]), util::TraceLevel::info);
    EXPECT_EQ(std::get<1>(sink.entries[0]), "TestComponent");
    EXPECT_EQ(std::get<2>(sink.entries[0]), "Value=42");
}

TEST(TraceTest, TraceMessageBypassesFormatter) {
    CapturingSink sink;
    util::setTraceSink(&sink);

    util::traceMessage(util::TraceLevel::warn, "TraceTest", "preformatted");

    util::setTraceSink(nullptr);

    ASSERT_EQ(sink.entries.size(), 1);
    EXPECT_EQ(std::get<0>(sink.entries[0]), util::TraceLevel::warn);
    EXPECT_EQ(std::get<2>(sink.entries[0]), "preformatted");
}

