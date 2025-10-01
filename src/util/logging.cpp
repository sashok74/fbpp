#include <fbpp_util/logging.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>

namespace util {

std::shared_ptr<spdlog::logger> Logging::logger_;

void Logging::init(const std::string& level,
                   bool console,
                   bool file,
                   const std::string& filePath,
                   size_t maxSizeMb,
                   size_t maxFiles) {
    
    // IDEMPOTENT: Check if logger already exists
    const std::string logger_name = "binding_lab";
    if (auto existing = spdlog::get(logger_name)) {
        // Logger already exists, just update the level
        spdlog::level::level_enum log_level = spdlog::level::info;
        if (level == "trace") log_level = spdlog::level::trace;
        else if (level == "debug") log_level = spdlog::level::debug;
        else if (level == "info") log_level = spdlog::level::info;
        else if (level == "warn") log_level = spdlog::level::warn;
        else if (level == "error") log_level = spdlog::level::err;
        else if (level == "critical") log_level = spdlog::level::critical;
        
        existing->set_level(log_level);
        logger_ = existing;
        spdlog::set_default_logger(existing);
        return; // Already initialized, nothing more to do
    }
    
    // Create new logger
    std::vector<spdlog::sink_ptr> sinks;
    
    if (console) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(console_sink);
    }
    
    if (file) {
        std::filesystem::path logPath(filePath);
        if (logPath.has_parent_path()) {
            std::filesystem::create_directories(logPath.parent_path());
        }
        
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            filePath, maxSizeMb * 1024 * 1024, maxFiles);
        sinks.push_back(file_sink);
    }
    
    logger_ = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
    
    spdlog::level::level_enum log_level = spdlog::level::info;
    if (level == "trace") log_level = spdlog::level::trace;
    else if (level == "debug") log_level = spdlog::level::debug;
    else if (level == "info") log_level = spdlog::level::info;
    else if (level == "warn") log_level = spdlog::level::warn;
    else if (level == "error") log_level = spdlog::level::err;
    else if (level == "critical") log_level = spdlog::level::critical;
    
    logger_->set_level(log_level);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
    
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger> Logging::get() {
    if (!logger_) {
        init();
    }
    return logger_;
}

void Logging::shutdown() {
    if (logger_) {
        spdlog::drop("binding_lab");  // Remove logger from registry
        logger_.reset();               // Clear our reference
    }
}

}  // namespace util