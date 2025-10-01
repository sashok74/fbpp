#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/type_traits.hpp"
#include <memory>
#include <string>
#include <vector>

namespace fbpp::core {

class MessageBuilder {
public:
    explicit MessageBuilder(unsigned field_count);
    ~MessageBuilder();
    
    // Disable copy
    MessageBuilder(const MessageBuilder&) = delete;
    MessageBuilder& operator=(const MessageBuilder&) = delete;
    
    // Enable move
    MessageBuilder(MessageBuilder&& other) noexcept;
    MessageBuilder& operator=(MessageBuilder&& other) noexcept;
    
    // Add field with explicit type and length
    void addField(const std::string& name, int sql_type, unsigned length, int scale = 0);
    
    // Template method to add field based on C++ type
    template<typename T>
    void addField(const std::string& name) {
        using traits = FirebirdTypeTraits<T>;
        
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>) {
            // For strings, use default length
            addField(name, traits::sql_type, traits::default_length, 0);
        } else {
            // For other types, use their size
            addField(name, traits::sql_type, static_cast<unsigned>(traits::size), traits::scale);
        }
    }
    
    // Template method with explicit length for string types
    template<typename T>
    void addFieldWithLength(const std::string& name, unsigned length) {
        using traits = FirebirdTypeTraits<T>;
        
        // Handle optional<string> specially
        if constexpr (std::is_same_v<T, std::optional<std::string>>) {
            addField(name, traits::sql_type, length, 0);
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>) {
            addField(name, traits::sql_type, length, 0);
        } else {
            // For non-string types, ignore length parameter and use default
            addField<T>(name);
        }
    }
    
    // Set field at specific index
    void setField(unsigned index, const std::string& name, int sql_type, unsigned length, int scale = 0);
    
    // Template version for setField
    template<typename T>
    void setField(unsigned index, const std::string& name) {
        using traits = FirebirdTypeTraits<T>;
        
        if constexpr (std::is_same_v<T, std::string>) {
            setField(index, name, traits::sql_type, traits::default_length, 0);
        } else {
            setField(index, name, traits::sql_type, static_cast<unsigned>(traits::size), traits::scale);
        }
    }
    
    // Build the final metadata
    std::unique_ptr<MessageMetadata> build();
    
    // Get current field count
    unsigned getFieldCount() const { return current_index_; }
    
    // Check if builder is valid
    bool isValid() const { return builder_ != nullptr; }
    
private:
    // Internal helper to get a ready-to-use ThrowStatusWrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
    
    Environment& env_;
    Firebird::IMetadataBuilder* builder_;
    Firebird::IStatus* status_;
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    unsigned field_count_;
    unsigned current_index_;
    std::vector<std::string> field_names_;
    
    void checkIndex(unsigned index) const;
    void ensureCapacity();
};

} // namespace fbpp::core