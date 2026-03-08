#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/schema/schema_inspector.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

using namespace fbpp::test;
using namespace fbpp::schema;

// ---------------------------------------------------------------------------
// Test fixture: SuiteDatabaseTest with extra DDL (procedure + sequence)
// The database is created once for all tests in this fixture.
// ---------------------------------------------------------------------------

class SchemaInspectorTest : public SuiteDatabaseTest {
protected:
    std::vector<SchemaProfile> schemaProfiles() const override {
        return {SchemaProfile::BasicTable};
    }

    void createTestSchema() override {
        // BasicTable is applied via schemaProfiles(), so only add extras here.
        connection_->ExecuteDDL(
            "CREATE PROCEDURE test_proc (p_in INTEGER)"
            " RETURNS (p_out INTEGER)"
            " AS"
            " BEGIN"
            "   p_out = p_in * 2;"
            " END");

        connection_->ExecuteDDL("CREATE SEQUENCE test_seq INCREMENT BY 5");
    }
};

// ---------------------------------------------------------------------------
// Table existence
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetTableNames_ContainsTestTable) {
    SchemaInspector inspector(*connection_);
    auto names = inspector.getTableNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "TEST_TABLE"), names.end());
}

TEST_F(SchemaInspectorTest, GetTableNames_Sorted) {
    SchemaInspector inspector(*connection_);
    auto names = inspector.getTableNames();
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST_F(SchemaInspectorTest, TableExists_True) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.tableExists("TEST_TABLE"));
}

TEST_F(SchemaInspectorTest, TableExists_CaseInsensitive) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.tableExists("test_table"));
    EXPECT_TRUE(inspector.tableExists("Test_Table"));
}

TEST_F(SchemaInspectorTest, TableExists_False) {
    SchemaInspector inspector(*connection_);
    EXPECT_FALSE(inspector.tableExists("NO_SUCH_TABLE_XYZ"));
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetViewNames_Empty) {
    SchemaInspector inspector(*connection_);
    auto views = inspector.getViewNames();
    // Basic schema has no views
    EXPECT_TRUE(views.empty());
}

TEST_F(SchemaInspectorTest, ViewExists_TableIsNotView) {
    SchemaInspector inspector(*connection_);
    EXPECT_FALSE(inspector.viewExists("TEST_TABLE"));
}

TEST_F(SchemaInspectorTest, ViewExists_False) {
    SchemaInspector inspector(*connection_);
    EXPECT_FALSE(inspector.viewExists("NO_SUCH_VIEW_XYZ"));
}

// ---------------------------------------------------------------------------
// getTableInfo — columns
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetTableInfo_RelationType) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");
    EXPECT_EQ(info.relationType, RelationType::table);
}

TEST_F(SchemaInspectorTest, GetTableInfo_ColumnCount) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");
    // test_table: id INTEGER, name VARCHAR(100), amount DOUBLE PRECISION
    ASSERT_EQ(info.columns.size(), 3u);
}

TEST_F(SchemaInspectorTest, GetTableInfo_ColumnNames) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");

    ASSERT_EQ(info.columns.size(), 3u);
    EXPECT_EQ(info.columns[0].name, "ID");
    EXPECT_EQ(info.columns[1].name, "NAME");
    EXPECT_EQ(info.columns[2].name, "AMOUNT");
}

TEST_F(SchemaInspectorTest, GetTableInfo_IdColumnNotNull) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");
    ASSERT_FALSE(info.columns.empty());
    const auto& idCol = info.columns[0];
    EXPECT_EQ(idCol.name, "ID");
    EXPECT_TRUE(idCol.notNull);
}

TEST_F(SchemaInspectorTest, GetTableInfo_NullableColumn) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");
    // name and amount are nullable
    ASSERT_GE(info.columns.size(), 2u);
    EXPECT_FALSE(info.columns[1].notNull);  // name
    EXPECT_FALSE(info.columns[2].notNull);  // amount
}

// ---------------------------------------------------------------------------
// getTableInfo — primary key
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetTableInfo_HasPrimaryKey) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");

    auto it = std::find_if(info.constraints.begin(), info.constraints.end(),
                           [](const ConstraintInfo& c) {
                               return c.type == ConstraintType::primary_key;
                           });
    ASSERT_NE(it, info.constraints.end());
}

TEST_F(SchemaInspectorTest, GetTableInfo_PrimaryKeyOnId) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");

    auto it = std::find_if(info.constraints.begin(), info.constraints.end(),
                           [](const ConstraintInfo& c) {
                               return c.type == ConstraintType::primary_key;
                           });
    ASSERT_NE(it, info.constraints.end());
    ASSERT_EQ(it->columns.size(), 1u);
    EXPECT_EQ(it->columns[0], "ID");
}

TEST_F(SchemaInspectorTest, GetTableInfo_HasIndexForPK) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("TEST_TABLE");
    // There must be at least one index (backing the PK)
    EXPECT_FALSE(info.indexes.empty());
}

TEST_F(SchemaInspectorTest, GetTableInfo_NonExistent) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getTableInfo("NO_SUCH_TABLE_XYZ");
    EXPECT_EQ(info.relationType, RelationType::unknown);
    EXPECT_TRUE(info.columns.empty());
    EXPECT_TRUE(info.indexes.empty());
    EXPECT_TRUE(info.constraints.empty());
}

// ---------------------------------------------------------------------------
// Stored procedures
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetProcedureNames_ContainsTestProc) {
    SchemaInspector inspector(*connection_);
    auto names = inspector.getProcedureNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "TEST_PROC"), names.end());
}

TEST_F(SchemaInspectorTest, ProcedureExists_True) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.procedureExists("TEST_PROC"));
}

TEST_F(SchemaInspectorTest, ProcedureExists_CaseInsensitive) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.procedureExists("test_proc"));
}

TEST_F(SchemaInspectorTest, ProcedureExists_False) {
    SchemaInspector inspector(*connection_);
    EXPECT_FALSE(inspector.procedureExists("NO_SUCH_PROC_XYZ"));
}

TEST_F(SchemaInspectorTest, GetProcedureInfo_InputParams) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getProcedureInfo("TEST_PROC");
    EXPECT_EQ(info.name, "TEST_PROC");
    ASSERT_EQ(info.inputParams.size(), 1u);
    EXPECT_EQ(info.inputParams[0].name, "P_IN");
    EXPECT_EQ(info.inputParams[0].direction, ParameterDirection::input);
}

TEST_F(SchemaInspectorTest, GetProcedureInfo_OutputParams) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getProcedureInfo("TEST_PROC");
    ASSERT_EQ(info.outputParams.size(), 1u);
    EXPECT_EQ(info.outputParams[0].name, "P_OUT");
    EXPECT_EQ(info.outputParams[0].direction, ParameterDirection::output);
}

TEST_F(SchemaInspectorTest, GetProcedureInfo_SqlType) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getProcedureInfo("TEST_PROC");
    // INTEGER maps to SQL_LONG = 496
    ASSERT_EQ(info.inputParams.size(), 1u);
    EXPECT_EQ(info.inputParams[0].sqlType, 496);
}

TEST_F(SchemaInspectorTest, GetProcedureInfo_NonExistent) {
    SchemaInspector inspector(*connection_);
    auto info = inspector.getProcedureInfo("NO_SUCH_PROC_XYZ");
    EXPECT_TRUE(info.inputParams.empty());
    EXPECT_TRUE(info.outputParams.empty());
}

// ---------------------------------------------------------------------------
// Sequences
// ---------------------------------------------------------------------------

TEST_F(SchemaInspectorTest, GetSequences_ContainsTestSeq) {
    SchemaInspector inspector(*connection_);
    auto seqs = inspector.getSequences();
    auto it = std::find_if(seqs.begin(), seqs.end(),
                           [](const SequenceInfo& s) { return s.name == "TEST_SEQ"; });
    ASSERT_NE(it, seqs.end());
}

TEST_F(SchemaInspectorTest, GetSequences_Increment) {
    SchemaInspector inspector(*connection_);
    auto seqs = inspector.getSequences();
    auto it = std::find_if(seqs.begin(), seqs.end(),
                           [](const SequenceInfo& s) { return s.name == "TEST_SEQ"; });
    ASSERT_NE(it, seqs.end());
    EXPECT_EQ(it->increment, 5);
}

TEST_F(SchemaInspectorTest, GetSequences_CurrentValueIsReadable) {
    SchemaInspector inspector(*connection_);
    auto seqs = inspector.getSequences();
    auto it = std::find_if(seqs.begin(), seqs.end(),
                           [](const SequenceInfo& s) { return s.name == "TEST_SEQ"; });
    ASSERT_NE(it, seqs.end());
    // currentValue = GEN_ID(name, 0): raw internal counter.  For a fresh
    // sequence created with INCREMENT BY 5 and default START WITH 1, Firebird
    // stores -4 (= 1 - 5) so the first NEXT VALUE returns 1.  The value may
    // therefore be negative; we only verify it was successfully fetched (i.e.
    // it is not INT64_MIN, which would indicate a fetch failure).
    EXPECT_GT(it->currentValue, std::numeric_limits<std::int64_t>::min());
}

TEST_F(SchemaInspectorTest, SequenceExists_True) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.sequenceExists("TEST_SEQ"));
}

TEST_F(SchemaInspectorTest, SequenceExists_CaseInsensitive) {
    SchemaInspector inspector(*connection_);
    EXPECT_TRUE(inspector.sequenceExists("test_seq"));
}

TEST_F(SchemaInspectorTest, SequenceExists_False) {
    SchemaInspector inspector(*connection_);
    EXPECT_FALSE(inspector.sequenceExists("NO_SUCH_SEQ_XYZ"));
}
