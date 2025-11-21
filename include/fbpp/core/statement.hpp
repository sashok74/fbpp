#pragma once

#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/pack_utils.hpp"
#include "fbpp/core/named_param_helper.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/result_set.hpp"
#include <memory>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

namespace fbpp {
namespace core {

// Forward declarations
class Connection;
class Transaction;
class MessageMetadata;
class Batch;

/**
 * @brief Wrapper for Firebird IStatement interface
 * 
 * Provides prepared statement functionality with support for:
 * - Parameterized queries with input/output parameters
 * - Cursor operations for SELECT statements
 * - Batch operations (Firebird 4.0+)
 * - Metadata inspection
 */
class Statement {
    // Transaction needs access to execute methods
    friend class Transaction;
    
public:
    /**
     * @brief Statement flags (from Firebird API)
     */
    enum Flags : unsigned {
        PREPARE_PREFETCH_NONE = 0,
        PREPARE_PREFETCH_TYPE = Firebird::IStatement::PREPARE_PREFETCH_TYPE,
        PREPARE_PREFETCH_INPUT_PARAMETERS = Firebird::IStatement::PREPARE_PREFETCH_INPUT_PARAMETERS,
        PREPARE_PREFETCH_OUTPUT_PARAMETERS = Firebird::IStatement::PREPARE_PREFETCH_OUTPUT_PARAMETERS,
        PREPARE_PREFETCH_LEGACY_PLAN = Firebird::IStatement::PREPARE_PREFETCH_LEGACY_PLAN,
        PREPARE_PREFETCH_DETAILED_PLAN = Firebird::IStatement::PREPARE_PREFETCH_DETAILED_PLAN,
        PREPARE_PREFETCH_AFFECTED_RECORDS = Firebird::IStatement::PREPARE_PREFETCH_AFFECTED_RECORDS,
        PREPARE_PREFETCH_FLAGS = Firebird::IStatement::PREPARE_PREFETCH_FLAGS,
        PREPARE_PREFETCH_METADATA = Firebird::IStatement::PREPARE_PREFETCH_METADATA,
        PREPARE_PREFETCH_ALL = Firebird::IStatement::PREPARE_PREFETCH_ALL
    };

    /**
     * @brief Cursor flags
     */
    enum CursorFlags : unsigned {
        CURSOR_TYPE_SCROLLABLE = Firebird::IStatement::CURSOR_TYPE_SCROLLABLE
    };

public:
    // Constructors
    Statement(Firebird::IStatement* stmt, Connection* connection);
    
    // Move semantics
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    
    // Delete copy operations
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    
    // Destructor
    ~Statement();

    /**
     * @brief Get statement type (SELECT, INSERT, UPDATE, DELETE, etc.)
     * @return Statement type code
     */
    unsigned getType() const;
    
    /**
     * @brief Get statement flags
     * @return Statement flags
     */
    unsigned getFlags() const;
    
    /**
     * @brief Get statement plan
     * @param detailed If true, get detailed plan
     * @return Execution plan as string
     */
    std::string getPlan(bool detailed = false) const;
    
    /**
     * @brief Get number of affected records from last execution
     * @return Number of affected records
     */
    uint64_t getAffectedRecords() const;
    
    /**
     * @brief Get input metadata
     * @return Input metadata or nullptr if no input parameters
     */
    std::unique_ptr<MessageMetadata> getInputMetadata() const;
    
    /**
     * @brief Get output metadata
     * @return Output metadata or nullptr if no output (non-SELECT)
     */
    std::unique_ptr<MessageMetadata> getOutputMetadata() const;
    
    /**
     * @brief Get timeout for statement execution
     * @return Timeout in milliseconds (0 = no timeout)
     */
    unsigned getTimeout() const;
    
    /**
     * @brief Set timeout for statement execution
     * @param timeout Timeout in milliseconds (0 = no timeout)
     */
    void setTimeout(unsigned timeout);
    
    /**
     * @brief Create batch for batch operations
     * @param transaction Transaction to use for batch
     * @param recordCounts Enable per-message record count statistics
     * @param continueOnError Continue batch processing on errors
     * @return Batch object for batch operations
     */
    std::unique_ptr<Batch> createBatch(Transaction* transaction,
                                       bool recordCounts = true,
                                       bool continueOnError = false);
    
