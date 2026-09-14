#include "log_record.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace {

std::shared_ptr<spdlog::logger> g_player;
std::shared_ptr<spdlog::logger> g_ai;

spdlog::level::level_enum to_level(int level) {
    switch (level) {
    case 1:
        return spdlog::level::debug;
    case 3:
        return spdlog::level::warn;
    case 4:
        return spdlog::level::err;
    case 5:
        return spdlog::level::critical;
    default:
        return spdlog::level::info;
    }
}

// 造一个只写滚动文件的 logger。name：名字。path：文件路径，空则不写盘。
// size_mb、max_files：滚动参数。lv：最低级别。返回 logger。
std::shared_ptr<spdlog::logger> make_file_logger(const std::string& name, const std::string& path, int size_mb,
                                                 int max_files, spdlog::level::level_enum lv) {
    std::vector<spdlog::sink_ptr> sinks;
    if (!path.empty()) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path, static_cast<std::size_t>(size_mb) * 1024 * 1024, max_files));
    }

    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    logger->set_level(lv);
    logger->flush_on(spdlog::level::info);
    return logger;
}

}  // namespace

void init_log(const std::string& file, int size_mb, int max_files, int level) {
    const auto lv = to_level(level);
    auto server = make_file_logger("ddz", file, size_mb, max_files, lv);
    spdlog::set_default_logger(server);

    std::string player_file;
    std::string ai_file;
    if (!file.empty()) {
        const auto dir = std::filesystem::path(file).parent_path();
        player_file = (dir / "ddz-player.log").string();
        ai_file = (dir / "ddz-ai.log").string();
    }

    g_player = make_file_logger("player", player_file, size_mb, max_files, lv);
    g_ai = make_file_logger("ai", ai_file, size_mb, max_files, lv);
    spdlog::register_logger(g_player);
    spdlog::register_logger(g_ai);
}

void log_debug(const std::string& msg) {
    spdlog::debug("{}", msg);
}

void log_info(const std::string& msg) {
    spdlog::info("{}", msg);
}

void log_warn(const std::string& msg) {
    spdlog::warn("{}", msg);
}

void log_error(const std::string& msg) {
    spdlog::error("{}", msg);
}

void log_player(const std::string& msg) {
    if (g_player) {
        g_player->info("{}", msg);
    }
}

void log_ai(const std::string& msg) {
    if (g_ai) {
        g_ai->info("{}", msg);
    }
}
