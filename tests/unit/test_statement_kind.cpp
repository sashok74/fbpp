#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

// Statement::kind() / hasOutput() — semantic enum + output check over the
// raw isc_info_sql_stmt_* code returned by getType().

class StatementKindTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();

        connection_->ExecuteDDL(R"(
            CREATE TABLE k_dml (
                id INTEGER NOT NULL PRIMARY KEY,
                v VARCHAR(32)
            )
        )");

        // Selectable proc — returns rows via SUSPEND.
        connection_->ExecuteDDL(R"(
            CREATE PROCEDURE k_sel_proc
            RETURNS (out_id INTEGER)
            AS BEGIN
                out_id = 1;
                SUSPEND;
            END
        )");

        // Executable proc — sets OUT params, no SUSPEND.
        connection_->ExecuteDDL(R"(
            CREATE PROCEDURE k_exec_proc (in_x INTEGER)
            RETURNS (out_doubled INTEGER)
            AS BEGIN
                out_doubled = in_x * 2;
            END
        )");
    }
};

TEST_F(StatementKindTest, KindSelect) {
    auto stmt = connection_->prepareStatement("SELECT 1 FROM RDB$DATABASE");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Select);
    EXPECT_TRUE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindInsert) {
    auto stmt = connection_->prepareStatement(
        "INSERT INTO k_dml (id, v) VALUES (?, ?)");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Dml);
    EXPECT_FALSE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindUpdate) {
    auto stmt = connection_->prepareStatement(
        "UPDATE k_dml SET v = ? WHERE id = ?");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Dml);
    EXPECT_FALSE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindDelete) {
    auto stmt = connection_->prepareStatement(
        "DELETE FROM k_dml WHERE id = ?");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Dml);
    EXPECT_FALSE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindDdl) {
    // CREATE TABLE goes through DDL parser — type code 5 (isc_info_sql_stmt_ddl).
    auto stmt = connection_->prepareStatement(
        "CREATE TABLE k_ddl_probe (x INTEGER)");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Ddl);
    EXPECT_FALSE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindExecuteProcedureExecutable) {
    // EXECUTE PROCEDURE on an executable proc → exec_procedure (8).
    auto stmt = connection_->prepareStatement(
        "EXECUTE PROCEDURE k_exec_proc(?)");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::ExecuteProcedure);
    // Executable procs with OUT params do return one row of OUTs via execute().
    EXPECT_TRUE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindSelectFromSelectableProc) {
    // SELECT * FROM selectable_proc — pure SELECT cursor.
    auto stmt = connection_->prepareStatement(
        "SELECT out_id FROM k_sel_proc");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Select);
    EXPECT_TRUE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindExecuteBlockWithOutput) {
    // EXECUTE BLOCK RETURNS ... → engine reports as Select (cursor-producing).
    auto stmt = connection_->prepareStatement(R"(
        EXECUTE BLOCK
        RETURNS (n INTEGER)
        AS BEGIN
            n = 42;
            SUSPEND;
        END
    )");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Select);
    EXPECT_TRUE(stmt->hasOutput());
}

TEST_F(StatementKindTest, KindExecuteBlockNoOutput) {
    // EXECUTE BLOCK without RETURNS → exec_procedure code, no output.
    auto stmt = connection_->prepareStatement(R"(
        EXECUTE BLOCK AS
        DECLARE VARIABLE x INTEGER;
        BEGIN
            x = 1;
        END
    )");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::ExecuteProcedure);
    EXPECT_FALSE(stmt->hasOutput());
}

TEST_F(StatementKindTest, HasOutputForInsertReturning) {
    auto stmt = connection_->prepareStatement(
        "INSERT INTO k_dml (id, v) VALUES (?, ?) RETURNING id");
    // RETURNING flips hasOutput to true even though kind() is still Dml or
    // exec_procedure (FB returns exec_procedure for INSERT...RETURNING).
    EXPECT_TRUE(stmt->hasOutput());
    // Either Dml or ExecuteProcedure is acceptable — assert the union.
    auto k = stmt->kind();
    EXPECT_TRUE(k == Statement::StatementKind::Dml ||
                k == Statement::StatementKind::ExecuteProcedure)
        << "RETURNING produced kind = " << static_cast<int>(k);
}

TEST_F(StatementKindTest, KindMatchesGetTypeForSelect) {
    auto stmt = connection_->prepareStatement("SELECT 1 FROM RDB$DATABASE");
    // getType() returns raw FB code (1 = isc_info_sql_stmt_select), kind()
    // collapses it into the semantic enum.
    EXPECT_EQ(stmt->getType(), 1u);
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::Select);
}
