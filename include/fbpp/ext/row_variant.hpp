#pragma once

// PR-05: name-based VCL Variant decoder over RowView / Row.
//
// Thin wrapper around the existing rad_variant_decoder primitives
// (decodeColumnToVariant + makeColumnPlan). Lives in fbpp::ext because
// it depends on RAD/VCL System::Variant — the core layer must not.
//
// Two access patterns:
//
//   // hot loop — pre-built ColumnDecodePlan, no per-call lookup:
//   auto plans = fbpp::ext::buildColumnPlans(rowView.metadata());
//   for (const auto& v : cursor->rows()) {
//       Variant val = fbpp::ext::toVariant(plans[idx], v, opts);
//   }
//
//   // ad-hoc — name or index lookup per call:
//   Variant val = fbpp::ext::toVariant(rowView, "out_id", opts);

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/row.hpp"
#include "fbpp/ext/rad_variant_decoder.hpp"

#ifdef FBPP_WITH_RAD_DATASET

#include <string>
#include <string_view>

namespace fbpp::ext {

// Hot-loop overload: caller has already built a ColumnDecodePlan via
// buildColumnPlans(meta) and reuses it across rows. No per-call
// metadata lookup or plan rebuild — strictly the cheapest path.
inline System::Variant
toVariant(const ColumnDecodePlan& plan,
          const fbpp::core::RowView& view,
          const VariantDecodeOptions& opts = {}) {
    return decodeColumnToVariant(plan, view.data(), view.transaction(), opts);
}

inline System::Variant
toVariant(const ColumnDecodePlan& plan,
          const fbpp::core::Row& row,
          const VariantDecodeOptions& opts = {}) {
    return decodeColumnToVariant(plan, row.data(), row.transaction(), opts);
}

// Index overloads — make a fresh plan per call. For hot loops prefer
// the plan-based overloads above.
inline System::Variant
toVariant(const fbpp::core::RowView& view, unsigned index,
          const VariantDecodeOptions& opts = {}) {
    auto plan = makeColumnPlan(view.metadata().getField(index));
    plan.offset     = view.metadata().getOffset(index);
    plan.nullOffset = view.metadata().getNullOffset(index);
    return decodeColumnToVariant(plan, view.data(), view.transaction(), opts);
}

inline System::Variant
toVariant(const fbpp::core::Row& row, unsigned index,
          const VariantDecodeOptions& opts = {}) {
    auto plan = makeColumnPlan(row.metadata().getField(index));
    plan.offset     = row.metadata().getOffset(index);
    plan.nullOffset = row.metadata().getNullOffset(index);
    return decodeColumnToVariant(plan, row.data(), row.transaction(), opts);
}

// Name overloads — case-fold + trim via MessageMetadata::getIndex.
inline System::Variant
toVariant(const fbpp::core::RowView& view, std::string_view name,
          const VariantDecodeOptions& opts = {}) {
    auto idx = view.metadata().getIndex(std::string(name));
    if (!idx) {
        throw fbpp::core::FirebirdException(
            std::string("toVariant: column not found: '") +
            std::string(name) + "'");
    }
    return toVariant(view, *idx, opts);
}

inline System::Variant
toVariant(const fbpp::core::Row& row, std::string_view name,
          const VariantDecodeOptions& opts = {}) {
    auto idx = row.metadata().getIndex(std::string(name));
    if (!idx) {
        throw fbpp::core::FirebirdException(
            std::string("toVariant: column not found: '") +
            std::string(name) + "'");
    }
    return toVariant(row, *idx, opts);
}

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET
