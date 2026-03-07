#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/schema/query_analyzer.hpp"
#include "fbpp/schema/query_analysis.hpp"
#include "fbpp/query_generator_service.hpp"

using namespace fbpp::schema;
using namespace fbpp::test;

// ============================================================================
// Static classify tests — no DB connection required
// ============================================================================

TEST(QueryAnalyzerClassify, Select) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("SELECT id FROM t"), QueryKind::select_query);
}

TEST(QueryAnalyzerClassify, Insert) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("INSERT INTO t VALUES (1)"), QueryKind::insert_query);
}

TEST(QueryAnalyzerClassify, Update) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("UPDATE t SET x = 1"), QueryKind::update_query);
}

TEST(QueryAnalyzerClassify, Delete) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("DELETE FROM t WHERE id = 1"), QueryKind::delete_query);
}

TEST(QueryAnalyzerClassify, ExecuteProcedure) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("EXECUTE PROCEDURE my_proc(1)"),
              QueryKind::execute_procedure);
}

TEST(QueryAnalyzerClassify, ExecuteBlock) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("EXECUTE BLOCK AS BEGIN END"),
              QueryKind::execute_block);
}

TEST(QueryAnalyzerClassify, DdlCreate) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("CREATE TABLE foo (id INT)"), QueryKind::ddl);
}

TEST(QueryAnalyzerClassify, DdlAlter) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("ALTER TABLE foo ADD x INT"), QueryKind::ddl);
}

TEST(QueryAnalyzerClassify, DdlDrop) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("DROP TABLE foo"), QueryKind::ddl);
}

TEST(QueryAnalyzerClassify, Unknown) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("MERGE INTO t USING s ON (t.id = s.id)"),
              QueryKind::unknown);
}

TEST(QueryAnalyzerClassify, CaseInsensitive) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("select id from t"), QueryKind::select_query);
    EXPECT_EQ(QueryAnalyzer::classifyQuery("SELECT id from t"), QueryKind::select_query);
    EXPECT_EQ(QueryAnalyzer::classifyQuery("SeLeCt id from t"), QueryKind::select_query);
    EXPECT_EQ(QueryAnalyzer::classifyQuery("execute procedure p"), QueryKind::execute_procedure);
    EXPECT_EQ(QueryAnalyzer::classifyQuery("EXECUTE BLOCK AS BEGIN END"), QueryKind::execute_block);
}

TEST(QueryAnalyzerClassify, LeadingWhitespace) {
    EXPECT_EQ(QueryAnalyzer::classifyQuery("  \t\n  SELECT 1 FROM rdb$database"),
              QueryKind::select_query);
}

// ============================================================================
// Integration tests — require live DB via TempDatabaseTest
// ============================================================================

class QueryAnalyzerTest : public TempDatabaseTest {};

TEST_F(QueryAnalyzerTest, SelectWithNamedParams) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("SELECT id, name FROM test_table WHERE id = :id");

    EXPECT_EQ(analysis.kind, QueryKind::select_query);
    EXPECT_TRUE(analysis.hasNamedParameters);
    EXPECT_NE(analysis.positionalSql.find('?'), std::string::npos);
    EXPECT_EQ(analysis.originalSql, "SELECT id, name FROM test_table WHERE id = :id");
    ASSERT_EQ(analysis.inputParams.size(), 1u);
    EXPECT_EQ(analysis.inputParams[0].sqlName, "id");
    EXPECT_EQ(analysis.inputParams[0].ordinal, 0u);
    EXPECT_FALSE(analysis.inputParams[0].repeated);
    ASSERT_FALSE(analysis.outputFields.empty());
}

TEST_F(QueryAnalyzerTest, SelectWithoutParams) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("SELECT id, name FROM test_table");

    EXPECT_EQ(analysis.kind, QueryKind::select_query);
    EXPECT_FALSE(analysis.hasNamedParameters);
    EXPECT_EQ(analysis.positionalSql, analysis.originalSql);
    EXPECT_TRUE(analysis.inputParams.empty());
    ASSERT_FALSE(analysis.outputFields.empty());
}

TEST_F(QueryAnalyzerTest, InsertWithOutput) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze(
        "INSERT INTO test_table (id, name) VALUES (:id, :name) RETURNING id");

    EXPECT_EQ(analysis.kind, QueryKind::insert_query);
    EXPECT_TRUE(analysis.hasNamedParameters);
    ASSERT_EQ(analysis.inputParams.size(), 2u);
    EXPECT_EQ(analysis.inputParams[0].sqlName, "id");
    EXPECT_EQ(analysis.inputParams[1].sqlName, "name");
    ASSERT_EQ(analysis.outputFields.size(), 1u);
    EXPECT_EQ(analysis.outputFields[0].ordinal, 0u);
}

TEST_F(QueryAnalyzerTest, RepeatedNamedParam) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze(
        "SELECT id FROM test_table WHERE id = :id OR id = :id");

    EXPECT_TRUE(analysis.hasNamedParameters);
    ASSERT_GE(analysis.inputParams.size(), 1u);
    EXPECT_TRUE(analysis.inputParams[0].repeated);
}

TEST_F(QueryAnalyzerTest, OriginalSqlNotModified) {
    const std::string sql = "SELECT id FROM test_table WHERE id = :id";
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze(sql);
    EXPECT_EQ(analysis.originalSql, sql);
}

