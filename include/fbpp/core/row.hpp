#pragma once

// Runtime, name-based row access without compile-time descriptors.
//
// Two types live here:
//
//   RowView  — non-owning, transient. Points at the cursor's current
//              fetch buffer; valid until the next fetch / close. Cheap to
//              construct, designed for hot loops via ResultSet::rows().
//
//   Row      — owning copy. Independent of the cursor — holds its own
//              bytes and a shared_ptr to metadata. Safe to keep after
//              the cursor is closed. BLOB reads still need a live
//              transaction (Row holds shared_ptr<Transaction> for that).
//
// Both use the same per-field codec (sql_value_codec::read_sql_value)
// as TupleUnpacker / StructDescriptor, so any type those paths support
// works here too. get<T>(name | index) returns std::optional<T>:
//   nullopt    iff column is SQL NULL,
//   throws     iff column name unknown or T does not match column type.

#include "fbpp/core/exception.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/detail/sql_value_codec.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fbpp {
namespace core {

class ResultSet;

namespace detail {

// Validate that requested C++ type T is compatible with the column's
// Firebird type. Only catches the obvious arithmetic mismatches —
// callers asking for a specific extended type (Int128, DecFloat, ...) get
// caught by the codec itself. std::string is the universal fallback and
// always passes; any column can be read as a string.
//
// Takes FieldInfo (not just sql_type) because scaled NUMERIC/DECIMAL is
// stored as SQL_SHORT/LONG/INT64 with scale<0 — get<double>() on such a
// column is valid (codec applies the scale factor) and must be allowed.
template<typename T>
inline bool sqlTypeMatches(const FieldInfo& fi) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    const unsigned t = fi.type & ~1u;   // strip nullable bit
    if constexpr (std::is_same_v<U, bool>) {
        return t == SQL_BOOLEAN;
    } else if constexpr (std::is_integral_v<U> && sizeof(U) <= 2) {
        return t == SQL_SHORT;
    } else if constexpr (std::is_integral_v<U> && sizeof(U) == 4) {
        return t == SQL_LONG;
    } else if constexpr (std::is_integral_v<U> && sizeof(U) == 8) {
        return t == SQL_INT64;
    } else if constexpr (std::is_same_v<U, float>) {
        return t == SQL_FLOAT;
    } else if constexpr (std::is_same_v<U, double>) {
        if (t == SQL_DOUBLE || t == SQL_D_FLOAT || t == SQL_FLOAT) return true;
        // Scaled NUMERIC/DECIMAL — codec divides by 10^(-scale).
        if ((t == SQL_SHORT || t == SQL_LONG || t == SQL_INT64) && fi.scale < 0) {
            return true;
        }
        return false;
    } else {
        // std::string and all extended types — rely on codec dispatch.
        return true;
    }
}

} // namespace detail

class RowView {
public:
    /// Construct a RowView over a buffer. owner/generation are optional
    /// and used only by the staleness guard; pass nullptr/0 when the
    /// view is built outside of a ResultSet (e.g., unit tests).
    RowView(std::shared_ptr<const MessageMetadata> meta,
            const uint8_t* buf,
            Transaction* tx,
            const ResultSet* owner = nullptr,
            std::uint64_t generation = 0) noexcept
        : meta_(std::move(meta))
        , buf_(buf)
        , tx_(tx)
        , owner_(owner)
        , snapshotGen_(generation) {}

    bool isNull(unsigned index) const {
        checkValid();
        const FieldInfo& fi = meta_->getField(index);
        const int16_t* nullPtr = reinterpret_cast<const int16_t*>(
            buf_ + fi.nullOffset);
        return nullPtr && *nullPtr == -1;
    }

    bool isNull(std::string_view name) const {
        return isNull(resolveIndex(name));
    }

    template<typename T>
    std::optional<T> get(unsigned index) const {
        checkValid();
        const FieldInfo& fi = meta_->getField(index);
        const int16_t* nullPtr = reinterpret_cast<const int16_t*>(
            buf_ + fi.nullOffset);
        if (nullPtr && *nullPtr == -1) {
            return std::nullopt;
        }
        if (!detail::sqlTypeMatches<T>(fi)) {
            throw FirebirdException(
                std::string("Type mismatch for column '") +
                std::string(displayName(fi)) + "' (sql_type=" +
                std::to_string(fi.type) + ")");
        }
        const uint8_t* dataPtr = buf_ + fi.offset;
        detail::sql_value_codec::SqlReadContext ctx{&fi, tx_, nullPtr};
        T value{};
        detail::sql_value_codec::read_sql_value(ctx, dataPtr, value);
        return value;
    }

