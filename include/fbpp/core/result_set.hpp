#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/pack_utils.hpp"
#include <firebird/Interface.h>
#include <memory>
#include <optional>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace fbpp {
namespace core {

// Forward declarations
class Statement;
class Transaction;

/**
 * @brief Wrapper for Firebird IResultSet interface
 * 
 * Provides cursor functionality for fetching rows from SELECT statements
 * Simplified API with only template methods for fetching data
 */
class ResultSet {
public:
    /**
     * @brief Fetch status codes
     */
    enum FetchStatus {
        RESULT_OK = Firebird::IStatus::RESULT_OK,           // Row fetched successfully
        RESULT_NO_DATA = Firebird::IStatus::RESULT_NO_DATA, // No more rows
        RESULT_ERROR = Firebird::IStatus::RESULT_ERROR      // Error occurred
    };

public:
    // Constructors
    //ResultSet() = default;
    ResultSet(Firebird::IResultSet* resultSet, 
              std::unique_ptr<MessageMetadata> metadata);
    
    // Constructor with Transaction weak_ptr
    ResultSet(Firebird::IResultSet* resultSet,
              std::unique_ptr<MessageMetadata> metadata,
              std::weak_ptr<Transaction> transaction);
    
    // Move semantics
    ResultSet(ResultSet&& other) noexcept;
    ResultSet& operator=(ResultSet&& other) noexcept;
    
    // Delete copy operations
    ResultSet(const ResultSet&) = delete;
    ResultSet& operator=(const ResultSet&) = delete;
    
    // Destructor
    ~ResultSet();

    /**
     * @brief Fetch next row into specified type
     * @tparam T Type to unpack row into (tuple, json, etc)
     * @param record Reference to object to populate with row data
     * @return true if row fetched successfully, false if no more rows
     */
    template<typename T>
    bool fetch(T& record) {
        if (!isValid() || eof_) {
            return false;
        }
        
        // Ensure buffer is allocated
        if (buffer_.empty()) {
            buffer_.resize(getBufferSize());
        }
        
        // Fetch next row
        int result = fetchNext(buffer_.data());
        if (result != RESULT_OK) {
            eof_ = true;
            return false;
        }
        
        // Use universal unpack
        auto transaction = transaction_.lock();
        record = unpack<T>(buffer_.data(), metadata_.get(), transaction.get());
        return true;
    }
    
    /**
     * @brief Fetch all remaining rows as specified type
     * @tparam T Type to unpack rows into (tuple, json, etc)
     * @param results Reference to vector to populate with rows
     */
    template<typename T>
    void fetchAll(std::vector<T>& results) {
        results.clear();
        T record;
        while (fetch(record)) {
            results.push_back(std::move(record));
        }
    }
    
    /**
     * @brief Get metadata for result set
     * @return Message metadata
     */
    const MessageMetadata* getMetadata() const { return metadata_.get(); }
    
    /**
     * @brief Get message buffer size
     * @return Size in bytes needed for row buffer
     */
    unsigned getBufferSize() const;
    
    /**
     * @brief Check if at end of result set
     * @return true if no more rows available
     */
    bool isEof() const { return eof_; }
    
    /**
     * @brief Close the cursor
     */
    void close();
    
    /**
     * @brief Check if result set is valid
     * @return true if result set is open and valid
     */
    bool isValid() const { return resultSet_ != nullptr; }
    
    /**
     * @brief Get raw Firebird result set interface
     * @return Raw IResultSet pointer
     */
    Firebird::IResultSet* getRawResultSet() const { return resultSet_; }
    
    /**
     * @brief Get transaction reference
     * @return Shared pointer to transaction or nullptr if expired
     */
    std::shared_ptr<Transaction> getTransaction() const { 
        return transaction_.lock(); 
    }

    /**
     * @brief Helper class for row iteration
     */
    template<typename RowType>
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = RowType;
        using difference_type = std::ptrdiff_t;
        using pointer = const RowType*;
        using reference = const RowType&;
        
        Iterator(ResultSet* rs, bool end = false)
            : resultSet_(rs), end_(end) {
            if (!end) {
                fetch();
            }
        }
        
        Iterator& operator++() {
            fetch();
            return *this;
        }
        
        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        
        bool operator==(const Iterator& other) const {
            return end_ == other.end_;
        }
        
        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }
        
        reference operator*() const { return current_.value(); }
        pointer operator->() const { return &current_.value(); }
        
    private:
        ResultSet* resultSet_;
        std::optional<RowType> current_;
        bool end_;
        
        void fetch() {
            RowType row;
            if (resultSet_->fetch(row)) {
                current_ = std::move(row);
            } else {
                current_ = std::nullopt;
                end_ = true;
            }
        }
    };
    
    /**
     * @brief Get begin iterator for range-based for loop
     * @tparam RowType Type to deserialize rows into
     * @return Begin iterator
     */
    template<typename RowType>
    Iterator<RowType> begin() { return Iterator<RowType>(this); }
    
    /**
     * @brief Get end iterator for range-based for loop
     * @tparam RowType Type to deserialize rows into
     * @return End iterator
     */
    template<typename RowType>
    Iterator<RowType> end() { return Iterator<RowType>(this, true); }

private:
    /**
     * @brief Fetch next row into buffer (internal use)
     * @param buffer Buffer to receive row data
     * @return Fetch status (RESULT_OK, RESULT_NO_DATA, or RESULT_ERROR)
     */
    int fetchNext(void* buffer);
    
    void cleanup();
    
    // Internal helper to get a ready-to-use ThrowStatusWrapper
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
    
private:
    Environment& env_;
    Firebird::IResultSet* resultSet_ = nullptr;
    std::unique_ptr<MessageMetadata> metadata_;
    std::weak_ptr<Transaction> transaction_;  // Weak reference to transaction
    Firebird::IStatus* status_;
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    bool eof_ = false;
    
    // Buffer for fetching (allocated on demand)
    mutable std::vector<uint8_t> buffer_;
};

} // namespace core
} // namespace fbpp