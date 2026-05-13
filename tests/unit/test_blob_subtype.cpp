#include <gtest/gtest.h>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/exception.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

// Transaction::createBlob(data, subType). The default (single-arg) form
// produces SUB_TYPE 0 (binary); passing subType = 1 explicitly tags the
// BLOB as TEXT so Firebird won't transliterate or reject bytes on read.

using namespace fbpp::core;
using namespace fbpp::test;

class BlobSubtypeTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        TempDatabaseTest::createTestSchema();
        connection_->ExecuteDDL(R"(
            CREATE TABLE blob_t (
                id INTEGER NOT NULL PRIMARY KEY,
                bin BLOB SUB_TYPE BINARY,
                txt BLOB SUB_TYPE TEXT
            )
        )");
    }

    static std::vector<uint8_t> bytesOf(const std::string& s) {
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    // Insert a BLOB created via createBlob(...) into the named column.
    void insertBlob(int id,
                    const char* column,
                    ISC_QUAD blobId,
                    Transaction* tx) {
        const std::string sql =
            std::string("INSERT INTO blob_t (id, ") + column +
            ") VALUES (?, ?)";
        auto stmt = connection_->prepareStatement(sql);

        // Build a tuple with the Blob wrapper and rely on codec's
        // ISC_QUAD handling for the BLOB column.
        Blob blobWrapper(reinterpret_cast<const uint8_t*>(&blobId));
        ASSERT_EQ(tx->execute(stmt, std::make_tuple(int32_t{id}, blobWrapper)), 1u);
    }

    // Read raw bytes from a blob column for the row with given id.
    std::vector<uint8_t> readBlobBytes(int id, const char* column,
                                       Transaction* tx) {
        const std::string sql =
            std::string("SELECT ") + column + " FROM blob_t WHERE id = ?";
        auto stmt = connection_->prepareStatement(sql);
        auto cur = tx->openCursor(stmt, std::make_tuple(int32_t{id}));
        std::tuple<std::string> row;
        if (!cur->fetch(row)) return {};
        const auto& s = std::get<0>(row);
        return std::vector<uint8_t>(s.begin(), s.end());
    }
};

TEST_F(BlobSubtypeTest, CreateBlobBinaryDefault) {
    auto tx = connection_->StartTransaction();
    auto data = bytesOf("hello-binary");
    auto blobId = tx->createBlob(data);
    insertBlob(1, "bin", blobId, tx.get());
    EXPECT_EQ(readBlobBytes(1, "bin", tx.get()), data);
}

TEST_F(BlobSubtypeTest, CreateBlobBinarySubtype0Explicit) {
    auto tx = connection_->StartTransaction();
    auto data = bytesOf("hello-binary-explicit");
    auto blobId = tx->createBlob(data, 0);
    insertBlob(2, "bin", blobId, tx.get());
    EXPECT_EQ(readBlobBytes(2, "bin", tx.get()), data);
}

TEST_F(BlobSubtypeTest, CreateBlobTextSubtype1) {
    auto tx = connection_->StartTransaction();
    auto data = bytesOf("hello-text-content");
    auto blobId = tx->createBlob(data, 1);
    insertBlob(3, "txt", blobId, tx.get());
    EXPECT_EQ(readBlobBytes(3, "txt", tx.get()), data);
}

TEST_F(BlobSubtypeTest, CreateBlobTextRoundTripUtf8) {
    auto tx = connection_->StartTransaction();
    // "Привет, мир! 🦊" as raw UTF-8 bytes (avoid u8"" which is char8_t[]
    // in C++20 and not implicitly convertible to std::string).
    const char utf8[] =
        "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82,"
        " \xD0\xBC\xD0\xB8\xD1\x80! \xF0\x9F\xA6\x8A";
    auto data = bytesOf(std::string(utf8));
    auto blobId = tx->createBlob(data, 1);
    insertBlob(4, "txt", blobId, tx.get());
    EXPECT_EQ(readBlobBytes(4, "txt", tx.get()), data);
}

TEST_F(BlobSubtypeTest, LoadBlobReturnsBytesForSubtype1) {
    auto tx = connection_->StartTransaction();
    auto data = bytesOf("payload-via-loadblob");
    auto blobId = tx->createBlob(data, 1);

    // loadBlob is byte-oriented and ignores subType — verify that bytes
    // round-trip through it as well (regression for any future change to
    // pass BPB on read path).
    ISC_QUAD copy = blobId;
    auto bytes = tx->loadBlob(&copy);
    EXPECT_EQ(bytes, data);
}

TEST_F(BlobSubtypeTest, CreateBlobInactiveTxThrows) {
    auto tx = connection_->StartTransaction();
    tx->Commit();
    EXPECT_THROW(tx->createBlob(bytesOf("dead"), 0), FirebirdException);
    EXPECT_THROW(tx->createBlob(bytesOf("dead"), 1), FirebirdException);
}
