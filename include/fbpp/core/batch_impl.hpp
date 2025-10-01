#pragma once

#include "fbpp/core/batch.hpp"
#include "fbpp/core/pack_utils.hpp"  // Use universal pack/unpack functions
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <nlohmann/json.hpp>
#include <cstring>

namespace fbpp::core {

// Implementation class (hidden from public API)
class Batch::BatchImpl {
public:
    Firebird::IBatch* batch_ = nullptr;
    std::shared_ptr<MessageMetadata> metadata_;
    Firebird::IStatus* status_ = nullptr;
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    unsigned messageCount_ = 0;
    std::vector<uint8_t> buffer_;  // Reusable buffer for packing
    
    BatchImpl(Firebird::IBatch* batch, std::shared_ptr<MessageMetadata> metadata);
    ~BatchImpl();
    
    // Get status wrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
};

// Template implementations
template<typename... Args>
void Batch::add(const std::tuple<Args...>& params) {
    if (!impl_ || !impl_->batch_) {
        throw FirebirdException("Invalid batch");
    }
    
    // Ensure buffer is sized correctly
    size_t bufferSize = impl_->metadata_->getMessageLength();
    if (impl_->buffer_.size() < bufferSize) {
        impl_->buffer_.resize(bufferSize);
    }
    
    // Clear buffer
    std::memset(impl_->buffer_.data(), 0, bufferSize);
    
    // Use universal pack function instead of TuplePacker directly
    pack(params, impl_->buffer_.data(), impl_->metadata_.get(), nullptr);
    
    // Add to batch
    try {
        impl_->batch_->add(&impl_->status(), 1, impl_->buffer_.data());
        impl_->messageCount_++;
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Added message {} to batch", impl_->messageCount_);
        }
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

template<typename... Args>
void Batch::addMany(const std::vector<std::tuple<Args...>>& paramsList) {
    if (!impl_ || !impl_->batch_) {
        throw FirebirdException("Invalid batch");
    }
    
    if (paramsList.empty()) {
        return;
    }
    
    // Calculate buffer size for all messages
    size_t messageLength = impl_->metadata_->getMessageLength();
    size_t messageAlign = impl_->metadata_->getAlignment();
    
    // Align each message to boundary
    size_t alignedLength = ((messageLength + messageAlign - 1) / messageAlign) * messageAlign;
    size_t totalSize = alignedLength * paramsList.size();
    
    // Allocate stream buffer
    std::vector<uint8_t> streamBuffer(totalSize);
    std::memset(streamBuffer.data(), 0, totalSize);
    
    // Pack all tuples using universal pack function
    uint8_t* ptr = streamBuffer.data();
    
    for (const auto& params : paramsList) {
        pack(params, ptr, impl_->metadata_.get(), nullptr);
        ptr += alignedLength;
    }
    
    // Add all messages to batch at once
    try {
        impl_->batch_->add(&impl_->status(), 
                          static_cast<unsigned>(paramsList.size()), 
                          streamBuffer.data());
        impl_->messageCount_ += paramsList.size();
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Added {} messages to batch (total: {})", 
                         paramsList.size(), impl_->messageCount_);
        }
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

// JSON support methods (non-template)
inline void Batch::add(const nlohmann::json& params) {
    if (!impl_ || !impl_->batch_) {
        throw FirebirdException("Invalid batch");
    }
    
    // Ensure buffer is sized correctly
    size_t bufferSize = impl_->metadata_->getMessageLength();
    if (impl_->buffer_.size() < bufferSize) {
        impl_->buffer_.resize(bufferSize);
    }
    
    // Clear buffer
    std::memset(impl_->buffer_.data(), 0, bufferSize);
    
    // Use universal pack function for JSON
    pack(params, impl_->buffer_.data(), impl_->metadata_.get(), nullptr);
    
    // Add to batch
    try {
        impl_->batch_->add(&impl_->status(), 1, impl_->buffer_.data());
        impl_->messageCount_++;
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Added JSON message {} to batch", impl_->messageCount_);
        }
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

inline void Batch::addMany(const std::vector<nlohmann::json>& paramsList) {
    if (!impl_ || !impl_->batch_) {
        throw FirebirdException("Invalid batch");
    }
    
    if (paramsList.empty()) {
        return;
    }
    
    // Calculate buffer size for all messages
    size_t messageLength = impl_->metadata_->getMessageLength();
    size_t messageAlign = impl_->metadata_->getAlignment();
    
    // Align each message to boundary
    size_t alignedLength = ((messageLength + messageAlign - 1) / messageAlign) * messageAlign;
    size_t totalSize = alignedLength * paramsList.size();
    
    // Allocate stream buffer
    std::vector<uint8_t> streamBuffer(totalSize);
    std::memset(streamBuffer.data(), 0, totalSize);
    
    // Pack all JSON objects using universal pack function
    uint8_t* ptr = streamBuffer.data();
    
    for (const auto& params : paramsList) {
        pack(params, ptr, impl_->metadata_.get(), nullptr);
        ptr += alignedLength;
    }
    
    // Add all messages to batch at once
    try {
        impl_->batch_->add(&impl_->status(), 
                          static_cast<unsigned>(paramsList.size()), 
                          streamBuffer.data());
        impl_->messageCount_ += paramsList.size();
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Added {} JSON messages to batch (total: {})", 
                         paramsList.size(), impl_->messageCount_);
        }
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

} // namespace fbpp::core