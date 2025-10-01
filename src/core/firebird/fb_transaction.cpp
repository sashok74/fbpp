#include "fbpp/core/transaction.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/batch.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <cstring>

namespace fbpp {
namespace core {

Transaction::Transaction(Connection* connection, Firebird::ITransaction* transaction)
    : env_(Environment::getInstance())
    , connection_(connection)
    , transaction_(transaction)
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_)
    , active_(true) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating transaction");
    }
    if (!transaction_) {
        if (logger) {
            logger->error("Invalid transaction pointer");
        }
        throw FirebirdException("Invalid transaction pointer");
    }
}

Transaction::~Transaction() {
    // Auto-rollback if transaction is still active
    if (active_ && transaction_) {
        auto logger = util::Logging::get();
        if (logger) {
            logger->warn("Transaction being destroyed while still active - auto-rollback");
        }
        try {
            Rollback();
        }
        catch (...) {
            // Ignore errors during destructor
        }
    }
    statusWrapper_.dispose();
}

Transaction::Transaction(Transaction&& other) noexcept
    : env_(Environment::getInstance())
    , connection_(other.connection_)
    , transaction_(other.transaction_)
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_)
    , active_(other.active_) {
    other.connection_ = nullptr;
    other.transaction_ = nullptr;
    other.active_ = false;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        // Rollback current transaction if active
        if (active_ && transaction_) {
            try {
                Rollback();
            }
            catch (...) {
                // Ignore errors
            }
        }
        
        connection_ = other.connection_;
        transaction_ = other.transaction_;
        active_ = other.active_;
        
        other.connection_ = nullptr;
        other.transaction_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

void Transaction::Commit() {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Committing transaction");
    }
    
    if (!active_ || !transaction_) {
        if (logger) {
            logger->error("Cannot commit - transaction is not active");
        }
        throw FirebirdException("Transaction is not active");
    }
    
    try {
        auto& st = status();
        
        transaction_->commit(&st);
        transaction_ = nullptr;
        active_ = false;
        
        if (logger) {
            logger->info("Transaction committed successfully");
        }
    }
    catch (const Firebird::FbException& e) {
        if (logger) {
            logger->error("Failed to commit transaction");
        }
        throw FirebirdException(e);
    }
}

void Transaction::Rollback() {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Rolling back transaction");
    }
    
    if (!active_ || !transaction_) {
        if (logger) {
            logger->error("Cannot rollback - transaction is not active");
        }
        throw FirebirdException("Transaction is not active");
    }
    
    try {
        auto& st = status();
        
        transaction_->rollback(&st);
        transaction_ = nullptr;
        active_ = false;
        
        if (logger) {
            logger->info("Transaction rolled back successfully");
        }
    }
    catch (const Firebird::FbException& e) {
        if (logger) {
            logger->error("Failed to rollback transaction");
        }
        throw FirebirdException(e);
    }
}

