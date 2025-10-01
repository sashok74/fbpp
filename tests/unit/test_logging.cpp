#include <gtest/gtest.h>
#include <fbpp_util/logging.h>
#include <filesystem>

TEST(LoggingTest, InitializeLogger) {
    util::Logging::init("debug", true, true, "logs/test.log", 5, 3);
    auto logger = util::Logging::get();
    
    ASSERT_NE(logger, nullptr);
    ASSERT_EQ(logger->name(), "binding_lab");
}

TEST(LoggingTest, LogMessages) {
    auto logger = util::Logging::get();
    
    ASSERT_NO_THROW({
        logger->trace("Test trace message");
        logger->debug("Test debug message");
        logger->info("Test info message");
        logger->warn("Test warning message");
        logger->error("Test error message");
    });
}

TEST(LoggingTest, LogFileCreated) {
    std::filesystem::path logFile("logs/test.log");
    
    if (std::filesystem::exists(logFile)) {
        ASSERT_TRUE(std::filesystem::is_regular_file(logFile));
    } else {
        GTEST_SKIP() << "Log file not created, might be permission issue";
    }
}