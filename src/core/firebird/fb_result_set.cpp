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
      transaction_(),  // Default-construct empty weak_ptr
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
                    std::weak_ptr<Transaction> transaction)
    : env_(Environment::getInstance()),
      resultSet_(resultSet), 
      metadata_(std::move(metadata)),
      transaction_(transaction),
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
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_),
      eof_(other.eof_),
      buffer_(std::move(other.buffer_)) {
    other.resultSet_ = nullptr;
}

ResultSet& ResultSet::operator=(ResultSet&& other) noexcept {
    if (this != &other) {
        cleanup();
        resultSet_ = other.resultSet_;
        metadata_ = std::move(other.metadata_);
        transaction_ = std::move(other.transaction_);
        eof_ = other.eof_;
        buffer_ = std::move(other.buffer_);
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
    
    auto& st = status();
    
    int result = resultSet_->fetchNext(&st, buffer);
    
    if (result == RESULT_NO_DATA) {
        eof_ = true;
    }
    
    return result;
}

unsigned ResultSet::getBufferSize() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not available");
    }
    
    return metadata_->getMessageLength();
}

void ResultSet::close() {
    if (resultSet_) {
        auto& st = status();
        
        resultSet_->close(&st);
        resultSet_->release();
        resultSet_ = nullptr;
        eof_ = true;
    }
}

} // namespace core
} // namespace fbpp