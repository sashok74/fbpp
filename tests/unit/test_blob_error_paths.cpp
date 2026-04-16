// Tests for BLOB create/load paths that previously leaked the IBlob handle when
// close()/release() were missing on exception path. We exercise:
//   1) Happy path stress: many round-trips via BlobGuard ensure no handle leak
//      and (crucially) no double-close/release from the new RAII destructor.
//   2) Error path: openBlob with a bogus ISC_QUAD must throw, and repeated
//      failures must not exhaust handles or corrupt the connection.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/exception.hpp"

using namespace fbpp::core;
using namespace fbpp::test;

namespace {

constexpr int kStressIterations = 50;

class BlobErrorPathsTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        connection_->Execute(
            "CREATE TABLE test_blobs ("
            "    id INTEGER NOT NULL PRIMARY KEY,"
            "    payload BLOB SUB_TYPE BINARY"
            ")"
        )->Commit();
    }
};

} // namespace

TEST_F(BlobErrorPathsTest, CreateAndLoadBlob_HappyPathStress) {
    // Round-trip many blobs. If BlobGuard accidentally double-closed a blob in
    // its destructor, Firebird would raise "invalid BLOB handle" on the second
    // close and we'd see a failure surfacing either here or in the loop.
    const std::vector<uint8_t> payload(4096, 0xAB);

    for (int i = 0; i < kStressIterations; ++i) {
        auto tra = connection_->StartTransaction();

        ISC_QUAD blobId = tra->createBlob(payload);
        auto loaded = tra->loadBlob(&blobId);
        ASSERT_EQ(loaded.size(), payload.size()) << "iteration " << i;
        ASSERT_EQ(loaded, payload)               << "iteration " << i;

        tra->Commit();
    }
}

TEST_F(BlobErrorPathsTest, LoadBogusBlobId_ThrowsRepeatedly) {
    // A BLOB id that doesn't reference any existing BLOB should make
    // attachment->openBlob fail. We verify that repeated failures don't leak
    // the partially-constructed state that the guard is responsible for.
    for (int i = 0; i < kStressIterations; ++i) {
        auto tra = connection_->StartTransaction();

        ISC_QUAD bogus;
        // Non-zero high/low combination so the fast-path NULL check in
        // loadBlob doesn't early-return an empty vector.
        bogus.gds_quad_high = 0x7FFFFFFF;
        bogus.gds_quad_low  = 0xDEADBEEFu;

        EXPECT_THROW(tra->loadBlob(&bogus), FirebirdException)
            << "iteration " << i;

        tra->Rollback();
    }
}

TEST_F(BlobErrorPathsTest, CreateLargeBlob_MultipleSegments_NoLeak) {
    // Force >1 putSegment iteration (internal segment size is 32 KiB). We only
    // verify that createBlob returns a non-zero blob id and does not leak on
    // multi-segment writes — the round-trip readback is covered by the happy
    // path test above (which uses a single-segment payload).
    const std::vector<uint8_t> big(128 * 1024, 0x5A); // 128 KiB -> 4 putSegments

    for (int i = 0; i < kStressIterations; ++i) {
        auto tra = connection_->StartTransaction();
        ISC_QUAD blobId = tra->createBlob(big);
        EXPECT_TRUE(blobId.gds_quad_high != 0 || blobId.gds_quad_low != 0)
            << "iteration " << i;
        tra->Commit();
    }
}
