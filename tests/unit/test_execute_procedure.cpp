#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/query_executor.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/exception.hpp"

#include <cstdint>
#include <string>
#include <tuple>

// End-to-end coverage of EXECUTE PROCEDURE against a REAL stored procedure.
//
// In Firebird an executable procedure invoked via `EXECUTE PROCEDURE p(...)`
// is the same statement kind as `INSERT ... RETURNING` (isc_info_sql_stmt_
// exec_procedure): a singleton execute that reads exactly one output message,
// not a cursor. It therefore flows through the 3-arg Statement::execute and
// the executeReturning() typed entry point. These tests verify both the raw
// tuple primitive and the struct/descriptor path on an actual procedure.

using namespace fbpp::core;
using namespace fbpp::test;

namespace local_proc {

// Mirrors the generated descriptor shape (sql + Input + Output) so we can
// drive executeReturning() without the build-time generator.
struct AddInput {
    int32_t a;
    int32_t b;
};

struct AddOutput {
    int32_t outSum;
};

struct AddProcDescriptor {
    static constexpr std::string_view sql =
        "EXECUTE PROCEDURE exec_add(:a, :b)";
    using Input = AddInput;
    using Output = AddOutput;
};

} // namespace local_proc

namespace fbpp::core {

template<>
struct StructDescriptor<local_proc::AddInput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&local_proc::AddInput::a>("A", SQL_LONG, 0, sizeof(int32_t), 0, false),
        makeField<&local_proc::AddInput::b>("B", SQL_LONG, 0, sizeof(int32_t), 0, false)
    );
};

template<>
struct StructDescriptor<local_proc::AddOutput> {
    static constexpr auto fields = std::make_tuple(
        makeField<&local_proc::AddOutput::outSum>("OUT_SUM", SQL_LONG, 0, sizeof(int32_t), 0, false)
    );
};

} // namespace fbpp::core

class ExecuteProcedureTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        // Executable (non-SUSPEND) procedure: one input pair -> one output row.
        connection_->ExecuteDDL(R"(
            CREATE PROCEDURE exec_add (a INTEGER, b INTEGER)
            RETURNS (out_sum INTEGER)
            AS BEGIN
                out_sum = a + b;
            END
        )");
    }
};

// Firebird reports EXECUTE PROCEDURE (with RETURNS, no SUSPEND) as the
// exec_procedure kind, and it produces output — the signal that it must run
// via execute() (single row), not openCursor().
TEST_F(ExecuteProcedureTest, KindIsExecuteProcedureWithOutput) {
    auto stmt = connection_->prepareStatement("EXECUTE PROCEDURE exec_add(?, ?)");
    EXPECT_EQ(stmt->kind(), Statement::StatementKind::ExecuteProcedure);
    EXPECT_TRUE(stmt->hasOutput());
}

// 3-arg execute primitive: tuple in -> tuple out against the real procedure.
TEST_F(ExecuteProcedureTest, TupleInTupleOut) {
    auto stmt = connection_->prepareStatement("EXECUTE PROCEDURE exec_add(?, ?)");
    auto tra = connection_->StartTransaction();

    auto [affected, out] = tra->execute(
        stmt, std::make_tuple(2, 3), std::tuple<int32_t>{});
    (void)affected;  // affected-row count is not meaningful for a procedure
    EXPECT_EQ(std::get<0>(out), 5);

    tra->Commit();
}

// Typed path: struct in -> struct out via executeReturning() with a
// hand-written descriptor — the formerly-throwing struct-input branch.
TEST_F(ExecuteProcedureTest, StructInStructOutViaExecuteReturning) {
    auto tra = connection_->StartTransaction();

    local_proc::AddInput in{40, 2};
    auto [affected, out] = executeReturning<local_proc::AddProcDescriptor>(
        *connection_, *tra, in);
    (void)affected;
    EXPECT_EQ(out.outSum, 42);

    // executeReturningOne convenience returns just the output row.
    auto only = executeReturningOne<local_proc::AddProcDescriptor>(
        *connection_, *tra, local_proc::AddInput{10, 7});
    EXPECT_EQ(only.outSum, 17);

    tra->Commit();
}
