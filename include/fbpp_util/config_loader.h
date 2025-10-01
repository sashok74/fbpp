#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace util {

/**
 * Configuration file loader with multiple search paths
 * 
 * Search order:
 * 1. Current working directory
 * 2. Binary directory (where executable is located)
 * 3. Build directory root
 * 4. Source directory config/ folder
 * 5. System config directory (/etc/fbpp/)
 * 6. User home directory (~/.config/fbpp/)
 */
class ConfigLoader {
public:
    /**
     * Find configuration file in standard locations
     * @param filename Config file name (e.g., "test_config.json")
     * @return Full path to config file if found, empty string otherwise
     */
    static std::string findConfigFile(const std::string& filename);
    
    /**
     * Get all search paths for configuration files
     * @return Vector of paths where config files are searched
     */
    static std::vector<std::filesystem::path> getSearchPaths();
    
    /**
     * Set custom search path (useful for tests)
     * @param path Additional path to search for config files
     */
    static void addSearchPath(const std::filesystem::path& path);
    
private:
    static std::vector<std::filesystem::path> customPaths_;
};

} // namespace util