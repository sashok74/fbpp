#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"

// Connection::getEngineMajorVersion() — version probe for FB4+-only types
// (INT128, DECFLOAT, *_TZ). The FB3 throwing path is documented but skipped
// here — running the suite against an FB3 instance is a manual exercise;
// the project targets FB5.

using namespace fbpp::core;
using namespace fbpp::test;

class FbVersionGatingTest : public TempDatabaseTest {};

TEST_F(FbVersionGatingTest, EngineVersionAtLeastThree) {
    int v = connection_->getEngineMajorVersion();
    EXPECT_GE(v, 3);   // sanity bound for any supported FB
}

TEST_F(FbVersionGatingTest, EngineVersionIsCached) {
    int v1 = connection_->getEngineMajorVersion();
    int v2 = connection_->getEngineMajorVersion();
    EXPECT_EQ(v1, v2);
}

TEST_F(FbVersionGatingTest, EngineVersionMatchesCi) {
    // CI / dev workflow targets FB5 per CLAUDE.md. If you run against a
    // newer engine that's still fine.
    EXPECT_GE(connection_->getEngineMajorVersion(), 5);
}
