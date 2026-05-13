#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/message_metadata.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

class MessageMetadataTest : public TempDatabaseTest {};

// ----------------------------------------------------------------------------
// Identity helper (free function and getDisplayName(index))
// ----------------------------------------------------------------------------

TEST(MessageMetadataIdentityTest, DisplayNamePrefersAlias) {
    FieldInfo f;
    f.name = "CONSTANT";
    f.alias = "FLAG";
    EXPECT_EQ(displayName(f), "FLAG");
}

TEST(MessageMetadataIdentityTest, DisplayNameFallsBackToName) {
    FieldInfo f;
    f.name = "ID";
    f.alias = "";
    EXPECT_EQ(displayName(f), "ID");
}

TEST(MessageMetadataIdentityTest, DisplayNameEmptyWhenBothEmpty) {
    FieldInfo f;
    f.name = "";
    f.alias = "";
    EXPECT_EQ(displayName(f), "");
}

// ----------------------------------------------------------------------------
// Lookup case-fold + trim
// ----------------------------------------------------------------------------

TEST_F(MessageMetadataTest, GetFieldExactMatchUppercase) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    auto field = meta->getField("ID");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->name, "ID");
}

TEST_F(MessageMetadataTest, GetFieldCaseInsensitiveLowercase) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    auto field = meta->getField("id");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->name, "ID");
}

TEST_F(MessageMetadataTest, GetFieldCaseInsensitiveMixedCase) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    auto field = meta->getField("Id");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->name, "ID");
}

TEST_F(MessageMetadataTest, GetFieldTrimsWhitespace) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    auto field = meta->getField("  id  ");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->name, "ID");
}

TEST_F(MessageMetadataTest, GetFieldNotFound) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    EXPECT_FALSE(meta->getField("does_not_exist").has_value());
    EXPECT_FALSE(meta->getField("").has_value());
    EXPECT_FALSE(meta->getField("   ").has_value());
}

TEST_F(MessageMetadataTest, GetIndexCaseInsensitive) {
    auto stmt = connection_->prepareStatement(
        "SELECT id, name, amount FROM test_table");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);

    auto idx_upper = meta->getIndex("NAME");
    auto idx_lower = meta->getIndex("name");
    auto idx_padded = meta->getIndex("  Name  ");
    ASSERT_TRUE(idx_upper.has_value());
    ASSERT_TRUE(idx_lower.has_value());
    ASSERT_TRUE(idx_padded.has_value());
    EXPECT_EQ(*idx_upper, *idx_lower);
    EXPECT_EQ(*idx_upper, *idx_padded);
    EXPECT_FALSE(meta->getIndex("nope").has_value());
}

// ----------------------------------------------------------------------------
// Regression: literal columns with aliases share name="CONSTANT"
// ----------------------------------------------------------------------------

TEST_F(MessageMetadataTest, LiteralColumnsDisambiguatedByAlias) {
    // The original pain point: two literal columns share name="CONSTANT"
    // but have distinct aliases.
    auto stmt = connection_->prepareStatement(
        "SELECT 0 AS flag, 0 AS new_item_id FROM RDB$DATABASE");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);
    ASSERT_EQ(meta->getCount(), 2u);

    // Both raw names collide on "CONSTANT" — proves provenance is useless here.
    EXPECT_EQ(meta->getField(0).name, meta->getField(1).name);

    // Aliases are distinct and uppercased by Firebird (unquoted identifier).
    EXPECT_EQ(meta->getField(0).alias, "FLAG");
    EXPECT_EQ(meta->getField(1).alias, "NEW_ITEM_ID");

    // Display name uses alias.
    EXPECT_EQ(meta->getDisplayName(0), "FLAG");
    EXPECT_EQ(meta->getDisplayName(1), "NEW_ITEM_ID");

    // Lookup by alias resolves to distinct indices.
    auto i_flag = meta->getIndex("flag");
    auto i_new = meta->getIndex("new_item_id");
    ASSERT_TRUE(i_flag.has_value());
    ASSERT_TRUE(i_new.has_value());
    EXPECT_NE(*i_flag, *i_new);
    EXPECT_EQ(*i_flag, 0u);
    EXPECT_EQ(*i_new, 1u);
}

// ----------------------------------------------------------------------------
// Quoted identifier preservation (exact-pass wins)
// ----------------------------------------------------------------------------

TEST_F(MessageMetadataTest, QuotedIdentifierPreservesCase) {
    connection_->ExecuteDDL(
        "CREATE TABLE qident_test ("
        "    \"MyCol\" INTEGER NOT NULL PRIMARY KEY"
        ")");

    auto stmt = connection_->prepareStatement(
        "SELECT \"MyCol\" FROM qident_test");
    auto meta = stmt->getOutputMetadata();
    ASSERT_TRUE(meta);
    ASSERT_EQ(meta->getCount(), 1u);

    // Server returns case-preserved name for quoted identifier.
    EXPECT_EQ(meta->getField(0).name, "MyCol");

    // Exact-pass: case-sensitive match wins.
    auto exact = meta->getField("MyCol");
    ASSERT_TRUE(exact.has_value());
    EXPECT_EQ(exact->name, "MyCol");

    // Pass 2 fold: lowercase lookup also resolves (acceptable trade-off).
    auto folded = meta->getField("mycol");
    ASSERT_TRUE(folded.has_value());
    EXPECT_EQ(folded->name, "MyCol");
}
