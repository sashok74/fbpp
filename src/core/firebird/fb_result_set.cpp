#include "fbpp/core/result_set.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include <cstring>

namespace fbpp {
namespace core {

ResultSet::ResultSet(Firebird::IResultSet* resultSet,
                    std::unique_ptr<MessageMetadata> metadata)
    : env_(Environment::getInstance()),
      resultSet_(resultSet),
      metadata_(std::move(metadata)),
      transaction_(),  // No owning transaction (caller manages lifetimes)
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_) {
    if (!resultSet_) {
        throw FirebirdException("Invalid result set pointer");
    }
    if (!metadata_) {
        throw FirebirdException("Invalid metadata for result set");
    }
}

ResultSet::ResultSet(Firebird::IResultSet* resultSet,
                    std::unique_ptr<MessageMetadata> metadata,
                    std::shared_ptr<Transaction> transaction)
    : env_(Environment::getInstance()),
      resultSet_(resultSet),
      metadata_(std::move(metadata)),
      transaction_(std::move(transaction)),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_) {
    if (!resultSet_) {
        throw FirebirdException("Invalid result set pointer");
    }
    if (!metadata_) {
        throw FirebirdException("Invalid metadata for result set");
    }
}

ResultSet::ResultSet(ResultSet&& other) noexcept
    : env_(Environment::getInstance()),
      resultSet_(other.resultSet_),
      metadata_(std::move(other.metadata_)),
      transaction_(std::move(other.transaction_)),
      statement_(std::move(other.statement_)),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_),
      eof_(other.eof_),
      buffer_(std::move(other.buffer_)),
      generation_(other.generation_) {
    other.resultSet_ = nullptr;
}

ResultSet& ResultSet::operator=(ResultSet&& other) noexcept {
    if (this != &other) {
        cleanup();
        resultSet_ = other.resultSet_;
        metadata_ = std::move(other.metadata_);
        transaction_ = std::move(other.transaction_);
        statement_ = std::move(other.statement_);
        eof_ = other.eof_;
        buffer_ = std::move(other.buffer_);
        generation_ = other.generation_;
        other.resultSet_ = nullptr;
    }
    return *this;
}

ResultSet::~ResultSet() {
    cleanup();
    statusWrapper_.dispose();
}

void ResultSet::cleanup() {
    if (resultSet_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

int ResultSet::fetchNext(void* buffer) {
    if (!resultSet_) {
        throw FirebirdException("Result set is not open");
    }
    if (eof_) {
        return RESULT_NO_DATA;
    }

    try {
        auto& st = status();

        int result = resultSet_->fetchNext(&st, buffer);

        if (result == RESULT_NO_DATA) {
            eof_ = true;
        } else if (result == RESULT_OK) {
            // Each successful fetch invalidates any outstanding RowView snapshots.
            ++generation_;
        }

        return result;
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

unsigned ResultSet::getBufferSize() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not available");
    }
    
    return metadata_->getMessageLength();
}

void ResultSet::close() {
    if (resultSet_) {
        // Release and null the interface on ALL paths: if close() throws and
        // the pointer survived, the destructor would retry close() on a dead
        // cursor and the interface would never be released.
        try {
            auto& st = status();
            resultSet_->close(&st);
        } catch (const Firebird::FbException& e) {
            resultSet_->release();
            resultSet_ = nullptr;
            eof_ = true;
            ++generation_;
            statement_.reset();
            transaction_.reset();
            throw FirebirdException(e);
        }
        resultSet_->release();
        resultSet_ = nullptr;
        eof_ = true;
        // Invalidate any outstanding RowView snapshots.
        ++generation_;
        // Cursor is gone — stop keeping the producing statement and the
        // transaction alive on its behalf.
        statement_.reset();
        transaction_.reset();
    }
}

std::optional<Row> ResultSet::fetchOne() {
    if (!resultSet_) {
        throw FirebirdException("ResultSet::fetchOne called on closed cursor");
    }
    if (eof_) {
        return std::nullopt;
    }
    if (buffer_.empty()) {
        buffer_.resize(getBufferSize());
    }
    int result = fetchNext(buffer_.data());
    if (result != RESULT_OK) {
        return std::nullopt;
    }
    RowView view(metadata_, buffer_.data(), transaction_.get(), this, generation_);
    return Row(view);
}

ResultSet::RowsRange ResultSet::rows() {
    return RowsRange(this);
}

ResultSet::RowsRange::Iterator::Iterator(ResultSet* rs) : rs_(rs) {
    advance();
}

ResultSet::RowsRange::Iterator& ResultSet::RowsRange::Iterator::operator++() {
    advance();
    return *this;
}

void ResultSet::RowsRange::Iterator::advance() {
    if (!rs_ || !rs_->isValid() || rs_->isEof()) {
        current_.reset();
        end_ = true;
        return;
    }
    if (rs_->buffer_.empty()) {
        rs_->buffer_.resize(rs_->getBufferSize());
    }
    int result = rs_->fetchNext(rs_->buffer_.data());
    if (result != RESULT_OK) {
        current_.reset();
        end_ = true;
        return;
    }
    current_.emplace(rs_->metadata_,
                     rs_->buffer_.data(),
                     rs_->transaction_.get(),
                     rs_,
                     rs_->generation_);
}

} // namespace core
} // namespace fbpp