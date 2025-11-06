#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/firebird_compat.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

class QueryMetadataTest : public PersistentDatabaseTest {};

TEST_F(QueryMetadataTest, DescribeSelectQuery) {
    const std::string sql =
        "SELECT id, name, amount, created_at FROM test_data WHERE id = :id";

    auto meta = connection_->describeQuery(sql);

    ASSERT_EQ(meta.inputFields.size(), 1u);
    EXPECT_EQ(meta.inputFields[0].type, SQL_LONG);

    ASSERT_EQ(meta.outputFields.size(), 4u);
    EXPECT_EQ(meta.outputFields[0].type, SQL_LONG);
    EXPECT_EQ(meta.outputFields[1].type, SQL_VARYING);
    EXPECT_EQ(meta.outputFields[2].type, SQL_DOUBLE);
    EXPECT_EQ(meta.outputFields[3].type, SQL_TIMESTAMP);
}

TEST_F(QueryMetadataTest, DescribeInsertReturning) {
    const std::string sql =
        "INSERT INTO test_data (id, name, amount) "
        "VALUES (:id, :name, :amount) RETURNING id";

    auto meta = connection_->describeQuery(sql);

    ASSERT_EQ(meta.inputFields.size(), 3u);
    EXPECT_EQ(meta.inputFields[0].type, SQL_LONG);
    EXPECT_EQ(meta.inputFields[1].type, SQL_VARYING);
    EXPECT_EQ(meta.inputFields[2].type, SQL_DOUBLE);

    ASSERT_EQ(meta.outputFields.size(), 1u);
    EXPECT_EQ(meta.outputFields[0].type, SQL_LONG);
}
