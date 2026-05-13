#pragma once

// Typed, name-based parameter binding for prepared statements WITHOUT going
// through nlohmann::json. Binds values of native C++ types directly into the
// Firebird input message buffer, using the same per-field codec that
// TuplePacker/StructPacker use internally (sql_value_codec).
//
// Designed for callers that have values in native C++ types
// (int64_t, std::string, Int128, chrono::time_point, ...) and want
// trim + lowercase name normalization with non-throwing unknown-name
// semantics (legacy IBO behaviour). Lazy construction without Transaction,
// reuse via clear(), move-friendly for storing in std::optional / std::map.

#include "fbpp/core/statement.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/detail/sql_value_codec.hpp"
#include "fbpp/core/tuple_packer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fbpp {
namespace core {

class ParamBinder {
public:
    // Lazy construction: tx is optional. Without tx, BLOB-typed parameters
    // cannot be bound (set() will throw via the codec with a meaningful
    // message). Use setTransaction() to attach a transaction later.
    explicit ParamBinder(std::shared_ptr<Statement> stmt, Transaction* tx = nullptr)
        : stmt_(std::move(stmt)), tx_(tx) {
        if (!stmt_) {
            throw FirebirdException("ParamBinder: null statement");
        }
        meta_ = stmt_->getInputMetadata();
        if (meta_) {
            buffer_.assign(meta_->getMessageLength(), uint8_t{0});
            bound_.assign(meta_->getCount(), false);
            initNullsToNull();
        }
    }

    // Move-only, noexcept — safe to store in std::optional / std::map.
    ParamBinder(ParamBinder&&) noexcept = default;
    ParamBinder& operator=(ParamBinder&&) noexcept = default;
    ParamBinder(const ParamBinder&) = delete;
    ParamBinder& operator=(const ParamBinder&) = delete;

    // Bind by name. T is deduced from value — caller writes b.set("id", x).
    // Returns false if name is not in the statement's named-parameter mapping
    // (legacy IBO behaviour: silently skip unknown). Returns true on success.
    // Name is normalized (trim + lowercase) before lookup.
    //
    // std::optional<T>: engaged → writes value, disengaged → writes NULL
    // (handled via the codec's std::optional overload — no explicit set()
    // overload needed). For literal NULL without spelling the inner type,
    // use set(name, std::nullopt) — see overload below.
    template<typename T>
    bool set(std::string_view name, const T& value) {
        if (!meta_) return false;
        const auto& mapping = stmt_->getNamedParamMapping();
        const std::string key = normalizeName(name);
        auto it = mapping.find(key);
        if (it == mapping.end()) return false;
        for (size_t pos : it->second) {
            writeAt(pos, value);
        }
        return true;
    }

    // Explicit NULL via std::nullopt — `binder.set("X", std::nullopt)` without
    // having to spell `std::optional<T>{}`. Required because the templated
    // set<T> would deduce T=std::nullopt_t and fall through the generic codec
    // memcpy path (not the std::optional<T> overload), silently writing junk.
    bool set(std::string_view name, std::nullopt_t) { return setNull(name); }

    // Explicit NULL by name. Returns false on unknown name (same convention
    // as set()).
    bool setNull(std::string_view name) {
        if (!meta_) return false;
        const auto& mapping = stmt_->getNamedParamMapping();
        const std::string key = normalizeName(name);
        auto it = mapping.find(key);
        if (it == mapping.end()) return false;
        for (size_t pos : it->second) {
            int16_t* nullPtr = nullIndicatorAt(pos);
            detail::sql_value_codec::setNull(nullPtr);
            bound_[pos] = true;
        }
        return true;
    }

    // Late-bind transaction (needed before binding values into BLOB columns).
    void setTransaction(Transaction* tx) noexcept { tx_ = tx; }
    Transaction* transaction() const noexcept { return tx_; }

    // Reset for reuse: zero buffer, reset null-indicators to NULL, clear
    // bound flags. Allocations are kept (the binder structure stays bound to
    // the same statement).
    void clear() {
        if (!meta_) return;
        std::fill(buffer_.begin(), buffer_.end(), uint8_t{0});
        std::fill(bound_.begin(), bound_.end(), false);
        initNullsToNull();
    }

