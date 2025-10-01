#include <fbpp_util/config.h>
#include <fstream>
#include <cstdlib>

namespace util {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

bool Config::load(const std::string& jsonPath) {
    auto& cfg = instance();
    
    std::ifstream file(jsonPath);
    if (file.is_open()) {
        nlohmann::json j;
        file >> j;
        cfg.loadFromJson(j);
    }
    
    cfg.loadFromEnv();
    return true;
}

void Config::loadFromJson(const nlohmann::json& j) {
    if (j.contains("db")) {
        auto& jdb = j["db"];
        if (jdb.contains("server")) db_.server = jdb["server"];
        if (jdb.contains("path")) db_.path = jdb["path"];
        if (jdb.contains("user")) db_.user = jdb["user"];
        if (jdb.contains("password")) db_.password = jdb["password"];
        if (jdb.contains("charset")) db_.charset = jdb["charset"];
        if (jdb.contains("create_if_missing")) db_.createIfMissing = jdb["create_if_missing"];
        if (jdb.contains("drop_on_cleanup")) db_.dropOnCleanup = jdb["drop_on_cleanup"];
    }
    
    if (j.contains("logging")) {
        auto& jlog = j["logging"];
        if (jlog.contains("level")) logging_.level = jlog["level"];
        if (jlog.contains("console")) logging_.console = jlog["console"];
        if (jlog.contains("file")) logging_.file = jlog["file"];
        if (jlog.contains("file_path")) logging_.filePath = jlog["file_path"];
        if (jlog.contains("rotate_max_size_mb")) logging_.rotateMaxSizeMb = jlog["rotate_max_size_mb"];
        if (jlog.contains("rotate_max_files")) logging_.rotateMaxFiles = jlog["rotate_max_files"];
    }
    
    if (j.contains("tests")) {
        auto& jtests = j["tests"];
        if (jtests.contains("skip_create_schema")) tests_.skipCreateSchema = jtests["skip_create_schema"];
        if (jtests.contains("log_level")) tests_.logLevel = jtests["log_level"];
        if (jtests.contains("temp_db_path")) tests_.tempDbPath = jtests["temp_db_path"];
    }

    if (j.contains("cache")) {
        auto& jcache = j["cache"];
        if (jcache.contains("enabled")) cache_.enabled = jcache["enabled"];
        if (jcache.contains("max_statements")) cache_.maxStatements = jcache["max_statements"];
        if (jcache.contains("ttl_minutes")) cache_.ttlMinutes = jcache["ttl_minutes"];
    }
}

void Config::loadFromEnv() {
    if (const char* val = std::getenv("FBLAB_DB_SERVER")) db_.server = val;
    if (const char* val = std::getenv("FBLAB_DB_PATH")) db_.path = val;
    if (const char* val = std::getenv("FBLAB_DB_USER")) db_.user = val;
    if (const char* val = std::getenv("FBLAB_DB_PASS")) db_.password = val;
    if (const char* val = std::getenv("FBLAB_DB_CHARSET")) db_.charset = val;
    if (const char* val = std::getenv("FBLAB_LOG_LEVEL")) logging_.level = val;

    // Cache environment variables
    if (const char* val = std::getenv("FBLAB_CACHE_ENABLED")) {
        cache_.enabled = (std::string(val) == "true" || std::string(val) == "1");
    }
    if (const char* val = std::getenv("FBLAB_CACHE_MAX_STATEMENTS")) {
        cache_.maxStatements = std::stoul(val);
    }
    if (const char* val = std::getenv("FBLAB_CACHE_TTL_MINUTES")) {
        cache_.ttlMinutes = std::stoul(val);
    }
}

}  // namespace util