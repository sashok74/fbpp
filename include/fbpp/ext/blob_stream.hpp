#pragma once

// PR-06: VCL TStream-friendly BLOB encode/decode helpers.
//
// Wraps Transaction::loadBlob and Transaction::createBlob(data, subType)
// to/from System::Classes::TStream — the legacy contract of HiTek's
// IDbAdapter (DsqlGetBlob / DsqlSetBlobParam) and any code that already
// has a TMemoryStream / TFileStream payload.
//
// Reading goes through Transaction::loadBlob which buffers the whole
// BLOB into memory before writing to the stream. For very large BLOBs
// a true segment-streaming variant (IBlob exposed) is a future PR.

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/ext/rad_variant_decoder.hpp"   // ColumnDecodePlan, RadColumnKind

#ifdef FBPP_WITH_RAD_DATASET

#include <System.Classes.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace fbpp::ext {

/// Decode the BLOB at plan/rowBase into outStream. Returns false if the
/// column value is SQL NULL (stream is left untouched). Sets stream
/// Position to 0 on success.
///
/// Throws if plan does not describe a BLOB column, or if txn / outStream
/// is null.
inline bool
decodeBlobToStream(const ColumnDecodePlan& plan,
                   const std::uint8_t* rowBase,
                   fbpp::core::Transaction* txn,
                   System::Classes::TStream* outStream) {
    if (plan.kind != RadColumnKind::Blob) {
        throw fbpp::core::FirebirdException(
            "decodeBlobToStream: column is not BLOB");
    }
    if (!txn) {
        throw fbpp::core::FirebirdException("decodeBlobToStream: null transaction");
    }
    if (!outStream) {
        throw fbpp::core::FirebirdException("decodeBlobToStream: null stream");
    }
    if (!rowBase) {
        throw fbpp::core::FirebirdException("decodeBlobToStream: null rowBase");
    }

    // Null indicator check — same convention as decodeColumnToVariant.
    int16_t nullFlag = 0;
    std::memcpy(&nullFlag, rowBase + plan.nullOffset, sizeof(int16_t));
    if (nullFlag == -1) {
        return false;
    }

    fbpp::core::Blob blobId(rowBase + plan.offset);
    if (blobId.isNull()) {
        return false;
    }

    ISC_QUAD blobRef{};
    std::memcpy(&blobRef, blobId.getId(), sizeof(blobRef));
    auto bytes = txn->loadBlob(&blobRef);

    // Reset target stream BEFORE writing — legacy DsqlGetBlob caller
    // typically passes a TMemoryStream that may already hold the
    // previous row's payload; appending or leaving a tail corrupts data.
    outStream->Position = 0;
    outStream->Size     = 0;
    if (!bytes.empty()) {
        outStream->WriteBuffer(bytes.data(), static_cast<int>(bytes.size()));
    }
    outStream->Position = 0;
    return true;
}

/// Read the entire input stream from Position 0 and store as a new BLOB
/// of the requested subType (0 = binary, 1 = text). The returned Blob
/// holds the new BLOB's ISC_QUAD — bind it to the target column via
/// ParamBinder/codec to insert.
///
/// Throws on null txn / in / outBlob.
inline void
encodeStreamToBlob(fbpp::core::Transaction* txn,
                   System::Classes::TStream* in,
                   int subType,
                   fbpp::core::Blob* outBlob) {
    if (!txn) {
        throw fbpp::core::FirebirdException("encodeStreamToBlob: null transaction");
    }
    if (!in) {
        throw fbpp::core::FirebirdException("encodeStreamToBlob: null input stream");
    }
    if (!outBlob) {
        throw fbpp::core::FirebirdException("encodeStreamToBlob: null output Blob");
    }

    in->Position = 0;
    const auto sz = in->Size;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
    if (sz > 0) {
        in->ReadBuffer(bytes.data(), static_cast<int>(sz));
    }

    ISC_QUAD blobId = txn->createBlob(bytes, subType);
    *outBlob = fbpp::core::Blob(reinterpret_cast<const std::uint8_t*>(&blobId));
}

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET
