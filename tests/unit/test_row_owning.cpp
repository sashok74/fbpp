#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/row.hpp"

#include <optional>
#include <string>

// Row — owning copy that survives cursor close.

using namespace fbpp::core;
using namespace fbpp::test;

class RowOwningTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE TABLE ro (
                id INTEGER NOT NULL PRIMARY KEY,
                name VARCHAR(64),
                payload BLOB SUB_TYPE TEXT
            )
        )");
        auto tx = connection_->StartTransaction();
        auto ins = connection_->prepareStatement(
            "INSERT INTO ro (id, name, payload) VALUES (?, ?, ?)");
        // Bind BLOB by creating it explicitly — codec writes ISC_QUAD.
        std::vector<uint8_t> bytes{'h', 'e', 'l', 'l', 'o'};
        auto blobId = tx->createBlob(bytes, 1);
        Blob bw(reinterpret_cast<const uint8_t*>(&blobId));
        tx->execute(ins, std::make_tuple(int32_t{1}, std::string("Alice"), bw));
        tx->Commit();
    }
};

TEST_F(RowOwningTest, RowFromViewCopiesData) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id, name FROM ro WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    auto rowOpt = cur->fetchOne();
    ASSERT_TRUE(rowOpt.has_value());
    const auto& row = *rowOpt;

    EXPECT_EQ(row.get<int32_t>(0).value_or(-1), 1);
    EXPECT_EQ(row.get<std::string>("NAME").value_or(""), "Alice");
}

TEST_F(RowOwningTest, RowSurvivesCursorClose) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id, name FROM ro WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    auto rowOpt = cur->fetchOne();
    ASSERT_TRUE(rowOpt.has_value());
    cur->close();
    cur.reset();

    // Scalar reads remain valid after the cursor is gone.
    EXPECT_EQ(rowOpt->get<int32_t>("id").value_or(-1), 1);
    EXPECT_EQ(rowOpt->get<std::string>("name").value_or(""), "Alice");
}

TEST_F(RowOwningTest, RowMetadataSurvivesResultSetDestruction) {
    auto tx = connection_->StartTransaction();
    std::optional<Row> savedRow;
    {
        auto stmt = connection_->prepareStatement(
            "SELECT id, name FROM ro WHERE id = 1");
        auto cur = tx->openCursor(stmt);
        savedRow = cur->fetchOne();
    }   // cursor destroyed here
    ASSERT_TRUE(savedRow.has_value());

    // FieldInfo lookup still works because Row holds shared_ptr to the
    // metadata that the destroyed ResultSet was sharing.
    EXPECT_EQ(savedRow->columnCount(), 2u);
    EXPECT_EQ(savedRow->columnName(0), "ID");
    EXPECT_EQ(savedRow->columnName(1), "NAME");
}

TEST_F(RowOwningTest, RowBlobReadAfterCursorClose) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT payload FROM ro WHERE id = 1");
    auto cur = tx->openCursor(stmt);
    auto rowOpt = cur->fetchOne();
    ASSERT_TRUE(rowOpt.has_value());
    cur->close();
    cur.reset();

    // BLOB read needs a live transaction — Row captured shared_ptr<Transaction>
    // via shared_from_this so this still works after cursor close.
    auto payload = rowOpt->get<std::string>("payload");
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(*payload, "hello");
}

TEST_F(RowOwningTest, FetchOneEmpty) {
    auto tx = connection_->StartTransaction();
    auto stmt = connection_->prepareStatement(
        "SELECT id FROM ro WHERE id = 999");
    auto cur = tx->openCursor(stmt);
    EXPECT_FALSE(cur->fetchOne().has_value());
}
