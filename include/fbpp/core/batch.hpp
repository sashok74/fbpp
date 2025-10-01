#pragma once

#include <firebird/Interface.h>
#include <memory>
#include <vector>
#include <tuple>
#include <nlohmann/json_fwd.hpp>

namespace fbpp::core {

// Forward declarations
class Transaction;
class Statement;
class MessageMetadata;

/**
 * @brief Batch execution result
 */
struct BatchResult {
    unsigned totalMessages = 0;
    unsigned successCount = 0;
    unsigned failedCount = 0;
    std::vector<int> perMessageStatus; // Status for each message (-1 = failed, >=0 = records affected)
    std::vector<std::string> errors;   // Error messages for failed records
};

/**
 * @brief Wrapper for Firebird IBatch interface
 * 
 * Supports batch operations for efficient bulk INSERT/UPDATE/DELETE
 */
class Batch {
public:
    /**
     * @brief Construct batch from Firebird IBatch
     * @param batch Firebird batch interface
     * @param metadata Message metadata for input parameters
     */
    Batch(Firebird::IBatch* batch, std::shared_ptr<MessageMetadata> metadata);
    
    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~Batch();
    
    // Non-copyable
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    
    // Movable
    Batch(Batch&& other) noexcept;
    Batch& operator=(Batch&& other) noexcept;
    
    /**
     * @brief Add single tuple to batch
     * @tparam Args Tuple element types
     * @param params Tuple with parameters
     */
    template<typename... Args>
    void add(const std::tuple<Args...>& params);
    
    /**
     * @brief Add multiple tuples to batch
     * @tparam Args Tuple element types
     * @param paramsList Vector of tuples
     */
    template<typename... Args>
    void addMany(const std::vector<std::tuple<Args...>>& paramsList);
    
    /**
     * @brief Add single JSON object to batch
     * @param params JSON object or array with parameters
     */
    void add(const nlohmann::json& params);
    
    /**
     * @brief Add multiple JSON objects to batch
     * @param paramsList Vector of JSON objects or arrays
     */
    void addMany(const std::vector<nlohmann::json>& paramsList);
    
    /**
     * @brief Execute batch and get results
     * @param transaction Transaction to use
     * @return BatchResult with execution statistics and errors
     */
    BatchResult execute(Transaction* transaction);
    
    /**
     * @brief Cancel batch without executing
     */
    void cancel();
    
    /**
     * @brief Get number of messages added to batch
     * @return Number of messages
     */
    unsigned getMessageCount() const;
    
    /**
     * @brief Check if batch is valid
     * @return true if batch is valid
     */
    bool isValid() const;
    
private:
    class BatchImpl;
    std::unique_ptr<BatchImpl> impl_;
};

} // namespace fbpp::core