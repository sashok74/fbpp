#pragma once

// PR-07: fast EhLib loadIntoMemTable.
//
// Iterates ResultSet::rows() (RowView, no per-row copy) and feeds rows
// straight into TMemTableEh's internal RecordsList — bypasses the
// generic VCL Append/Post path. This is the canonical replacement for
// HiTek's three nearly-identical FbppAdapter::MemLoad / MemLoadGenericFast
// / MemLoadBigBenchTyped loops; per the ТЗ they should all collapse to
// a single call into this helper.
//
// Caller is responsible for calling CreateDataSet() / FieldDefs setup
// before invoking — typically via ext::DatasetLoader or
// ext::configureFieldDefs(fieldDefs, plans). loadIntoMemTable does not
// touch FieldDefs.
//
// rowLimit == 0 means no limit. opts.loadBlobs defaults to false so a
// mass load does not pull BLOB content for every row.

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/row.hpp"
#include "fbpp/ext/rad_variant_decoder.hpp"

#if defined(FBPP_WITH_RAD_DATASET) && defined(FBPP_WITH_EHLIB)

#include <Data.DB.hpp>
#include <MemTableEh.hpp>
#include <MemTableDataEh.hpp>
#include <System.Variants.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace fbpp::ext {

namespace detail {

// RAII guard around RecordsList::BeginUpdate / EndUpdate.
class MemTableUpdateGuard {
public:
    explicit MemTableUpdateGuard(Memtabledataeh::TRecordsListEh* rl) : rl_(rl) {
        if (rl_) rl_->BeginUpdate();
    }
    ~MemTableUpdateGuard() {
        if (rl_) { try { rl_->EndUpdate(); } catch (...) {} }
    }
    MemTableUpdateGuard(const MemTableUpdateGuard&) = delete;
    MemTableUpdateGuard& operator=(const MemTableUpdateGuard&) = delete;
private:
    Memtabledataeh::TRecordsListEh* rl_;
};

} // namespace detail

/// Stream rows from cursor into mt via the RecordsList fast path —
/// bypasses TDataSet::Append/Post and the per-field FieldByName
/// indirection. Returns the number of rows inserted.
inline int
loadIntoMemTable(Memtableeh::TMemTableEh* mt,
                 fbpp::core::ResultSet& cursor,
                 const std::vector<ColumnDecodePlan>& plans,
                 const VariantDecodeOptions& opts = {},
                 int rowLimit = 0) {
    if (!mt) {
        throw fbpp::core::FirebirdException("loadIntoMemTable: null TMemTableEh");
    }
    if (!cursor.isValid()) {
        throw fbpp::core::FirebirdException("loadIntoMemTable: cursor closed");
    }

    auto* meta = cursor.getMetadata();
    if (!meta || meta->getCount() != plans.size()) {
        throw fbpp::core::FirebirdException(
            "loadIntoMemTable: plans / metadata column count mismatch");
    }

    auto* recList = mt->RecordsView->MemTableData->RecordsList;
    detail::MemTableUpdateGuard guard(recList);

    int n = 0;
    const int nFields = static_cast<int>(plans.size());

    for (const auto& view : cursor.rows()) {
        if (rowLimit > 0 && n >= rowLimit) break;

        Memtabledataeh::TMemoryRecordEh* rec = recList->NewRecord();
        for (int i = 0; i < nFields; ++i) {
            // Cheap null-check straight off the row buffer — avoids
            // decoding into a Variant just to find out it's empty.
            int16_t nullFlag = 0;
            std::memcpy(&nullFlag, view.data() + plans[i].nullOffset,
                        sizeof(int16_t));
            if (nullFlag == -1) continue;   // freshly-created cell is NULL
            System::Variant val = decodeColumnToVariant(
                plans[i], view.data(), view.transaction(), opts);
            rec->Value[i][Memtabledataeh::dvvValueEh] = val;
        }
        recList->FetchRecord(rec);
        ++n;
    }
    return n;
}

// NOTE on attempted typed fast path (2026-05-15, reverted):
//
// We attempted a `loadIntoMemTableTyped` variant that bypassed the
// `rec->Value[i][dvvValueEh] = v` property setter by writing directly to
// the protected `TMemoryRecordEh::Data[i]` (via crack class) and dispatched
// by RadColumnKind for primitive types. The hypothesis was 10-15% speedup
// on numeric payloads.
//
// Result: NO measurable gain. Storage is `TRecDataValues = array of Variant`
// (MemTableDataEh.pas:648,672,705) by design — Variant slot assignment IS
// the canonical write. `SetDataValue → DataVarCast(VarCast)` for same-type
// Variants reduces to ~memcpy(16-byte TVarData), comparable to direct
// `data[i] = Variant`. The architectural ceiling of EhLib's Variant-cell
// storage applies equally to both paths.
//
// FireDAC's TFDDatSRow uses typed packed byte buffers with field offsets
// (FireDAC.DatS.pas:3974, 10807) — that's why it's faster on bulk load,
// but it's a different storage backend, not reachable from TMemTableEh.
//
// Code removed; this comment preserved as institutional memory.
// See doc/benchmark_results.md (2026-05-15 section) for full analysis.
//
// -------------------------------------------------------------------------
// Future optimization directions (do NOT chase the setter again):
//
//   1. EndUpdate / view rebuild cost. RecordsList.EndUpdate triggers
//      TRecordsViewEh.RefreshFilteredRecsList (MemTableDataEh.pas) which
//      runs O(N) sort+filter+TMemRecViewEh alloc even when Filtered=false
//      and IndexFieldNames=''. For very large rowsets this can dominate
//      the per-row work — measure it separately before optimizing.
//
//   2. Disable UI/index/sort/filter/aggregates during load. Caller hints:
//      - `mt->DisableControls()` / `EnableControls()` around the load
//      - clear `IndexFieldNames`/`SortMarkers`/aggregates temporarily
//      - detach grid DataSource if attached
//      These suppress notifications that fire after EndUpdate.
//
//   3. Reuse existing records on refresh. For repeated loads with the
//      same schema, walk RecordsList[i] and overwrite Data[i] cells in
//      place; only Add/Delete the count delta. Saves TMemoryRecordEh
//      object allocation, SetLength(FData), and InitRecord (VarSetNull).
//      Not provided by EhLib's DataDriverEh — would need a custom helper.
//
//   4. Compile-time-typed fetch for known schemas. Use
//      fbpp::core::ResultSet::fetch<Tuple> + typed TField setters per
//      RadColumnKind — applies only where Output schema is known at
//      compile time. This is the path the historical MemLoadBigBenchTyped
//      took for its measured ~10% gain.
//
//   5. Replace TMemTableEh with TFDMemTable in hot-path transit buffers
//      (FireDAC's typed packed storage). Architectural change, ~2× perf
//      on bulk load — not interchangeable with TMemTableEh in UI binding.

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET && FBPP_WITH_EHLIB
