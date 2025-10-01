#include "fbpp/core/statement.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/batch.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <cstring>

namespace fbpp {
namespace core {

Statement::Statement(Firebird::IStatement* stmt, Connection* connection)
    : env_(Environment::getInstance()),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_),
      statement_(stmt), connection_(connection) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating statement");
    }
    if (!statement_) {
        if (logger) {
            logger->error("Invalid statement pointer");
        }
        throw FirebirdException("Invalid statement pointer");
    }
}

Statement::Statement(Statement&& other) noexcept
    : env_(Environment::getInstance()),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_),
      statement_(other.statement_),
      connection_(other.connection_),
      inputMetadata_(std::move(other.inputMetadata_)),
      outputMetadata_(std::move(other.outputMetadata_)),
      type_(other.type_),
      flags_(other.flags_),
      metadataLoaded_(other.metadataLoaded_) {
    other.statement_ = nullptr;
    other.connection_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        cleanup();
        statement_ = other.statement_;
        connection_ = other.connection_;
        inputMetadata_ = std::move(other.inputMetadata_);
        outputMetadata_ = std::move(other.outputMetadata_);
        type_ = other.type_;
        flags_ = other.flags_;
        metadataLoaded_ = other.metadataLoaded_;
        
        other.statement_ = nullptr;
        other.connection_ = nullptr;
    }
    return *this;
}

Statement::~Statement() {
    cleanup();
    statusWrapper_.dispose();
}

void Statement::cleanup() {
    if (statement_) {
        try {
            auto& st = status();
            statement_->free(&st);
        } catch (...) {
            // Suppress exceptions in destructor
        }
        statement_ = nullptr;
    }
}

unsigned Statement::execute(Transaction* transaction) {
    return execute(transaction, nullptr, nullptr, nullptr, nullptr);
}

unsigned Statement::execute(Transaction* transaction,
                           Firebird::IMessageMetadata* inMetadata,
                           const void* inBuffer,
                           Firebird::IMessageMetadata* outMetadata,
                           void* outBuffer) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Executing prepared statement");
    }
    
    if (!statement_) {
        if (logger) {
            logger->error("Statement is not prepared");
        }
        throw FirebirdException("Statement is not prepared");
    }
    if (!transaction || !transaction->isActive()) {
        if (logger) {
            logger->error("Invalid or inactive transaction");
        }
        throw FirebirdException("Invalid or inactive transaction");
    }
    
    try {
        auto& st = status();
        
        auto tra = transaction->getRawTransaction();
        // Cast away const for Firebird API (it doesn't modify the input buffer)
        statement_->execute(&st, tra, inMetadata, const_cast<void*>(inBuffer), outMetadata, outBuffer);
        
        // Get affected records count
        try {
            return static_cast<unsigned>(getAffectedRecords());
        } catch (...) {
            return 0;  // If we can't get affected records, return 0
        }
    } catch (const Firebird::FbException& e) {
        // Convert Firebird exception to our exception type
        throw FirebirdException(e);
    } catch (const FirebirdException& e) {
        throw;  // Re-throw our own exceptions
    } catch (const std::exception& e) {
        throw FirebirdException(std::string("Error executing statement: ") + e.what());
    } catch (...) {
        throw FirebirdException("Unknown error executing statement");
    }
}

std::unique_ptr<ResultSet> Statement::openCursor(Transaction* transaction, unsigned flags) {
    return openCursor(transaction, nullptr, nullptr, nullptr, flags);
}

std::unique_ptr<ResultSet> Statement::openCursor(std::shared_ptr<Transaction> transaction) {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Invalid or inactive transaction");
    }
    
    auto& st = status();
    
    auto tra = transaction->getRawTransaction();
    auto cursor = statement_->openCursor(&st, tra, nullptr, nullptr, nullptr, 0);
    
    if (!cursor) {
        throw FirebirdException("Failed to open cursor");
    }
    
    // Get output metadata for the result set
    auto meta = statement_->getOutputMetadata(&st);
    auto metadataWrapper = std::make_unique<MessageMetadata>(meta);
    
    // Create ResultSet with shared_ptr to transaction
    try {
        auto transactionShared = transaction->shared_from_this();
        return std::make_unique<ResultSet>(cursor, std::move(metadataWrapper), transactionShared);
    } catch (const std::bad_weak_ptr&) {
        // If transaction is not managed by shared_ptr, create ResultSet without it
        return std::make_unique<ResultSet>(cursor, std::move(metadataWrapper));
    }
}

