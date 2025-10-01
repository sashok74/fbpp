#include "fbpp/core/batch.hpp"
#include "fbpp/core/batch_impl.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <sstream>

namespace fbpp::core {

// BatchImpl implementation
Batch::BatchImpl::BatchImpl(Firebird::IBatch* batch, std::shared_ptr<MessageMetadata> metadata)
    : batch_(batch), metadata_(metadata), messageCount_(0) {
    auto& env = Environment::getInstance();
    status_ = env.getMaster()->getStatus();
    statusWrapper_ = Firebird::ThrowStatusWrapper(status_);
}

Batch::BatchImpl::~BatchImpl() {
    if (batch_) {
        try {
            batch_->release();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
    if (status_) {
        status_->dispose();
    }
}

// Batch implementation
Batch::Batch(Firebird::IBatch* batch, std::shared_ptr<MessageMetadata> metadata)
    : impl_(std::make_unique<BatchImpl>(batch, metadata)) {
    if (!batch) {
        throw FirebirdException("Invalid batch pointer");
    }
    if (!metadata) {
        throw FirebirdException("Invalid metadata for batch");
    }
}

Batch::~Batch() {
    // Ensure proper cleanup
    if (impl_ && impl_->batch_) {
        try {
            cancel();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

Batch::Batch(Batch&& other) noexcept = default;
Batch& Batch::operator=(Batch&& other) noexcept = default;

BatchResult Batch::execute(Transaction* transaction) {
    if (!impl_ || !impl_->batch_) {
        throw FirebirdException("Invalid batch");
    }
    
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Valid active transaction required for batch execution");
    }
    
    auto logger = util::Logging::get();
    if (logger) {
        logger->info("Executing batch with {} messages", impl_->messageCount_);
    }
    
    BatchResult result;
    result.totalMessages = impl_->messageCount_;
    
    try {
        // Execute batch
        auto& st = impl_->status();
        Firebird::IBatchCompletionState* cs = impl_->batch_->execute(&st, 
                                                                      transaction->getRawTransaction());
        
        if (!cs) {
            throw FirebirdException("Batch execution failed - no completion state returned");
        }
        
        // Process results
        unsigned messageCount = cs->getSize(&st);
        result.totalMessages = messageCount;
        
        auto& env = Environment::getInstance();
        auto util = env.getMaster()->getUtilInterface();
        Firebird::IStatus* errorStatus = env.getMaster()->getStatus();
        
        try {
            // Collect per-message status
            for (unsigned i = 0; i < messageCount; ++i) {
                int state = cs->getState(&st, i);
                result.perMessageStatus.push_back(state);
                
                if (state == Firebird::IBatchCompletionState::EXECUTE_FAILED) {
                    result.failedCount++;
                    
                    // Get error details
                    cs->getStatus(&st, errorStatus, i);
                    char errorBuf[1024];
                    util->formatStatus(errorBuf, sizeof(errorBuf) - 1, errorStatus);
                    errorBuf[sizeof(errorBuf) - 1] = 0;
                    result.errors.push_back(std::string("Message ") + std::to_string(i) + ": " + errorBuf);
                    
                    if (logger) {
                        logger->error("Batch message {} failed: {}", i, errorBuf);
                    }
                } else if (state >= 0) {
                    result.successCount++;
                    if (logger) {
                        logger->debug("Batch message {} succeeded, {} rows affected", i, state);
                    }
                } else {
                    // SUCCESS_NO_INFO
                    result.successCount++;
                }
            }
            
            // Find all errors for detailed reporting
            unsigned errorPos = 0;
            while ((errorPos = cs->findError(&st, errorPos)) != Firebird::IBatchCompletionState::NO_MORE_ERRORS) {
                // Error already processed above
                errorPos++;
            }
            
        } catch (...) {
            if (errorStatus) {
                errorStatus->dispose();
            }
            throw;
        }
        
        if (errorStatus) {
            errorStatus->dispose();
        }
        
        // Release completion state
        cs->dispose();
        
        // Release batch after successful execution
        impl_->batch_->release();
        impl_->batch_ = nullptr;
        
        if (logger) {
            logger->info("Batch execution complete: {} success, {} failed out of {} total",
                        result.successCount, result.failedCount, result.totalMessages);
        }
        
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
    
    return result;
}

void Batch::cancel() {
    if (!impl_ || !impl_->batch_) {
        return; // Already cancelled or executed
    }
    
    try {
        impl_->batch_->cancel(&impl_->status());
        impl_->batch_->release();
        impl_->batch_ = nullptr;
        impl_->messageCount_ = 0;
        
        auto logger = util::Logging::get();
        if (logger) {
            logger->debug("Batch cancelled");
        }
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

unsigned Batch::getMessageCount() const {
    return impl_ ? impl_->messageCount_ : 0;
}

bool Batch::isValid() const {
    return impl_ && impl_->batch_ != nullptr;
}

} // namespace fbpp::core