    /**
     * @brief Create batch for batch operations (low-level Firebird 4.0+)
     * @param inMetadata Input metadata for batch
     * @param parLength Parameter length
     * @param par Parameters
     * @return Batch interface
     */
    Firebird::IBatch* createBatch(Firebird::IMessageMetadata* inMetadata = nullptr,
                                  unsigned parLength = 0,
                                  const unsigned char* par = nullptr);
    
    /**
     * @brief Free the prepared statement (but keep wrapper valid)
     */
    void free();
    
    /**
     * @brief Check if statement is valid
     * @return true if statement is prepared and valid
     */
    bool isValid() const { return statement_ != nullptr; }
    
    /**
     * @brief Get raw Firebird statement interface
     * @return Raw IStatement pointer (for advanced usage)
     */
    Firebird::IStatement* getRawStatement() const { return statement_; }

    /**
     * @brief Get named parameter mapping
     * @return Map of parameter names to their positions (0-based)
     */
    const std::unordered_map<std::string, std::vector<size_t>>& getNamedParamMapping() const {
        return namedParamMapping_;
    }

    /**
     * @brief Check if statement has named parameters
     * @return true if statement uses named parameters
     */
    bool hasNamedParameters() const { return hasNamedParams_; }

    /**
     * @brief Set named parameter mapping (called by StatementCache)
     * @param mapping Map of parameter names to positions
     * @param hasNamed Whether statement has named parameters
     */
    void setNamedParamMapping(const std::unordered_map<std::string, std::vector<size_t>>& mapping,
                              bool hasNamed) {
        namedParamMapping_ = mapping;
        hasNamedParams_ = hasNamed;
    }

private:
    void cleanup();
    
    // ========================================================================
    // Private methods - can only be called through Transaction
    // ========================================================================
    
    /**
     * @brief Execute statement without parameters
     * @param transaction Transaction to use
     * @return Number of affected rows (for DML) or 0
     */
    unsigned execute(Transaction* transaction);
    
    /**
     * @brief Execute statement with input parameters (using raw buffers)
     * @param transaction Transaction to use
     * @param inMetadata Input metadata
     * @param inBuffer Input buffer
     * @param outMetadata Output metadata (for RETURNING clause)
     * @param outBuffer Output buffer
     * @return Number of affected rows
     */
    unsigned execute(Transaction* transaction,
                    Firebird::IMessageMetadata* inMetadata,
                    const void* inBuffer,
                    Firebird::IMessageMetadata* outMetadata = nullptr,
                    void* outBuffer = nullptr);
    
    /**
     * @brief Execute statement with template parameters
     * @tparam InParams Input parameters type (tuple, struct, json)
     * @param transaction Transaction to use
     * @param params Input parameters
     * @return Number of affected rows
     */
    template<typename InParams>
    unsigned execute(Transaction* transaction, const InParams& params);
    
    /**
     * @brief Execute statement with input and output parameters (for RETURNING clause)
     * @tparam InParams Input parameters type (tuple)
     * @tparam OutParams Output parameters type (tuple)
     * @param transaction Transaction to use
     * @param inParams Input parameters
     * @param outTemplate Template for output parameters (for type deduction)
     * @return Pair of affected rows count and output parameters
     */

     template<typename InParams, typename OutParams>
     std::pair<unsigned, OutParams> execute(Transaction* transaction,
                                           const InParams& inParams,
                                           const OutParams& outTemplate);

    /**
     * @brief Open cursor for SELECT statement without parameters
     * @param transaction Transaction to use
     * @param flags Cursor flags (0 for default forward-only cursor)
     * @return ResultSet object for fetching rows
     */
    std::unique_ptr<ResultSet> openCursor(Transaction* transaction, 
                                          unsigned flags = 0);
    
    /**
     * @brief Open cursor for SELECT statement without parameters (shared_ptr version)
     * @param transaction Shared pointer to transaction
     * @return ResultSet object for fetching rows
     */
    std::unique_ptr<ResultSet> openCursor(std::shared_ptr<Transaction> transaction);
    
