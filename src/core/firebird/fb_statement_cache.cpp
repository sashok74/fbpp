#include "fbpp/core/statement_cache.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/named_param_parser.hpp"
#include "fbpp_util/trace.h"
#include <sstream>
#include <cctype>

namespace fbpp {
namespace core {

StatementCache::StatementCache(const CacheConfig& config)
    : config_(config), core_(std::make_shared<PoolCore>()) {}

StatementCache::~StatementCache() {
    // Mark the pool closed first (under the shared mutex): outstanding
    // checkout deleters on other threads then destroy their instances
    // instead of returning them into a dying cache.
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->closed = true;
    }
    clear();
}

std::shared_ptr<Statement> StatementCache::get(Connection* connection,
                                               const std::string& sql,
                                               unsigned flags) {
    // Parse named parameters
    auto parseResult = NamedParamParser::parse(sql);
    std::string actualSql = parseResult.hasNamedParams ? parseResult.convertedSql : sql;

    if (!config_.enabled) {
        // Cache disabled: hand out an unpooled instance (the caller fully
        // owns it; no checkout/return bookkeeping).
        return prepareInstance(
            connection, actualSql,
            parseResult.hasNamedParams ? &parseResult.nameToPositions : nullptr,
            flags);
    }

    std::lock_guard<std::mutex> lock(core_->mutex);

    // Use original SQL for cache key (with named parameters)
    std::string key = generateKey(sql, flags);

    // Check if statement exists in cache
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Key-level cache hit
        stats_.hitCount++;

        // Update LRU position
        touchEntry(key);

        // Update usage statistics
        it->second->lastUsed = std::chrono::steady_clock::now();
        it->second->useCount++;

        // Pop an idle instance; skip ones the user invalidated via free().
        auto& idle = it->second->idle;
        while (!idle.empty()) {
            auto inner = std::move(idle.back());
            idle.pop_back();
            if (inner && inner->isValid()) {
                return makeCheckout(key, std::move(inner));
            }
        }

        // All instances are checked out (or were invalidated): prepare an
        // additional instance for the same key. Still a hit — the key and
        // its metadata are cached; only the IStatement is new.
        auto inner = prepareInstance(
            connection, actualSql,
            parseResult.hasNamedParams ? &parseResult.nameToPositions : nullptr,
            flags);
        return makeCheckout(key, std::move(inner));
    }

    // Cache miss
    stats_.missCount++;

    // Check if cache is full
    if (cache_.size() >= config_.maxSize) {
        evictLRU();
    }

    auto stmt = prepareInstance(
        connection, actualSql,
        parseResult.hasNamedParams ? &parseResult.nameToPositions : nullptr,
        flags);

    // Create cache entry (the instance itself is checked out to the caller
    // and joins the idle pool when the caller releases it)
    auto entry = std::make_unique<CachedStatement>();
    entry->sql = sql;
    entry->flags = flags;
    entry->lastUsed = std::chrono::steady_clock::now();
    entry->useCount = 1;

    // Extract metadata
    extractMetadata(stmt.get(), *entry);

    // Add to cache
    cache_[key] = std::move(entry);

    // Add to LRU list (front = most recent)
    lruList_.push_front(key);
    lruMap_[key] = lruList_.begin();

    stats_.cacheSize = cache_.size();

    return makeCheckout(key, std::move(stmt));
}

std::shared_ptr<Statement> StatementCache::prepareInstance(
        Connection* connection,
        const std::string& actualSql,
        const std::unordered_map<std::string, std::vector<size_t>>* nameToPositions,
        unsigned flags) {
    auto& env = Environment::getInstance();
    auto attachment = connection->getAttachment();
    if (!attachment) {
        throw FirebirdException("Not connected to database");
    }

    Firebird::IStatus* raw = env.getMaster()->getStatus();
    Firebird::ThrowStatusWrapper st(raw);

    try {
        // Prepare against a short-lived probe transaction. Commit it
        // explicitly — letting the destructor roll it back emitted a
        // spurious "Transaction destroyed while still active" warning on
        // every prepare.
        auto tra = connection->StartTransaction();
        Firebird::IStatement* fbStmt = attachment->prepare(
            &st, tra->getTransaction(), 0, actualSql.c_str(), 3, flags);

        if (!fbStmt) {
            throw FirebirdException("prepare() returned nullptr");
        }

        auto stmt = std::make_shared<Statement>(fbStmt, connection);
        tra->Commit();

        // Set named parameter mapping if any
        if (nameToPositions) {
            stmt->setNamedParamMapping(*nameToPositions, true);
        }

        st.dispose();
        return stmt;

    } catch (const Firebird::FbException& e) {
        FirebirdException fbppEx(e);

        fbpp::util::trace(fbpp::util::TraceLevel::error, "StatementCache",
                    [&](auto& oss) {
                        oss << "Failed to prepare SQL: " << actualSql << "\n"
                            << "Error: " << fbppEx.what() << "\n"
                            << "Code: " << fbppEx.getErrorCode() << "\n"
                            << "SQLState: " << fbppEx.getSQLState();
                    });

        st.dispose();
        throw fbppEx;
    } catch (...) {
        // e.g. FirebirdException from StartTransaction()/Commit(); the raw
        // IStatus must still be disposed.
        st.dispose();
        throw;
    }
}

