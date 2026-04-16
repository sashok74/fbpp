#pragma once

#include "fbpp/core/firebird_compat.hpp"

namespace fbpp {
namespace core {
namespace detail {

class XpbBuilderGuard {
public:
    XpbBuilderGuard() noexcept : ptr_(nullptr) {}
    explicit XpbBuilderGuard(Firebird::IXpbBuilder* p) noexcept : ptr_(p) {}

    ~XpbBuilderGuard() noexcept {
        reset();
    }

    XpbBuilderGuard(const XpbBuilderGuard&)            = delete;
    XpbBuilderGuard& operator=(const XpbBuilderGuard&) = delete;

    XpbBuilderGuard(XpbBuilderGuard&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    XpbBuilderGuard& operator=(XpbBuilderGuard&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_       = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    Firebird::IXpbBuilder* get() const noexcept { return ptr_; }
    Firebird::IXpbBuilder* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    Firebird::IXpbBuilder* release() noexcept {
        Firebird::IXpbBuilder* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    void reset(Firebird::IXpbBuilder* p = nullptr) noexcept {
        if (ptr_ && ptr_ != p) {
            try { ptr_->dispose(); } catch (...) { /* swallow cleanup errors */ }
        }
        ptr_ = p;
    }

private:
    Firebird::IXpbBuilder* ptr_;
};

class BlobGuard {
public:
    BlobGuard(Firebird::IBlob* p, Firebird::ThrowStatusWrapper* status) noexcept
        : ptr_(p), status_(status) {}

    ~BlobGuard() noexcept {
        if (ptr_) {
            try { ptr_->close(status_); } catch (...) { /* swallow */ }
            try { ptr_->release(); }       catch (...) { /* swallow */ }
        }
    }

    BlobGuard(const BlobGuard&)            = delete;
    BlobGuard& operator=(const BlobGuard&) = delete;
    BlobGuard(BlobGuard&&)                 = delete;
    BlobGuard& operator=(BlobGuard&&)      = delete;

    Firebird::IBlob* get() const noexcept { return ptr_; }
    Firebird::IBlob* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    Firebird::IBlob* release() noexcept {
        Firebird::IBlob* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

private:
    Firebird::IBlob*              ptr_;
    Firebird::ThrowStatusWrapper* status_;
};

} // namespace detail
} // namespace core
} // namespace fbpp