void Transaction::CommitRetaining() {
    if (!active_ || !transaction_) {
        throw FirebirdException("Transaction is not active");
    }
    
    try {
        auto& st = status();
        
        transaction_->commitRetaining(&st);
        // Transaction remains active
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

void Transaction::RollbackRetaining() {
    if (!active_ || !transaction_) {
        throw FirebirdException("Transaction is not active");
    }
    
    try {
        auto& st = status();
        
        transaction_->rollbackRetaining(&st);
        // Transaction remains active
    }
    catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

bool Transaction::isActive() const {
    return active_ && transaction_ != nullptr;
}

std::vector<uint8_t> Transaction::loadBlob(ISC_QUAD* blobId) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Loading BLOB");
    }
    
    if (!active_ || !transaction_) {
        throw FirebirdException("Transaction is not active");
    }
    
    if (!blobId) {
        throw FirebirdException("Invalid BLOB ID");
    }
    
    // Check for NULL BLOB (all zeros)
    if (blobId->gds_quad_high == 0 && blobId->gds_quad_low == 0) {
        if (logger) {
            logger->debug("BLOB ID is NULL, returning empty data");
        }
        return std::vector<uint8_t>();
    }
    
    try {
        auto& st = status();
        auto attachment = connection_->getAttachment();
        
        // Open BLOB for reading
        Firebird::IBlob* blob = attachment->openBlob(&st, transaction_, blobId, 0, nullptr);
        if (!blob) {
            throw FirebirdException("Failed to open BLOB");
        }
        
        // Read BLOB data in segments
        std::vector<uint8_t> data;
        const unsigned segmentSize = 32768; // 32KB segments
        std::vector<uint8_t> segment(segmentSize);
        
        while (true) {
            unsigned actualLength = 0;
            int result = blob->getSegment(&st, segmentSize, segment.data(), &actualLength);
            
            if (result == Firebird::IStatus::RESULT_OK || 
                result == Firebird::IStatus::RESULT_SEGMENT) {
                // Append segment to data
                data.insert(data.end(), segment.begin(), segment.begin() + actualLength);
                
                if (result == Firebird::IStatus::RESULT_OK) {
                    // Last segment read successfully
                    break;
                }
            } else {
                // No more data or error
                break;
            }
        }
        
        // Close BLOB
        blob->close(&st);
        blob->release();
        
        if (logger) {
            logger->debug("BLOB loaded successfully, size: {} bytes", data.size());
        }
        
        return data;
    }
    catch (const Firebird::FbException& e) {
        if (logger) {
            logger->error("Failed to load BLOB");
        }
        throw FirebirdException(e);
    }
}

ISC_QUAD Transaction::createBlob(const std::vector<uint8_t>& data) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating BLOB with {} bytes", data.size());
    }
    
    if (!active_ || !transaction_) {
        throw FirebirdException("Transaction is not active");
    }
    
    try {
        auto& st = status();
        auto attachment = connection_->getAttachment();
        
        ISC_QUAD blobId;
        std::memset(&blobId, 0, sizeof(ISC_QUAD));
        
        // Create new BLOB
        Firebird::IBlob* blob = attachment->createBlob(&st, transaction_, &blobId, 0, nullptr);
        if (!blob) {
            throw FirebirdException("Failed to create BLOB");
        }
        
        // Write data in segments
        const unsigned segmentSize = 32768; // 32KB segments
        size_t offset = 0;
        
        while (offset < data.size()) {
            size_t chunkSize = std::min(static_cast<size_t>(segmentSize), data.size() - offset);
            blob->putSegment(&st, chunkSize, data.data() + offset);
            offset += chunkSize;
        }
        
        // Close BLOB
        blob->close(&st);
        blob->release();
        
        if (logger) {
            logger->debug("BLOB created successfully, ID: {}/{}", 
                         blobId.gds_quad_high, blobId.gds_quad_low);
        }
        
        return blobId;
    }
    catch (const Firebird::FbException& e) {
        if (logger) {
            logger->error("Failed to create BLOB");
        }
        throw FirebirdException(e);
    }
}

// Non-template execute implementation without parameters
unsigned Transaction::execute(const std::unique_ptr<Statement>& statement) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }
    
    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }
    
    // Delegate to Statement's private execute method
    return statement->execute(this);
}

// Non-template openCursor implementation without parameters
std::unique_ptr<ResultSet> Transaction::openCursor(const std::unique_ptr<Statement>& statement) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }
    
    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }
    
    // Delegate to Statement's openCursor method using shared_from_this
    // Note: shared_from_this() requires that the Transaction object is already managed by a shared_ptr
    try {
        return statement->openCursor(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // Fall back to raw pointer if not managed by shared_ptr
        // This can happen during transition period
        // Explicitly call non-template overload
        return statement->openCursor(this, static_cast<unsigned>(0));
    }
}

// Batch operations
std::unique_ptr<Batch> Transaction::createBatch(const std::unique_ptr<Statement>& statement,
                                               bool recordCounts,
                                               bool continueOnError) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }
    
    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }
    
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating batch with recordCounts={}, continueOnError={}", 
                     recordCounts, continueOnError);
    }
    
    // Delegate to Statement's createBatch method
    return statement->createBatch(this, recordCounts, continueOnError);
}

// ===== Overloads for shared_ptr<Statement> (for cached statements) =====

// Execute without parameters
unsigned Transaction::execute(const std::shared_ptr<Statement>& statement) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    // Delegate to Statement's private execute method
    return statement->execute(this);
}

// openCursor without parameters
std::unique_ptr<ResultSet> Transaction::openCursor(const std::shared_ptr<Statement>& statement) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    // Delegate to Statement's openCursor method
    try {
        return statement->openCursor(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // Fall back to raw pointer if not managed by shared_ptr
        return statement->openCursor(this, static_cast<unsigned>(0));
    }
}

// createBatch for shared_ptr
std::unique_ptr<Batch> Transaction::createBatch(const std::shared_ptr<Statement>& statement,
                                               bool recordCounts,
                                               bool continueOnError) {
    if (!statement) {
        throw FirebirdException("Invalid statement pointer");
    }

    if (!isActive()) {
        throw FirebirdException("Transaction is not active");
    }

    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating batch with cached statement, recordCounts={}, continueOnError={}",
                     recordCounts, continueOnError);
    }

    // Delegate to Statement's createBatch method
    return statement->createBatch(this, recordCounts, continueOnError);
}

} // namespace core
} // namespace fbpp