    /**
     * @brief Open cursor with input parameters (raw buffers)
     * @param transaction Transaction to use
     * @param inMetadata Input metadata
     * @param inBuffer Input buffer
     * @param outMetadata Output metadata (can override default)
     * @param flags Cursor flags
     * @return ResultSet object for fetching rows
     */
    std::unique_ptr<ResultSet> openCursor(Transaction* transaction,
                                          Firebird::IMessageMetadata* inMetadata,
                                          const void* inBuffer,
                                          Firebird::IMessageMetadata* outMetadata = nullptr,
                                          unsigned flags = 0);
    
    /**
     * @brief Open cursor with template parameters
     * @tparam InParams Input parameters type
     * @param transaction Transaction to use
     * @param params Input parameters
     * @param flags Cursor flags
     * @return ResultSet for fetching rows
     */
    template<typename InParams>
    std::unique_ptr<ResultSet> openCursor(Transaction* transaction,
                                         const InParams& params,
                                         unsigned flags = 0);

    
    /**
     * @brief Open cursor with template parameters (shared_ptr version)
     * @tparam InParams Input parameters type
     * @param transaction Shared pointer to transaction
     * @param params Input parameters
     * @return ResultSet for fetching rows
     */
    template<typename InParams>
    std::unique_ptr<ResultSet> openCursor(std::shared_ptr<Transaction> transaction,
                                          const InParams& params);
    
private:
    Environment& env_;
    Firebird::IStatement* statement_ = nullptr;
    Connection* connection_ = nullptr;  // Non-owning pointer
    Firebird::IStatus* status_;
    Firebird::ThrowStatusWrapper& status() const {
        statusWrapper_.init();
        return statusWrapper_;
    }
    mutable Firebird::ThrowStatusWrapper statusWrapper_{nullptr};
    
    // Cached metadata (lazy-loaded)
    mutable std::unique_ptr<MessageMetadata> inputMetadata_;
    mutable std::unique_ptr<MessageMetadata> outputMetadata_;
    mutable unsigned type_ = 0;
    mutable unsigned flags_ = 0;
    mutable bool metadataLoaded_ = false;

    // Named parameters support
    std::unordered_map<std::string, std::vector<size_t>> namedParamMapping_;
    bool hasNamedParams_ = false;
};

} // namespace core
} // namespace fbpp

// Include transaction.hpp for access to Transaction methods in templates
#include "fbpp/core/transaction.hpp"

namespace fbpp {
namespace core {

// Template implementation for execute with parameters
template<typename InParams>
unsigned Statement::execute(Transaction* transaction, const InParams& params) {
    if (!isValid()) {
        throw FirebirdException("Statement is not valid");
    }
    
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Invalid or inactive transaction");
    }
    
    // Get input metadata from prepared statement
    auto inMeta = getInputMetadata();
    
    if (!inMeta) {
        // No parameters expected
        if constexpr (is_tuple_v<InParams>) {
            constexpr auto tuple_size = std::tuple_size_v<InParams>;
            if constexpr (tuple_size > 0) {
                throw FirebirdException("Statement has no parameters but tuple provided");
            }
        } else if constexpr (is_json_v<InParams>) {
            if (!params.empty() && !params.is_null()) {
                throw FirebirdException("Statement has no parameters but JSON provided");
            }
        }
        // Execute without parameters
        return execute(transaction);
    }
    
    // Allocate buffer based on metadata
    size_t bufferSize = inMeta->getMessageLength();
    std::vector<uint8_t> buffer(bufferSize);

    // Handle named parameters for JSON
    if constexpr (is_json_v<InParams>) {
        if (hasNamedParams_ && !namedParamMapping_.empty()) {
            // Convert named parameters to positional
            nlohmann::json positional = NamedParamHelper::convertToPositional(
                params, namedParamMapping_, inMeta->getCount());
            pack(positional, buffer.data(), inMeta.get(), transaction);
        } else {
            // Use universal pack function as-is
            pack(params, buffer.data(), inMeta.get(), transaction);
        }
    } else {
        // Use universal pack function for non-JSON types
        pack(params, buffer.data(), inMeta.get(), transaction);
    }

    // Execute with packed parameters
    return execute(transaction,
                  inMeta->getRawMetadata(),
                  buffer.data(),
                  nullptr,
                  nullptr);
}

// Execute with RETURNING clause implementation
template<typename InParams, typename OutParams>
std::pair<unsigned, OutParams> Statement::execute(Transaction* transaction,
                                                  const InParams& inParams,
                                                  const OutParams& outTemplate) {
    if (!isValid()) {
        throw FirebirdException("Statement is not valid");
    }
    
    if (!transaction) {
        throw FirebirdException("Transaction is null");
    }
    
    // Handle input parameters based on type
    std::unique_ptr<MessageMetadata> inMeta;
    std::vector<uint8_t> inBuffer;
    
    // Get input metadata
    inMeta = getInputMetadata();
    if (inMeta) {
        inBuffer.resize(inMeta->getMessageLength());
        
        if constexpr (is_tuple_v<InParams>) {
            // Use universal pack for tuple
            pack(inParams, inBuffer.data(), inMeta.get(), transaction);
        } else if constexpr (is_json_v<InParams>) {
            // Handle named parameters for JSON
            if (hasNamedParams_ && !namedParamMapping_.empty()) {
                // Convert named parameters to positional
                nlohmann::json positional = NamedParamHelper::convertToPositional(
                    inParams, namedParamMapping_, inMeta->getCount());
                pack(positional, inBuffer.data(), inMeta.get(), transaction);
            } else {
                // Use universal pack for JSON
                pack(inParams, inBuffer.data(), inMeta.get(), transaction);
            }
        } else {
            throw FirebirdException("Unsupported input parameter type for execute with RETURNING");
        }
    }
    
    // Prepare output buffer
    auto outMeta = getOutputMetadata();
    if (!outMeta) {
        throw FirebirdException("No output metadata for RETURNING clause");
    }
    
    std::vector<uint8_t> outBuffer(outMeta->getMessageLength());
    
    // Execute with both input and output buffers
    unsigned affectedRows = execute(transaction,
                                   inMeta ? inMeta->getRawMetadata() : nullptr,
                                   inMeta ? inBuffer.data() : nullptr,
                                   outMeta->getRawMetadata(),
                                   outBuffer.data());
    
    // Use universal unpack function
    OutParams result = unpack<OutParams>(outBuffer.data(), outMeta.get(), transaction);
    
    return {affectedRows, result};
}

// Template implementation for openCursor with parameters
template<typename InParams>
std::unique_ptr<ResultSet> Statement::openCursor(Transaction* transaction,
                                                 const InParams& params,
                                                 unsigned flags) {
    if (!isValid()) {
        throw FirebirdException("Statement is not valid");
    }
    
    if (!transaction || !transaction->isActive()) {
        throw FirebirdException("Invalid or inactive transaction");
    }
    
    // Get input metadata from prepared statement
    auto inMeta = getInputMetadata();
    
    if (!inMeta) {
        // No parameters expected
        if constexpr (is_tuple_v<InParams>) {
            constexpr auto tuple_size = std::tuple_size_v<InParams>;
            if constexpr (tuple_size > 0) {
                throw FirebirdException("Statement has no parameters but tuple provided");
            }
        } else if constexpr (is_json_v<InParams>) {
            if (!params.empty() && !params.is_null()) {
                throw FirebirdException("Statement has no parameters but JSON provided");
            }
        }
        // Open cursor without parameters
        return openCursor(transaction, flags);
    }
    
    // Allocate buffer based on metadata
    size_t bufferSize = inMeta->getMessageLength();
    std::vector<uint8_t> buffer(bufferSize);

    // Handle named parameters for JSON
    if constexpr (is_json_v<InParams>) {
        if (hasNamedParams_ && !namedParamMapping_.empty()) {
            // Convert named parameters to positional
            nlohmann::json positional = NamedParamHelper::convertToPositional(
                params, namedParamMapping_, inMeta->getCount());
            pack(positional, buffer.data(), inMeta.get(), transaction);
        } else {
            // Use universal pack function as-is
            pack(params, buffer.data(), inMeta.get(), transaction);
        }
    } else {
        // Use universal pack function for non-JSON types
        pack(params, buffer.data(), inMeta.get(), transaction);
    }

    // Open cursor with packed parameters
    return openCursor(transaction,
                     inMeta->getRawMetadata(),
                     buffer.data(),
                     nullptr,
                     flags);
}

// OpenCursor with template parameters (shared_ptr version)
template<typename InParams>
std::unique_ptr<ResultSet> Statement::openCursor(std::shared_ptr<Transaction> transaction,
                                                 const InParams& params) {
    return openCursor(transaction.get(), params, 0);
}

} // namespace core
} // namespace fbpp