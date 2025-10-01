#include "fbpp/core/message_builder.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp_util/logging.h"
#include <sstream>
#include <algorithm>

namespace fbpp::core {

MessageBuilder::MessageBuilder(unsigned field_count)
    : env_(Environment::getInstance())
    , builder_(nullptr)
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_)
    , field_count_(field_count)
    , current_index_(0)
    , field_names_()
{
    auto master = env_.getMaster();
    
    auto& st = status();
    
    builder_ = master->getMetadataBuilder(&st, field_count);
    if (!builder_) {
        throw FirebirdException("Failed to create metadata builder");
    }
    
    field_names_.reserve(field_count);
    
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Created MessageBuilder for {} fields", field_count);
    }
}

MessageBuilder::~MessageBuilder() {
    if (builder_) {
        builder_->release();
        builder_ = nullptr;
    }
    statusWrapper_.dispose();
}

MessageBuilder::MessageBuilder(MessageBuilder&& other) noexcept
    : env_(Environment::getInstance())
    , builder_(other.builder_)
    , status_(env_.getMaster()->getStatus())
    , statusWrapper_(status_)
    , field_count_(other.field_count_)
    , current_index_(other.current_index_)
    , field_names_(std::move(other.field_names_))
{
    other.builder_ = nullptr;
    other.field_count_ = 0;
    other.current_index_ = 0;
}

MessageBuilder& MessageBuilder::operator=(MessageBuilder&& other) noexcept {
    if (this != &other) {
        if (builder_) {
            builder_->release();
        }
        
        builder_ = other.builder_;
        field_count_ = other.field_count_;
        current_index_ = other.current_index_;
        field_names_ = std::move(other.field_names_);
        
        other.builder_ = nullptr;
        other.field_count_ = 0;
        other.current_index_ = 0;
    }
    return *this;
}

void MessageBuilder::addField(const std::string& name, int sql_type, unsigned length, int scale) {
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("MessageBuilder::addField called - name: {}, sql_type: {}, length: {}, scale: {}", 
                     name, sql_type, length, scale);
    }
    
    if (!builder_) {
        throw FirebirdException("MessageBuilder is not valid");
    }
    
    if (current_index_ >= field_count_) {
        throw FirebirdException("Cannot add more fields than specified count");
    }
    
    unsigned index_to_use = current_index_;
    current_index_++;  // Increment before calling setField
    
    if (logger) {
        logger->debug("Calling setField with index: {}", index_to_use);
    }
    
    setField(index_to_use, name, sql_type, length, scale);
}

void MessageBuilder::setField(unsigned index, const std::string& name, int sql_type, unsigned length, int scale) {
    if (!builder_) {
        throw FirebirdException("MessageBuilder is not valid");
    }
    
    checkIndex(index);
    
    auto& env = Environment::getInstance();
    auto logger = util::Logging::get();
    
    if (logger) {
        logger->debug("setField called with index={}, name={}, sql_type={}, length={}, scale={}", 
                      index, name, sql_type, length, scale);
    }
    
    // Convert name to uppercase for Firebird
    std::string upper_name = name;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
    
    // Get thread-local status (automatically cleared before use)
    auto& st = status();
    
    // Try to set field name - this might not be supported by all Firebird versions
    try {
        builder_->setField(&st, index, upper_name.c_str());
        if (logger) {
            logger->debug("Successfully set field name: {}", upper_name);
        }
    } catch (...) {
        // If setField fails, continue anyway - field names are optional
        if (logger) {
            logger->debug("setField not supported or failed - continuing without field name");
        }
    }
    
    // Set field type - Firebird expects type + 1 for nullable fields
    // Check if type already has nullable flag (odd number)
    int firebird_type;
    if (sql_type & 1) {
        // Already nullable (odd type), use as-is
        firebird_type = sql_type;
        if (logger) {
            logger->debug("sql_type={} is already nullable (odd), using as-is", sql_type);
        }
    } else {
        // Make nullable by adding 1 (even -> odd)
        firebird_type = sql_type + 1;
        if (logger) {
            logger->debug("sql_type={} is not nullable (even), making nullable: firebird_type={}", 
                          sql_type, firebird_type);
        }
    }
    
    if (logger) {
        logger->debug("About to call setType with index={}, firebird_type={}", index, firebird_type);
    }
    
    builder_->setType(&st, index, firebird_type);
    
    // Set field length
    if (length > 0) {
        builder_->setLength(&st, index, length);
    }
    
    // Set scale for numeric types
    if (scale != 0) {
        builder_->setScale(&st, index, scale);
    }
    
    // Store field name for reference
    if (field_names_.size() <= index) {
        field_names_.resize(index + 1);
    }
    field_names_[index] = upper_name;
    
    if (logger) {
        logger->debug("Set field[{}]: name={}, sql_type={}, firebird_type={}, length={}, scale={}", 
                     index, upper_name, sql_type, firebird_type, length, scale);
    }
    
    // Don't update current_index_ here - let addField manage it
}

std::unique_ptr<MessageMetadata> MessageBuilder::build() {
    if (!builder_) {
        throw FirebirdException("MessageBuilder is not valid");
    }
    
    auto& env = Environment::getInstance();
    
    // Get thread-local status (automatically cleared before use)
    auto& st = status();
    
    // Get the built metadata
    Firebird::IMessageMetadata* metadata = builder_->getMetadata(&st);
    if (!metadata) {
        throw FirebirdException("Failed to build message metadata");
    }
    
    auto logger = util::Logging::get();
    if (logger) {
        logger->debug("Built MessageMetadata with {} fields", current_index_);
    }
    
    // Wrap in our MessageMetadata class
    return std::make_unique<MessageMetadata>(metadata);
}

void MessageBuilder::checkIndex(unsigned index) const {
    if (index >= field_count_) {
        std::ostringstream oss;
        oss << "Field index " << index << " out of bounds (max: " << field_count_ - 1 << ")";
        throw FirebirdException(oss.str());
    }
}

void MessageBuilder::ensureCapacity() {
    // This method can be used for future optimizations
    // Currently the field count is fixed at construction
}

} // namespace fbpp::core