std::shared_ptr<Statement> StatementCache::makeCheckout(std::string key,
                                                        std::shared_ptr<Statement> inner) {
    Statement* rawPtr = inner.get();
    std::weak_ptr<PoolCore> coreWeak = core_;
    // While checked out, the deleter owns the only strong reference to the
    // instance. `this` is touched only under the core mutex with the closed
    // flag false — ~StatementCache sets closed before any member is torn
    // down, so a late return degrades to plain destruction.
    return std::shared_ptr<Statement>(
        rawPtr,
        [coreWeak, self = this, key = std::move(key),
         inner = std::move(inner)](Statement*) mutable {
            if (auto core = coreWeak.lock()) {
                std::lock_guard<std::mutex> lock(core->mutex);
                if (!core->closed) {
                    self->returnToPool(key, std::move(inner));
                    return;
                }
            }
            inner.reset();  // Pool is gone: destroy the instance.
        });
}

void StatementCache::returnToPool(const std::string& key,
                                  std::shared_ptr<Statement> inner) {
    if (!config_.enabled || !inner || !inner->isValid()) {
        // Disabled cache, or the user called free() on the instance — a
        // poisoned instance must not re-enter the pool.
        return;
    }

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return;  // Entry evicted/cleared while the instance was checked out.
    }

    if (it->second->idle.size() < kMaxIdlePerKey) {
        it->second->idle.push_back(std::move(inner));
    }
    // Else: pool for this key is full — drop the surplus instance.
}

void StatementCache::clear() {
    std::lock_guard<std::mutex> lock(core_->mutex);

    // Statements will be freed automatically via unique_ptr destructors
    cache_.clear();
    lruList_.clear();
    lruMap_.clear();

    stats_.cacheSize = 0;
}

bool StatementCache::remove(const std::string& sql, unsigned flags) {
    std::lock_guard<std::mutex> lock(core_->mutex);

    std::string key = generateKey(sql, flags);
    auto it = cache_.find(key);

    if (it != cache_.end()) {
        // Remove from LRU tracking
        auto lruIt = lruMap_.find(key);
        if (lruIt != lruMap_.end()) {
            lruList_.erase(lruIt->second);
            lruMap_.erase(lruIt);
        }

        // Remove from cache (statement will be freed)
        cache_.erase(it);

        stats_.cacheSize = cache_.size();

        return true;
    }

    return false;
}

StatementCache::Statistics StatementCache::getStatistics() const {
    std::lock_guard<std::mutex> lock(core_->mutex);

    stats_.cacheSize = cache_.size();

    // Calculate hit rate
    size_t total = stats_.hitCount + stats_.missCount;
    if (total > 0) {
        stats_.hitRate = (static_cast<double>(stats_.hitCount) / total) * 100.0;
    }

    return stats_;
}

void StatementCache::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(core_->mutex);

    if (!enabled && config_.enabled) {
        cache_.clear();
        lruList_.clear();
        lruMap_.clear();
        stats_.cacheSize = 0;
    }

    config_.enabled = enabled;
    fbpp::util::trace(fbpp::util::TraceLevel::info, "StatementCache",
                [&](auto& oss) { oss << "Cache " << (enabled ? "enabled" : "disabled"); });
}

void StatementCache::setMaxSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(core_->mutex);

    config_.maxSize = maxSize;

    // Evict entries if new size is smaller
    while (cache_.size() > config_.maxSize) {
        evictLRU();
    }

    fbpp::util::trace(fbpp::util::TraceLevel::info, "StatementCache",
                [&](auto& oss) { oss << "Cache max size set to " << maxSize; });
}