    // Used by Transaction::execute / openCursor overloads.
    const MessageMetadata* metadata() const noexcept { return meta_.get(); }
    const uint8_t* buffer() const noexcept { return buffer_.data(); }
    bool empty() const noexcept { return !meta_ || meta_->getCount() == 0; }

private:
    template<typename T>
    void writeAt(size_t pos, const T& value) {
        FieldInfo fi = meta_->getField(static_cast<unsigned>(pos));
        uint8_t* dataPtr = buffer_.data() + meta_->getOffset(static_cast<unsigned>(pos));
        int16_t* nullPtr = nullIndicatorAt(pos);
        detail::packValueWithCodec(value, dataPtr, nullPtr, &fi, tx_);
        bound_[pos] = true;
    }

    int16_t* nullIndicatorAt(size_t pos) noexcept {
        return reinterpret_cast<int16_t*>(
            buffer_.data() + meta_->getNullOffset(static_cast<unsigned>(pos)));
    }

    void initNullsToNull() {
        for (unsigned i = 0; i < meta_->getCount(); ++i) {
            int16_t* nullPtr = reinterpret_cast<int16_t*>(
                buffer_.data() + meta_->getNullOffset(i));
            *nullPtr = -1;
        }
    }

    static std::string normalizeName(std::string_view name) {
        // Trim ASCII whitespace and lowercase. Statement::namedParamMapping_
        // stores keys already in lowercase (via NamedParamParser).
        size_t b = 0, e = name.size();
        while (b < e && std::isspace(static_cast<unsigned char>(name[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(name[e - 1]))) --e;
        std::string out;
        out.reserve(e - b);
        for (size_t i = b; i < e; ++i) {
            out.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(name[i]))));
        }
        return out;
    }

    std::shared_ptr<Statement> stmt_;
    std::unique_ptr<MessageMetadata> meta_;
    std::vector<uint8_t> buffer_;
    std::vector<bool> bound_;
    Transaction* tx_ = nullptr;
};

// ---- Transaction overloads (declarations are in transaction.hpp). ----

inline unsigned Transaction::execute(const std::shared_ptr<Statement>& statement,
                                     const ParamBinder& binder) {
    if (!statement) throw FirebirdException("Invalid statement pointer");
    if (!isActive()) throw FirebirdException("Transaction is not active");
    if (binder.empty()) {
        return statement->execute(this);
    }
    return statement->execute(this,
                              binder.metadata()->getRawMetadata(),
                              binder.buffer(),
                              nullptr,
                              nullptr);
}

inline unsigned Transaction::execute(const std::unique_ptr<Statement>& statement,
                                     const ParamBinder& binder) {
    if (!statement) throw FirebirdException("Invalid statement pointer");
    if (!isActive()) throw FirebirdException("Transaction is not active");
    if (binder.empty()) {
        return statement->execute(this);
    }
    return statement->execute(this,
                              binder.metadata()->getRawMetadata(),
                              binder.buffer(),
                              nullptr,
                              nullptr);
}

inline std::unique_ptr<ResultSet> Transaction::openCursor(
        const std::shared_ptr<Statement>& statement,
        const ParamBinder& binder,
        unsigned flags) {
    if (!statement) throw FirebirdException("Invalid statement pointer");
    if (!isActive()) throw FirebirdException("Transaction is not active");
    if (binder.empty()) {
        return statement->openCursor(this, flags);
    }
    return statement->openCursor(this,
                                 binder.metadata()->getRawMetadata(),
                                 binder.buffer(),
                                 nullptr,
                                 flags);
}

inline std::unique_ptr<ResultSet> Transaction::openCursor(
        const std::unique_ptr<Statement>& statement,
        const ParamBinder& binder,
        unsigned flags) {
    if (!statement) throw FirebirdException("Invalid statement pointer");
    if (!isActive()) throw FirebirdException("Transaction is not active");
    if (binder.empty()) {
        return statement->openCursor(this, flags);
    }
    return statement->openCursor(this,
                                 binder.metadata()->getRawMetadata(),
                                 binder.buffer(),
                                 nullptr,
                                 flags);
}

} // namespace core
} // namespace fbpp