    template<typename T>
    std::optional<T> get(std::string_view name) const {
        return get<T>(resolveIndex(name));
    }

    unsigned columnCount() const {
        checkValid();
        return meta_->getCount();
    }

    std::string columnName(unsigned index) const {
        checkValid();
        return meta_->getDisplayName(index);
    }

    FieldInfo columnInfo(unsigned index) const {
        checkValid();
        return meta_->getField(index);
    }

    const MessageMetadata& metadata() const { return *meta_; }
    std::shared_ptr<const MessageMetadata> sharedMetadata() const { return meta_; }
    const uint8_t* data() const { return buf_; }
    Transaction* transaction() const { return tx_; }

    /// True iff the underlying buffer is still alive (i.e. the owning
    /// ResultSet has not advanced past this view). Always true when the
    /// view was built without an owner.
    bool isValid() const noexcept;

private:
    unsigned resolveIndex(std::string_view name) const {
        checkValid();
        auto idx = meta_->getIndex(std::string(name));
        if (!idx) {
            throw FirebirdException(
                std::string("Column not found: '") + std::string(name) + "'");
        }
        return *idx;
    }

    void checkValid() const;

    std::shared_ptr<const MessageMetadata> meta_;
    const uint8_t* buf_ = nullptr;
    Transaction* tx_ = nullptr;
    const ResultSet* owner_ = nullptr;
    std::uint64_t snapshotGen_ = 0;
};

class Row {
public:
    /// Snapshot a RowView into an owning Row. Copies the buffer,
    /// shares the metadata, and captures a shared_ptr to the
    /// transaction (via Transaction::shared_from_this) so BLOB reads
    /// stay valid after the cursor closes.
    explicit Row(const RowView& view);

    /// Direct construction for callers that build buffers outside of
    /// ResultSet (tests, generated code).
    Row(std::shared_ptr<const MessageMetadata> meta,
        std::vector<uint8_t> buffer,
        std::shared_ptr<Transaction> tx);

    Row(Row&&) noexcept = default;
    Row& operator=(Row&&) noexcept = default;
    Row(const Row&) = delete;
    Row& operator=(const Row&) = delete;

    bool isNull(unsigned index) const {
        const FieldInfo& fi = meta_->getField(index);
        const int16_t* nullPtr = reinterpret_cast<const int16_t*>(
            buf_.data() + fi.nullOffset);
        return nullPtr && *nullPtr == -1;
    }

    bool isNull(std::string_view name) const {
        return isNull(resolveIndex(name));
    }

    template<typename T>
    std::optional<T> get(unsigned index) const {
        const FieldInfo& fi = meta_->getField(index);
        const int16_t* nullPtr = reinterpret_cast<const int16_t*>(
            buf_.data() + fi.nullOffset);
        if (nullPtr && *nullPtr == -1) {
            return std::nullopt;
        }
        if (!detail::sqlTypeMatches<T>(fi)) {
            throw FirebirdException(
                std::string("Type mismatch for column '") +
                std::string(displayName(fi)) + "' (sql_type=" +
                std::to_string(fi.type) + ")");
        }
        const uint8_t* dataPtr = buf_.data() + fi.offset;
        detail::sql_value_codec::SqlReadContext ctx{&fi, tx_.get(), nullPtr};
        T value{};
        detail::sql_value_codec::read_sql_value(ctx, dataPtr, value);
        return value;
    }

    template<typename T>
    std::optional<T> get(std::string_view name) const {
        return get<T>(resolveIndex(name));
    }

    unsigned columnCount() const { return meta_->getCount(); }
    std::string columnName(unsigned index) const { return meta_->getDisplayName(index); }
    FieldInfo columnInfo(unsigned index) const { return meta_->getField(index); }
    const MessageMetadata& metadata() const { return *meta_; }
    const uint8_t* data() const { return buf_.data(); }
    Transaction* transaction() const { return tx_.get(); }

private:
    unsigned resolveIndex(std::string_view name) const {
        auto idx = meta_->getIndex(std::string(name));
        if (!idx) {
            throw FirebirdException(
                std::string("Column not found: '") + std::string(name) + "'");
        }
        return *idx;
    }

    std::shared_ptr<const MessageMetadata> meta_;
    std::vector<uint8_t> buf_;
    std::shared_ptr<Transaction> tx_;
};

} // namespace core
} // namespace fbpp
