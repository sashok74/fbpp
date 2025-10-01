#include <fbpp_util/config_loader.h>
#include <filesystem>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <linux/limits.h>  // for PATH_MAX
#endif

namespace fs = std::filesystem;

namespace util {

std::vector<fs::path> ConfigLoader::customPaths_;

std::string ConfigLoader::findConfigFile(const std::string& filename) {
    auto paths = getSearchPaths();
    
    for (const auto& dir : paths) {
        auto fullPath = dir / filename;
        if (fs::exists(fullPath) && fs::is_regular_file(fullPath)) {
            std::cout << "[ConfigLoader] Found config at: " << fullPath << std::endl;
            return fullPath.string();
        }
    }
    
    // Log searched paths for debugging
    std::cerr << "[ConfigLoader] Config file '" << filename << "' not found. Searched in:\n";
    for (const auto& dir : paths) {
        std::cerr << "  - " << dir << "\n";
    }
    
    return "";
}

std::vector<fs::path> ConfigLoader::getSearchPaths() {
    std::vector<fs::path> paths;
    
    // Add custom paths first (highest priority)
    paths.insert(paths.end(), customPaths_.begin(), customPaths_.end());
    
    // 1. Current working directory
    paths.push_back(fs::current_path());
    
    // 2. Binary directory (useful for installed apps)
    #ifdef __linux__
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath)-1);
    if (len != -1) {
        exePath[len] = '\0';
        paths.push_back(fs::path(exePath).parent_path());
    }
    #endif
    
    // 3. Build directory (CMAKE_BINARY_DIR) - set via compile definition
    #ifdef CMAKE_BINARY_DIR
    paths.push_back(fs::path(CMAKE_BINARY_DIR));
    #endif
    
    // 4. Source config directory (CMAKE_SOURCE_DIR/config) - set via compile definition
    #ifdef CMAKE_SOURCE_DIR
    paths.push_back(fs::path(CMAKE_SOURCE_DIR) / "config");
    #endif
    
    // 5. System config directory
    paths.push_back(fs::path("/etc/fbpp"));
    
    // 6. User home directory
    #ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (home) {
        paths.push_back(fs::path(home) / ".config" / "fbpp");
    }
    #else
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        paths.push_back(fs::path(home) / ".config" / "fbpp");
    }
    #endif
    
    // 7. Environment variable FBPP_CONFIG_PATH
    const char* configPath = std::getenv("FBPP_CONFIG_PATH");
    if (configPath) {
        paths.push_back(fs::path(configPath));
    }
    
    return paths;
}

void ConfigLoader::addSearchPath(const fs::path& path) {
    customPaths_.push_back(path);
}

} // namespace util