#pragma once

#include "fbpp/core/environment.hpp"
#include <firebird/Interface.h>
#include <optional>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

/**
 * @brief Field information in a message
 */
struct FieldInfo {
    std::string name;        // Field name
    std::string relation;    // Table name
    std::string owner;       // Owner name
    std::string alias;       // Field alias
    unsigned type;           // SQL type code
    bool nullable;           // Can be NULL
    unsigned subType;        // Subtype (for BLOB, etc.)
    unsigned length;         // Field length in bytes
    int scale;               // Scale for NUMERIC/DECIMAL (negative for decimal places)
    unsigned charSet;        // Character set ID
    unsigned offset;         // Offset in message buffer
    unsigned nullOffset;     // Null indicator offset
};

/**
 * @brief Wrapper for Firebird IMessageMetadata interface
 * 
 * Provides metadata about message structure for input/output parameters
 */
class MessageMetadata {
public:
    // Constructors
    MessageMetadata() = default;
    explicit MessageMetadata(Firebird::IMessageMetadata* metadata);
    
    // Move semantics
    MessageMetadata(MessageMetadata&& other) noexcept;
    MessageMetadata& operator=(MessageMetadata&& other) noexcept;
    
    // Delete copy operations
    MessageMetadata(const MessageMetadata&) = delete;
    MessageMetadata& operator=(const MessageMetadata&) = delete;
    
    // Destructor
    ~MessageMetadata();

    /**
     * @brief Get number of fields in the message
     * @return Field count
     */
    unsigned getCount() const;
    
    /**
     * @brief Get field information by index
     * @param index Field index (0-based)
     * @return Field information
     */
    FieldInfo getField(unsigned index) const;
    
    /**
     * @brief Get field information by name
     * @param name Field name
     * @return Field information or nullopt if not found
     */
    std::optional<FieldInfo> getField(const std::string& name) const;
    
    /**
     * @brief Get all fields information
     * @return Vector of field information
     */
    std::vector<FieldInfo> getFields() const;
    
    /**
     * @brief Get field index by name
     * @param name Field name
     * @return Field index or nullopt if not found
     */
    std::optional<unsigned> getIndex(const std::string& name) const;
    
    /**
     * @brief Get field name by index
     * @param index Field index
     * @return Field name
     */
    std::string getFieldName(unsigned index) const;
    
    /**
     * @brief Get field type by index
     * @param index Field index
     * @return SQL type code
     */
    unsigned getType(unsigned index) const;
    
    /**
     * @brief Get field subtype by index
     * @param index Field index
     * @return Subtype
     */
    unsigned getSubType(unsigned index) const;
    
    /**
     * @brief Get field length by index
     * @param index Field index
     * @return Length in bytes
     */
    unsigned getLength(unsigned index) const;
    
    /**
     * @brief Get field scale by index
     * @param index Field index
     * @return Scale (negative for decimal places)
     */
    int getScale(unsigned index) const;
    
    /**
     * @brief Get field charset by index
     * @param index Field index
     * @return Character set ID
     */
    unsigned getCharSet(unsigned index) const;
    
    /**
     * @brief Get field offset in message buffer
     * @param index Field index
     * @return Offset in bytes
     */
    unsigned getOffset(unsigned index) const;
    
    /**
     * @brief Get null indicator offset in message buffer
     * @param index Field index
     * @return Null indicator offset
     */
    unsigned getNullOffset(unsigned index) const;
    
    /**
     * @brief Check if field is nullable
     * @param index Field index
     * @return true if field can be NULL
     */
    bool isNullable(unsigned index) const;
    
    /**
     * @brief Get table name for field
     * @param index Field index
     * @return Table name or empty string
     */
    std::string getRelation(unsigned index) const;
    
    /**
     * @brief Get owner name for field
     * @param index Field index
     * @return Owner name or empty string
     */
    std::string getOwner(unsigned index) const;
    
    /**
     * @brief Get field alias
     * @param index Field index
     * @return Field alias or empty string
     */
    std::string getAlias(unsigned index) const;
    
    /**
     * @brief Get total message length in bytes
     * @return Message buffer size
     */
    unsigned getMessageLength() const;
    
    /**
     * @brief Get aligned message length
     * @return Aligned message buffer size
     */
    unsigned getAlignedLength() const;
    
    /**
     * @brief Get alignment requirement
     * @return Alignment in bytes
     */
    unsigned getAlignment() const;
    
    /**
     * @brief Create a builder to modify metadata
     * @return Metadata builder
     */
    Firebird::IMetadataBuilder* getBuilder() const;
    
    /**
     * @brief Check if metadata is valid
     * @return true if metadata is valid
     */
    bool isValid() const { return metadata_ != nullptr; }
    
    /**
     * @brief Get raw Firebird metadata interface
     * @return Raw IMessageMetadata pointer
     */
    Firebird::IMessageMetadata* getRawMetadata() const { return metadata_; }
    
    /**
     * @brief Create metadata builder for custom metadata
     * @param fieldCount Number of fields
     * @return New metadata builder
     */
    static Firebird::IMetadataBuilder* createBuilder(unsigned fieldCount);

private:
    void cleanup();
    
    // Internal helper to get a ready-to-use ThrowStatusWrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
    
private:
    Environment& env_;
    Firebird::IMessageMetadata* metadata_ = nullptr;
    Firebird::IStatus* status_;
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    
    // Cached field information (lazy-loaded)
    mutable std::vector<FieldInfo> fields_;
    mutable bool fieldsLoaded_ = false;
    
    void loadFields() const;
};

} // namespace core
} // namespace fbpp