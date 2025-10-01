#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <list>
#include <chrono>
#include <mutex>
#include <vector>

namespace fbpp {
namespace core {

// Forward declarations
class Statement;
class Connection;

/**
 * @brief Configuration for statement cache
 */
struct StatementCacheConfig {
    size_t maxSize = 100;     // Maximum number of cached statements
    bool enabled = true;      // Enable/disable cache
    size_t ttlMinutes = 60;   // Time-to-live for unused statements
};

/**
 * @brief LRU cache for prepared statements
 *
 * Provides caching mechanism for prepared statements to avoid
 * repeated preparation overhead. Uses LRU eviction policy.
 */
class StatementCache {
public:
    using CacheConfig = StatementCacheConfig;

    /**
     * @brief Parameter metadata information
     */
    struct ParamInfo {
        std::string name;         // Parameter name (can be empty for positional)
        unsigned sqlType;         // SQL type code
        unsigned length;          // Data size
        short scale;             // Scale for NUMERIC/DECIMAL
        bool nullable;           // Can be NULL
    };

    /**
     * @brief Cached statement entry
     */
    struct CachedStatement {
        std::shared_ptr<Statement> statement;
        std::string sql;
        unsigned flags;
        std::chrono::steady_clock::time_point lastUsed;
        size_t useCount = 0;

        // Metadata about parameters
        std::vector<ParamInfo> inputParams;
        std::vector<ParamInfo> outputParams;
    };

    /**
     * @brief Cache statistics
     */
    struct Statistics {
        size_t cacheSize = 0;        // Current number of cached statements
        size_t hitCount = 0;         // Cache hits
        size_t missCount = 0;        // Cache misses
        size_t evictionCount = 0;    // Number of evictions
        double hitRate = 0.0;        // Hit rate percentage
    };

public:
    /**
     * @brief Construct cache with configuration
     * @param config Cache configuration
     */
    explicit StatementCache(const CacheConfig& config = {});

    /**
     * @brief Destructor - properly frees all cached statements
     */
    ~StatementCache();

    /**
     * @brief Get or create cached statement
     * @param connection Connection to use for preparation
     * @param sql SQL query text
     * @param flags Statement preparation flags
     * @return Shared pointer to cached statement
     */
    std::shared_ptr<Statement> get(Connection* connection,
                                   const std::string& sql,
                                   unsigned flags = 0);

    /**
     * @brief Clear all cached statements
     */
    void clear();

    /**
     * @brief Remove specific statement from cache
     * @param sql SQL query text
     * @param flags Statement preparation flags
     * @return true if statement was found and removed
     */
    bool remove(const std::string& sql, unsigned flags = 0);

    /**
     * @brief Get cache statistics
     * @return Current cache statistics
     */
    Statistics getStatistics() const;

    /**
     * @brief Check if cache is enabled
     * @return true if cache is enabled
     */
    bool isEnabled() const { return config_.enabled; }

    /**
     * @brief Set cache enabled state
     * @param enabled New enabled state
     */
    void setEnabled(bool enabled);

    /**
     * @brief Get maximum cache size
     * @return Maximum number of statements in cache
     */
    size_t getMaxSize() const { return config_.maxSize; }

    /**
     * @brief Set maximum cache size
     * @param maxSize New maximum size
     */
    void setMaxSize(size_t maxSize);

    /**
     * @brief Remove expired statements based on TTL
     * @return Number of statements removed
     */
    size_t removeExpired();

private:
    /**
     * @brief Generate cache key from SQL and flags
     * @param sql SQL query text
     * @param flags Statement preparation flags
     * @return Cache key string
     */
    std::string generateKey(const std::string& sql, unsigned flags) const;

    /**
     * @brief Move statement to front of LRU list
     * @param key Cache key
     */
    void touchEntry(const std::string& key);

    /**
     * @brief Evict least recently used statement
     */
    void evictLRU();

    /**
     * @brief Extract metadata from statement
     * @param statement Statement to extract metadata from
     * @param entry Cache entry to populate
     */
    void extractMetadata(Statement* statement, CachedStatement& entry);

    /**
     * @brief Parse and convert SQL with named parameters to positional
     * @param sql Original SQL with :param_name syntax
     * @param[out] convertedSql SQL with ? instead of named parameters
     * @param[out] paramNames List of parameter names in order
     * @return true if SQL contains named parameters, false otherwise
     */
    bool parseNamedParameters(const std::string& sql,
                             std::string& convertedSql,
                             std::vector<std::string>& paramNames);

private:
    // Cache configuration
    CacheConfig config_;

    // Cache storage - map of key to cached statement
    std::unordered_map<std::string, std::unique_ptr<CachedStatement>> cache_;

    // LRU tracking - list of keys in order of use (front = most recent)
    std::list<std::string> lruList_;

    // Map from key to position in LRU list for O(1) updates
    std::unordered_map<std::string, std::list<std::string>::iterator> lruMap_;

    // Thread safety
    mutable std::mutex mutex_;

    // Statistics
    mutable Statistics stats_;

    // Disable copy
    StatementCache(const StatementCache&) = delete;
    StatementCache& operator=(const StatementCache&) = delete;
};

} // namespace core
} // namespace fbpp