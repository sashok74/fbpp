#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <optional>
#include <string>
#include <vector>

namespace fbpp {
namespace core {

/**
 * @brief Field information in a message.
 *
 * Two name fields are exposed: `name` (provenance — origin column from server)
 * and `alias` (identity — what user wrote in SQL). For user-facing identification
 * always prefer `alias` with `name` as fallback; the free function
 * `displayName(field)` and `MessageMetadata::getDisplayName(index)` encode this
 * canonical convention. Using `name` directly causes collisions for literal
 * columns: `SELECT 0 AS x, 0 AS y` returns `name == "CONSTANT"` for both.
 */
struct FieldInfo {
    /// Raw column origin from server (provenance). For literals returns
    /// "CONSTANT"; for some derived columns may be empty. NOT a user-facing
    /// identifier — use displayName(field) or MessageMetadata::getDisplayName(index)
    /// for that. Bypassing this advice causes name collisions in caller-side maps.
    std::string name;
    std::string relation;    // Table name (provenance)
    std::string owner;       // Owner name (provenance)
    /// User-supplied identity from SQL. For column reference without AS
    /// equals the column name; for `expr AS foo` equals "FOO" (Firebird
    /// uppercases unquoted identifiers); for `expr AS "Foo"` equals "Foo".
    /// May be empty for unaliased expressions / aggregates.
    std::string alias;
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
 * @brief Canonical user-facing identifier for a field.
 *
 * Returns alias when non-empty, otherwise falls back to name. This is the
 * single source of truth for "what does the caller call this column" — used
 * by JSON output, statement cache, and any client building name→value maps.
 */
inline const std::string& displayName(const FieldInfo& f) noexcept {
    return f.alias.empty() ? f.name : f.alias;
}

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
     * @brief Get field information by name (matches against name OR alias).
     *
     * Two-pass lookup:
     *   1. Exact match against raw `name` and `alias` — preserves correctness
     *      for quoted identifiers (`SELECT col AS "MyCol"`).
     *   2. Case-insensitive ASCII match with whitespace trim — handles the
     *      common case where Firebird stores unquoted identifiers UPPERCASE
     *      while clients write them in lower/mixed case, and accommodates
     *      trailing/leading padding (e.g. VCL TStringField names).
     *
     * If multiple fields satisfy the same pass, the first by index is returned.
     *
     * @param name Field name (raw or case-insensitive with whitespace)
     * @return Field information or nullopt if not found
     */
    std::optional<FieldInfo> getField(const std::string& name) const;

    /**
     * @brief Get all fields information
     * @return Vector of field information
     */
    std::vector<FieldInfo> getFields() const;

    /**
     * @brief Get field index by name (matches against name OR alias).
     *
     * Same lookup semantics as getField(name) — exact pass first, then
     * case-insensitive ASCII match with whitespace trim. First by index wins.
     *
     * @param name Field name (raw or case-insensitive with whitespace)
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
     * @brief Get user-facing identifier for a field (alias, falling back to name).
     *
     * Preferred over getFieldName() / getAlias() for callers building
     * column→value maps, JSON output, or VCL FieldDefs. Avoids the literal
     * collision pitfall (`SELECT 0 AS x, 0 AS y` returns name == "CONSTANT"
     * for both, but distinct aliases).
     *
     * Equivalent to displayName(getField(index)) but skips the FieldInfo copy.
     *
     * @param index Field index
     * @return Alias if non-empty, otherwise name
     */
    std::string getDisplayName(unsigned index) const;

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
    // Pre-computed UPPERCASE forms of fields_[i].name and .alias for case-
    // insensitive lookup. Populated together with fields_ in loadFields().
    mutable std::vector<std::string> name_upper_;
    mutable std::vector<std::string> alias_upper_;
    mutable bool fieldsLoaded_ = false;

    void loadFields() const;
};

} // namespace core
} // namespace fbpp