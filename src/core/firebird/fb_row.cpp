#include "fbpp/core/row.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/exception.hpp"

namespace fbpp {
namespace core {

bool RowView::isValid() const noexcept {
    if (!owner_) {
        // No owner means the view was constructed standalone (e.g.
        // unit test with an explicit buffer). It's the caller's job to
        // ensure the buffer outlives the view.
        return buf_ != nullptr && static_cast<bool>(meta_);
    }
    return owner_->generation() == snapshotGen_ && owner_->isValid();
}

void RowView::checkValid() const {
#ifndef NDEBUG
    // _DEBUG-only guard. In release builds the user is responsible for
    // not using a stale view (the docs are explicit about this).
    if (owner_ && owner_->generation() != snapshotGen_) {
        throw FirebirdException(
            "RowView used after fetch/close — copy to Row first");
    }
#endif
}

Row::Row(const RowView& view)
    : meta_(view.sharedMetadata())
    , buf_(view.data(),
           view.data() + view.metadata().getMessageLength()) {
    if (auto* tx = view.transaction()) {
        try {
            tx_ = tx->shared_from_this();
        } catch (const std::bad_weak_ptr&) {
            // Transaction not managed by shared_ptr (test-only path).
            // BLOB reads on the resulting Row would fail, but scalar
            // reads still work because they don't need a live tx.
            tx_ = nullptr;
        }
    }
}

Row::Row(std::shared_ptr<const MessageMetadata> meta,
         std::vector<uint8_t> buffer,
         std::shared_ptr<Transaction> tx)
    : meta_(std::move(meta))
    , buf_(std::move(buffer))
    , tx_(std::move(tx)) {
    if (!meta_) {
        throw FirebirdException("Row: null metadata");
    }
}

} // namespace core
} // namespace fbpp