void StatementCache::setTtlMinutes(size_t ttlMinutes) {
    std::lock_guard<std::mutex> lock(core_->mutex);
    config_.ttlMinutes = ttlMinutes;
    fbpp::util::trace(fbpp::util::TraceLevel::info, "StatementCache",
                [&](auto& oss) { oss << "Cache TTL set to " << ttlMinutes << " minutes"; });
}

size_t StatementCache::removeExpired() {
    std::lock_guard<std::mutex> lock(core_->mutex);

    if (config_.ttlMinutes == 0) {
        // No TTL configured
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto ttl = std::chrono::minutes(config_.ttlMinutes);
    size_t removed = 0;

    // Find expired entries
    std::vector<std::string> toRemove;
    for (const auto& pair : cache_) {
        if (now - pair.second->lastUsed > ttl) {
            toRemove.push_back(pair.first);
        }
    }

    // Remove expired entries
    for (const auto& key : toRemove) {
        // Remove from LRU tracking
        auto lruIt = lruMap_.find(key);
        if (lruIt != lruMap_.end()) {
            lruList_.erase(lruIt->second);
            lruMap_.erase(lruIt);
        }

        // Remove from cache
        cache_.erase(key);
        removed++;
    }

    stats_.cacheSize = cache_.size();

    return removed;
}

std::string StatementCache::generateKey(const std::string& sql, unsigned flags) const {
    // Normalize SQL for consistent key generation
    std::string normalized;
    normalized.reserve(sql.size());

    bool inString = false;
    bool inComment = false;
    bool lastWasSpace = false;
    char stringDelimiter = 0;

    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        char next = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        // Handle string literals (preserve case and spaces inside strings)
        if (!inComment) {
            if (!inString && (c == '\'' || c == '"')) {
                inString = true;
                stringDelimiter = c;
                normalized += c;
                continue;
            }
            if (inString && c == stringDelimiter) {
                // Check for escaped quote
                if (next == stringDelimiter) {
                    normalized += c;
                    normalized += next;
                    ++i; // Skip next character
                    continue;
                }
                inString = false;
                stringDelimiter = 0;
                normalized += c;
                continue;
            }
        }

        // Inside string - preserve everything
        if (inString) {
            normalized += c;
            continue;
        }

        // Handle comments
        if (!inString) {
            // Single line comment
            if (c == '-' && next == '-') {
                inComment = true;
                i++; // Skip next dash
                continue;
            }
            // Multi-line comment start
            if (c == '/' && next == '*') {
                inComment = true;
                i++; // Skip asterisk
                continue;
            }
            // Multi-line comment end
            if (inComment && c == '*' && next == '/') {
                inComment = false;
                i++; // Skip slash
                continue;
            }
            // End of single line comment
            if (inComment && c == '\n') {
                inComment = false;
                continue;
            }
        }

        // Skip everything in comments
        if (inComment) {
            continue;
        }

        // Normalize whitespace (not in strings)
        if (std::isspace(c)) {
            if (!lastWasSpace) {
                normalized += ' ';
                lastWasSpace = true;
            }
            continue;
        }

        // Convert to uppercase (not in strings)
        normalized += std::toupper(c);
        lastWasSpace = false;
    }

    // Trim trailing whitespace
    while (!normalized.empty() && std::isspace(normalized.back())) {
        normalized.pop_back();
    }

    // Create final key with flags
    std::stringstream ss;
    ss << normalized << "|" << flags;
    return ss.str();
}

void StatementCache::touchEntry(const std::string& key) {
    // Move to front of LRU list
    auto it = lruMap_.find(key);
    if (it != lruMap_.end()) {
        // Remove from current position
        lruList_.erase(it->second);

        // Add to front
        lruList_.push_front(key);
        it->second = lruList_.begin();
    }
}

void StatementCache::evictLRU() {
    if (lruList_.empty()) {
        return;
    }

    // Get least recently used key (back of list)
    std::string key = lruList_.back();

    // Remove from LRU tracking
    lruList_.pop_back();
    lruMap_.erase(key);

    // Remove from cache (statement will be freed)
    cache_.erase(key);

    stats_.evictionCount++;
    stats_.cacheSize = cache_.size();
}

