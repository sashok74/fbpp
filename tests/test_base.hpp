#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp_util/connection_helper.hpp"

namespace fbpp::test {

using namespace fbpp::core;

enum class SchemaProfile {
    BasicTable,
    QueryMetadata,
    TableTest1,
    TTMath,
    DecFloat,
    DateTime,
    Combined,
    ExtendedTypes
};

ConnectionParams makeScopedTestDatabaseParams(std::string_view scope,
                                              std::string_view section = "tests.temp_db");
void recreateTestDatabase(const ConnectionParams& params);
void dropTestDatabaseQuietly(const ConnectionParams& params) noexcept;
void applySchemaProfiles(Connection& connection, const std::vector<SchemaProfile>& profiles);

class FbppTestBase : public ::testing::Test {
protected:
    static void initLoggingFromConfig(const fbpp::util::json&) {
        // Logging subsystem removed; trace observer can be configured separately.
    }
};

class ManagedDatabaseTest : public FbppTestBase {
protected:
    void initializeDatabase();

    virtual std::vector<SchemaProfile> schemaProfiles() const {
        return {};
    }

    virtual void createTestSchema() {}
    virtual void seedTestData() {}

    static long getCurrentProcessId() {
#if defined(_WIN32)
        return static_cast<long>(_getpid());
#else
        return static_cast<long>(getpid());
#endif
    }

    std::unique_ptr<Connection> connection_;
    ConnectionParams db_params_;
};

class SuiteDatabaseTest : public ManagedDatabaseTest {
protected:
    static void TearDownTestSuite();

    void SetUp() override;
    void TearDown() override;
};

using PersistentDatabaseTest = SuiteDatabaseTest;

class TempDatabaseTest : public ManagedDatabaseTest {
protected:
    void SetUp() override;
    void TearDown() override;

    void createTestSchema() override;

private:
    static int test_counter_;
};

} // namespace fbpp::test
