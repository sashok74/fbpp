#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace fbpp {
namespace core {

/**
 * @brief Named parameter info
 */
struct NamedParamInfo {
    std::string name;      // Parameter name (without : or @)
    size_t position;       // Position in SQL (0-based)
    size_t sqlOffset;      // Offset in original SQL string
};

/**
 * @brief Parser for named parameters in SQL statements
 *
 * Parses SQL statements with named parameters (:param or @param) and converts
 * them to positional parameters (?). Handles:
 * - Case-insensitive parameter names
 * - Multiple uses of same parameter
 * - String literals and comments
 */
class NamedParamParser {
public:
    /**
     * @brief Parse SQL with named parameters
     * @param sql Original SQL with named parameters
     * @return Parsed result with converted SQL and parameter mapping
     */
    struct ParseResult {
        std::string convertedSql;                    // SQL with ? instead of named params
        std::vector<NamedParamInfo> parameters;      // List of parameters in order
        std::unordered_map<std::string, std::vector<size_t>> nameToPositions; // Map name to positions
        bool hasNamedParams = false;                 // True if named params were found
    };

    static ParseResult parse(const std::string& sql);

private:
    // Check if we're inside a string literal
    static bool isInString(const std::string& sql, size_t pos, char& quoteChar);

    // Check if we're inside a comment
    static bool isInComment(const std::string& sql, size_t pos);

    // Extract parameter name starting at position
    static std::string extractParamName(const std::string& sql, size_t& pos);

    // Convert parameter name to lowercase for case-insensitive comparison
    static std::string toLower(const std::string& str);
};

} // namespace core
} // namespace fbpp