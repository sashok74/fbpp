#pragma once

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include "fbpp/core/exception.hpp"

namespace fbpp {
namespace core {

/**
 * @brief Helper for converting JSON with named parameters to positional array
 */
class NamedParamHelper {
public:
    /**
     * @brief Convert JSON object with named parameters to positional array
     * @param jsonData Input JSON (object or array)
     * @param nameToPositions Map of parameter names to their positions
     * @param paramCount Total number of parameters expected
     * @return JSON array with positional parameters
     */
    static nlohmann::json convertToPositional(const nlohmann::json& jsonData,
                                              const std::unordered_map<std::string, std::vector<size_t>>& nameToPositions,
                                              unsigned paramCount) {
        // If already array, return as-is
        if (jsonData.is_array()) {
            if (jsonData.size() != paramCount) {
                throw FirebirdException(
                    "JSON array size mismatch: array has " + std::to_string(jsonData.size()) +
                    " elements, but query expects " + std::to_string(paramCount) + " parameters"
                );
            }
            return jsonData;
        }

        // If object, convert using named parameters
        if (!jsonData.is_object()) {
            throw FirebirdException("JSON data must be array or object");
        }

        // Create positional array initialized with nulls
        nlohmann::json result = nlohmann::json::array();
        for (unsigned i = 0; i < paramCount; ++i) {
            result.push_back(nullptr);
        }

        // Process each key-value pair in the JSON object
        for (auto& [key, value] : jsonData.items()) {
            // Convert key to lowercase for case-insensitive matching
            std::string keyLower = toLower(key);

            // Check if this is a named parameter
            auto it = nameToPositions.find(keyLower);
            if (it != nameToPositions.end()) {
                // Set value at all positions for this named parameter
                for (size_t pos : it->second) {
                    if (pos < paramCount) {
                        result[pos] = value;
                    }
                }
            } else {
                // Try to parse as positional index (e.g., "0", "1", etc.)
                try {
                    size_t pos = std::stoull(key);
                    if (pos < paramCount) {
                        result[pos] = value;
                    }
                } catch (...) {
                    // Not a valid index, ignore or could throw warning
                    // For now, we silently ignore unknown keys
                }
            }
        }

        return result;
    }

private:
    static std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
};

} // namespace core
} // namespace fbpp