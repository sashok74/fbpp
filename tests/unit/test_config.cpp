#include <gtest/gtest.h>
#include <fbpp_util/config.h>
#include <fstream>
#include <cstdlib>

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test config file
        std::ofstream file("test_config.json");
        file << R"({
            "db": {
                "server": "testserver",
                "path": "/test/path.fdb",
                "user": "testuser",
                "password": "testpass",
                "charset": "UTF8",
                "create_if_missing": false,
                "drop_on_cleanup": false
            },
            "logging": {
                "level": "debug",
                "console": false,
                "file": true,
                "file_path": "test.log",
                "rotate_max_size_mb": 10,
                "rotate_max_files": 5
            },
            "tests": {
                "skip_create_schema": true
            }
        })";
        file.close();
    }
    
    void TearDown() override {
        std::filesystem::remove("test_config.json");
    }
};

TEST_F(ConfigTest, LoadFromJson) {
    ASSERT_TRUE(util::Config::load("test_config.json"));
    
    EXPECT_EQ(util::Config::db().server, "testserver");
    EXPECT_EQ(util::Config::db().path, "/test/path.fdb");
    EXPECT_EQ(util::Config::db().user, "testuser");
    EXPECT_EQ(util::Config::db().password, "testpass");
    EXPECT_EQ(util::Config::db().charset, "UTF8");
    EXPECT_FALSE(util::Config::db().createIfMissing);
    EXPECT_FALSE(util::Config::db().dropOnCleanup);
    
    EXPECT_EQ(util::Config::logging().level, "debug");
    EXPECT_FALSE(util::Config::logging().console);
    EXPECT_TRUE(util::Config::logging().file);
    EXPECT_EQ(util::Config::logging().filePath, "test.log");
    EXPECT_EQ(util::Config::logging().rotateMaxSizeMb, 10);
    EXPECT_EQ(util::Config::logging().rotateMaxFiles, 5);
    
    EXPECT_TRUE(util::Config::tests().skipCreateSchema);
}

TEST_F(ConfigTest, EnvOverridesJson) {
    setenv("FBLAB_DB_USER", "envuser", 1);
    setenv("FBLAB_DB_PATH", "/env/path.fdb", 1);
    setenv("FBLAB_LOG_LEVEL", "info", 1);
    
    ASSERT_TRUE(util::Config::load("test_config.json"));
    
    EXPECT_EQ(util::Config::db().user, "envuser");
    EXPECT_EQ(util::Config::db().path, "/env/path.fdb");
    EXPECT_EQ(util::Config::logging().level, "info");
    
    // Values not overridden by env should remain from JSON
    EXPECT_EQ(util::Config::db().server, "testserver");
    EXPECT_EQ(util::Config::db().password, "testpass");
    
    unsetenv("FBLAB_DB_USER");
    unsetenv("FBLAB_DB_PATH");
    unsetenv("FBLAB_LOG_LEVEL");
}