void StatementCache::extractMetadata(Statement* statement, CachedStatement& entry) {
    if (!statement) {
        return;
    }

    // Extract input metadata
    try {
        auto inputMeta = statement->getInputMetadata();
        if (inputMeta) {
            unsigned count = inputMeta->getCount();
            entry.inputParams.reserve(count);

            for (unsigned i = 0; i < count; i++) {
                ParamInfo param;
                auto fieldInfo = inputMeta->getField(i);
                param.name = fieldInfo.name;  // May be empty for positional
                param.sqlType = fieldInfo.type;
                param.length = fieldInfo.length;
                param.scale = fieldInfo.scale;
                param.nullable = fieldInfo.nullable;

                entry.inputParams.push_back(param);
            }

        }
    } catch (const std::exception&) {
        // Ignore metadata extraction failures
    }

    // Extract output metadata
    try {
        auto outputMeta = statement->getOutputMetadata();
        if (outputMeta) {
            unsigned count = outputMeta->getCount();
            entry.outputParams.reserve(count);

            for (unsigned i = 0; i < count; i++) {
                ParamInfo param;
                auto fieldInfo = outputMeta->getField(i);
                param.name = displayName(fieldInfo);
                param.sqlType = fieldInfo.type;
                param.length = fieldInfo.length;
                param.scale = fieldInfo.scale;
                param.nullable = fieldInfo.nullable;

                entry.outputParams.push_back(param);
            }

        }
    } catch (const std::exception&) {
        // Ignore metadata extraction failures
    }
}

bool StatementCache::parseNamedParameters(const std::string& sql,
                                         std::string& convertedSql,
                                         std::vector<std::string>& paramNames) {
    convertedSql.clear();
    convertedSql.reserve(sql.length());
    paramNames.clear();

    bool hasNamedParams = false;
    bool inString = false;
    char stringDelimiter = 0;
    bool inComment = false;
    bool inBlockComment = false;

    size_t i = 0;
    while (i < sql.length()) {
        char c = sql[i];

        // Handle block comments
        if (!inString && !inComment && c == '/' && i + 1 < sql.length() && sql[i + 1] == '*') {
            inBlockComment = true;
            convertedSql += "/*";
            i += 2;
            continue;
        }
        if (inBlockComment && c == '*' && i + 1 < sql.length() && sql[i + 1] == '/') {
            inBlockComment = false;
            convertedSql += "*/";
            i += 2;
            continue;
        }
        if (inBlockComment) {
            convertedSql += c;
            i++;
            continue;
        }

        // Handle line comments
        if (!inString && !inBlockComment && c == '-' && i + 1 < sql.length() && sql[i + 1] == '-') {
            inComment = true;
            convertedSql += "--";
            i += 2;
            continue;
        }
        if (inComment && (c == '\n' || c == '\r')) {
            inComment = false;
            convertedSql += c;
            i++;
            continue;
        }
        if (inComment) {
            convertedSql += c;
            i++;
            continue;
        }

        // Handle string literals
        if (!inComment && !inBlockComment && (c == '\'' || c == '"')) {
            if (!inString) {
                inString = true;
                stringDelimiter = c;
            } else if (c == stringDelimiter) {
                // Check for escaped quote
                if (i + 1 < sql.length() && sql[i + 1] == stringDelimiter) {
                    convertedSql += c;
                    convertedSql += sql[i + 1];
                    i += 2;
                    continue;
                }
                inString = false;
                stringDelimiter = 0;
            }
            convertedSql += c;
            i++;
            continue;
        }

        if (inString) {
            convertedSql += c;
            i++;
            continue;
        }

        // Look for named parameters :param_name
        if (c == ':' && i + 1 < sql.length()) {
            char next = sql[i + 1];
            if (std::isalpha(next) || next == '_') {
                // Found named parameter - extract name
                size_t start = i + 1;
                size_t end = start;

                while (end < sql.length() &&
                       (std::isalnum(sql[end]) || sql[end] == '_')) {
                    end++;
                }

                std::string paramName = sql.substr(start, end - start);
                paramNames.push_back(paramName);
                hasNamedParams = true;

                // Replace with ?
                convertedSql += '?';

                i = end;
                continue;
            }
        }

        // Regular character
        convertedSql += c;
        i++;
    }

    return hasNamedParams;
}

} // namespace core
} // namespace fbpp