TEST_F(QueryAnalyzerTest, OutputFieldOrdinalsAreSequential) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("SELECT id, name FROM test_table");

    for (std::size_t i = 0; i < analysis.outputFields.size(); ++i) {
        EXPECT_EQ(analysis.outputFields[i].ordinal, i);
    }
}

TEST_F(QueryAnalyzerTest, ClassifyInsert) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("INSERT INTO test_table (id, name) VALUES (1, 'x')");
    EXPECT_EQ(analysis.kind, QueryKind::insert_query);
}

TEST_F(QueryAnalyzerTest, ClassifyUpdate) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("UPDATE test_table SET name = 'y' WHERE id = 1");
    EXPECT_EQ(analysis.kind, QueryKind::update_query);
}

TEST_F(QueryAnalyzerTest, ClassifyDelete) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze("DELETE FROM test_table WHERE id = 1");
    EXPECT_EQ(analysis.kind, QueryKind::delete_query);
}

TEST_F(QueryAnalyzerTest, NamedParamNamesPreservedAsSqlName) {
    // Firebird does not expose named param names in metadata;
    // QueryAnalyzer must preserve them from the parse result.
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze(
        "SELECT id FROM test_table WHERE id = :my_id AND name = :my_name");

    ASSERT_EQ(analysis.inputParams.size(), 2u);
    EXPECT_EQ(analysis.inputParams[0].sqlName, "my_id");
    EXPECT_EQ(analysis.inputParams[1].sqlName, "my_name");
}

TEST_F(QueryAnalyzerTest, MemberNameIsCamelCase) {
    QueryAnalyzer analyzer(*connection_);
    auto analysis = analyzer.analyze(
        "SELECT id FROM test_table WHERE id = :my_param_name");

    ASSERT_EQ(analysis.inputParams.size(), 1u);
    EXPECT_EQ(analysis.inputParams[0].memberName, "myParamName");
}

TEST_F(QueryAnalyzerTest, AnalyzeInputParamsMethod) {
    QueryAnalyzer analyzer(*connection_);
    auto params = analyzer.analyzeInputParams(
        "SELECT id FROM test_table WHERE id = :x");

    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].sqlName, "x");
    EXPECT_EQ(params[0].memberName, "x");
    EXPECT_EQ(params[0].ordinal, 0u);
}

TEST_F(QueryAnalyzerTest, AnalyzeOutputFieldsMethod) {
    QueryAnalyzer analyzer(*connection_);
    auto fields = analyzer.analyzeOutputFields("SELECT id, name FROM test_table");

    ASSERT_GE(fields.size(), 2u);
    EXPECT_EQ(fields[0].ordinal, 0u);
    EXPECT_EQ(fields[1].ordinal, 1u);
}

// ============================================================================
// Pipeline tests — verify FieldSpec names flow correctly from QueryAnalyzer
// through QueryGeneratorService into the generated QuerySpec.
// ============================================================================

class QueryAnalyzerPipelineTest : public TempDatabaseTest {};

TEST_F(QueryAnalyzerPipelineTest, FieldSpecSqlNameFromNamedParam) {
    fbpp::core::QueryGeneratorService svc(*connection_);
    auto specs = svc.buildQuerySpecs(
        {{"GetById", "SELECT id, name FROM test_table WHERE id = :my_id"}});

    ASSERT_EQ(specs.size(), 1u);
    const auto& spec = specs[0];

    // Input: named param "my_id" must survive into FieldSpec.sqlName.
    ASSERT_EQ(spec.inputs.size(), 1u);
    EXPECT_EQ(spec.inputs[0].sqlName, "my_id");
    EXPECT_EQ(spec.inputs[0].memberName, "myId");
}

TEST_F(QueryAnalyzerPipelineTest, FieldSpecOutputOrdinalsViaPosition) {
    fbpp::core::QueryGeneratorService svc(*connection_);
    auto specs = svc.buildQuerySpecs(
        {{"GetAll", "SELECT id, name FROM test_table"}});

    ASSERT_EQ(specs.size(), 1u);
    ASSERT_GE(specs[0].outputs.size(), 2u);

    // Output fields appear in SELECT order; position in vector IS the ordinal.
    EXPECT_EQ(specs[0].outputs[0].sqlName, "ID");
    EXPECT_EQ(specs[0].outputs[1].sqlName, "NAME");
}

TEST_F(QueryAnalyzerPipelineTest, MultipleNamedParamsPreserveAllNames) {
    fbpp::core::QueryGeneratorService svc(*connection_);
    auto specs = svc.buildQuerySpecs(
        {{"Insert", "INSERT INTO test_table (id, name) VALUES (:row_id, :row_name)"}});

    ASSERT_EQ(specs.size(), 1u);
    ASSERT_EQ(specs[0].inputs.size(), 2u);
    EXPECT_EQ(specs[0].inputs[0].sqlName, "row_id");
    EXPECT_EQ(specs[0].inputs[0].memberName, "rowId");
    EXPECT_EQ(specs[0].inputs[1].sqlName, "row_name");
    EXPECT_EQ(specs[0].inputs[1].memberName, "rowName");
}
