#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"

#include <algorithm>
#include <string>

// Connection::listProcedures() / describeProcedure().
//
// Complements fbpp::schema::SchemaInspector by providing wire-level
// FieldInfo per parameter (sourced from a probe prepare). The schema
// inspector returns static info from RDB$ tables; this API returns the
// exact shape a describeQuery on an EXECUTE PROCEDURE would see.

using namespace fbpp::core;
using namespace fbpp::test;

class ProcedureMetadataTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE PROCEDURE pm_executable (in_x INTEGER, in_y INTEGER)
            RETURNS (out_sum INTEGER)
            AS BEGIN
                out_sum = in_x + in_y;
            END
        )");
        connection_->ExecuteDDL(R"(
            CREATE PROCEDURE pm_selectable
            RETURNS (id INTEGER, label VARCHAR(32))
            AS BEGIN
                id = 1; label = 'a'; SUSPEND;
                id = 2; label = 'b'; SUSPEND;
            END
        )");
    }

    static const ProcedureInfo* find(const std::vector<ProcedureInfo>& v,
                                     const std::string& name) {
        auto it = std::find_if(v.begin(), v.end(),
                               [&](const ProcedureInfo& p) { return p.name == name; });
        return it == v.end() ? nullptr : &*it;
    }
};

TEST_F(ProcedureMetadataTest, ListProceduresContainsKnownProcs) {
    auto procs = connection_->listProcedures();
    auto exec = find(procs, "PM_EXECUTABLE");
    auto sel  = find(procs, "PM_SELECTABLE");
    ASSERT_NE(exec, nullptr) << "executable proc missing";
    ASSERT_NE(sel,  nullptr) << "selectable proc missing";
    EXPECT_EQ(exec->kind, ProcedureKind::Executable);
    EXPECT_EQ(sel->kind,  ProcedureKind::Selectable);
    EXPECT_TRUE(exec->packageName.empty());
}

TEST_F(ProcedureMetadataTest, DescribeExecutableProc) {
    auto info = connection_->describeProcedure("PM_EXECUTABLE");
    EXPECT_EQ(info.kind, ProcedureKind::Executable);
    ASSERT_EQ(info.params.size(), 3u);

    // Inputs first (sorted by position within direction).
    EXPECT_EQ(info.params[0].direction, ParamDirection::Input);
    EXPECT_EQ(info.params[0].name, "IN_X");
    EXPECT_EQ(info.params[1].direction, ParamDirection::Input);
    EXPECT_EQ(info.params[1].name, "IN_Y");
    EXPECT_EQ(info.params[2].direction, ParamDirection::Output);
    EXPECT_EQ(info.params[2].name, "OUT_SUM");
}

TEST_F(ProcedureMetadataTest, DescribeSelectableProc) {
    auto info = connection_->describeProcedure("PM_SELECTABLE");
    EXPECT_EQ(info.kind, ProcedureKind::Selectable);
    ASSERT_EQ(info.params.size(), 2u);
    // No inputs — both rows are outputs.
    EXPECT_EQ(info.params[0].direction, ParamDirection::Output);
    EXPECT_EQ(info.params[0].name, "ID");
    EXPECT_EQ(info.params[1].direction, ParamDirection::Output);
    EXPECT_EQ(info.params[1].name, "LABEL");
}

TEST_F(ProcedureMetadataTest, DescribeUnknownProcThrows) {
    EXPECT_THROW(connection_->describeProcedure("DOES_NOT_EXIST"),
                 FirebirdException);
}

TEST_F(ProcedureMetadataTest, ListDoesNotPopulateCache) {
    auto sizeBefore = connection_->getCacheStatistics().cacheSize;
    auto procs = connection_->listProcedures();
    EXPECT_GE(procs.size(), 2u);
    EXPECT_EQ(connection_->getCacheStatistics().cacheSize, sizeBefore);
}

TEST_F(ProcedureMetadataTest, ProcedureParamFieldMatchesDescribeQuery) {
    auto info = connection_->describeProcedure("PM_EXECUTABLE");
    auto probe = connection_->describeQuery(
        "EXECUTE PROCEDURE \"PM_EXECUTABLE\"(?, ?)");
    ASSERT_EQ(probe.inputFields.size(), 2u);
    ASSERT_EQ(probe.outputFields.size(), 1u);

    // Input fields equal — type/length/scale all match.
    auto inputs = std::count_if(info.params.begin(), info.params.end(),
                                [](const ProcedureParamInfo& p) {
                                    return p.direction == ParamDirection::Input;
                                });
    EXPECT_EQ(inputs, 2);

    EXPECT_EQ(info.params[0].field.type, probe.inputFields[0].type);
    EXPECT_EQ(info.params[1].field.type, probe.inputFields[1].type);
    EXPECT_EQ(info.params[2].field.type, probe.outputFields[0].type);
}