std::unique_ptr<ResultSet> Statement::openCursor(Transaction* transaction,
                                                 Firebird::IMessageMetadata* inMetadata,
                                                 const void* inBuffer,
                                                 Firebird::IMessageMetadata* outMetadata,
                                                 unsigned flags) {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Invalid or inactive transaction");
    }
    
    auto& st = status();
    
    auto tra = transaction->getRawTransaction();
    // Cast away const for Firebird API (it doesn't modify the buffer)
    auto cursor = statement_->openCursor(&st, tra, inMetadata, const_cast<void*>(inBuffer), outMetadata, flags);
    
    if (!cursor) {
        throw FirebirdException("Failed to open cursor");
    }
    
    // Get output metadata for the result set
    auto meta = outMetadata ? outMetadata : statement_->getOutputMetadata(&st);
    auto metadataWrapper = std::make_unique<MessageMetadata>(meta);
    
    // Get shared_ptr from Transaction using shared_from_this
    // Transaction must be managed by shared_ptr for this to work
    try {
        auto transactionShared = transaction->shared_from_this();
        return std::make_unique<ResultSet>(cursor, std::move(metadataWrapper), transactionShared);
    } catch (const std::bad_weak_ptr&) {
        // If transaction is not managed by shared_ptr, create ResultSet without it
        // This means BLOBs won't be loaded automatically
        return std::make_unique<ResultSet>(cursor, std::move(metadataWrapper));
    }
}

unsigned Statement::getType() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    if (!metadataLoaded_) {
        auto& st = status();
        type_ = statement_->getType(&st);
        metadataLoaded_ = true;
    }
    
    return type_;
}

unsigned Statement::getFlags() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    if (!metadataLoaded_) {
        auto& st = status();
        flags_ = statement_->getFlags(&st);
        metadataLoaded_ = true;
    }
    
    return flags_;
}

std::string Statement::getPlan(bool detailed) const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    const char* plan = statement_->getPlan(&st, detailed);
    return plan ? std::string(plan) : std::string();
}

uint64_t Statement::getAffectedRecords() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    return statement_->getAffectedRecords(&st);
}

std::unique_ptr<MessageMetadata> Statement::getInputMetadata() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    try {
        auto& st = status();
        
        auto meta = statement_->getInputMetadata(&st);
        if (meta) {
            return std::make_unique<MessageMetadata>(meta);
        }
        
        return nullptr;
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

std::unique_ptr<MessageMetadata> Statement::getOutputMetadata() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    auto meta = statement_->getOutputMetadata(&st);
    if (meta) {
        return std::make_unique<MessageMetadata>(meta);
    }
    
    return nullptr;
}

unsigned Statement::getTimeout() const {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    return statement_->getTimeout(&st);
}

void Statement::setTimeout(unsigned timeout) {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    statement_->setTimeout(&st, timeout);
}

Firebird::IBatch* Statement::createBatch(Firebird::IMessageMetadata* inMetadata,
                                         unsigned parLength,
                                         const unsigned char* par) {
    if (!statement_) {
        throw FirebirdException("Statement is not prepared");
    }
    
    auto& st = status();
    
    return statement_->createBatch(&st, inMetadata, parLength, par);
}

void Statement::free() {
    if (statement_) {
        auto& st = status();
        statement_->free(&st);
        statement_ = nullptr;
        
        // Clear cached metadata
        inputMetadata_.reset();
        outputMetadata_.reset();
        metadataLoaded_ = false;
    }
}

std::unique_ptr<Batch> Statement::createBatch(Transaction* transaction,
                                              bool recordCounts,
                                              bool continueOnError) {
    if (!isValid()) {
        throw FirebirdException("Statement is not valid");
    }
    
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Valid active transaction required for batch creation");
    }
    
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Creating batch with recordCounts={}, continueOnError={}", 
                     recordCounts, continueOnError);
    }
    
    try {
        // Get attachment through statement
        // For now we'll create batch through statement itself
        // since we don't have direct attachment access
        
        // Get input metadata
        auto inMeta = getInputMetadata();
        if (!inMeta) {
            throw FirebirdException("Statement has no input parameters for batch");
        }
        
        // Build batch parameters
        auto& st = status();
        auto util = env_.getMaster()->getUtilInterface();
        auto pb = util->getXpbBuilder(&st, Firebird::IXpbBuilder::BATCH, nullptr, 0);
        
        // Set batch options
        if (recordCounts) {
            pb->insertInt(&st, Firebird::IBatch::TAG_RECORD_COUNTS, 1);
        }
        
        if (continueOnError) {
            pb->insertInt(&st, Firebird::IBatch::TAG_MULTIERROR, 1);
        }
        
        // Create Firebird batch through statement
        auto fbBatch = statement_->createBatch(&st,
                                               inMeta->getRawMetadata(),
                                               pb->getBufferLength(&st),
                                               pb->getBuffer(&st));
        
        pb->dispose();
        
        if (!fbBatch) {
            throw FirebirdException("Failed to create batch");
        }
        
        if (logger) {
            logger->debug("Batch created successfully");
        }
        
        // Create and return our Batch wrapper
        return std::make_unique<Batch>(fbBatch, std::move(inMeta));
        
    } catch (const Firebird::FbException& e) {
        throw FirebirdException(e);
    }
}

} // namespace core
} // namespace fbpp