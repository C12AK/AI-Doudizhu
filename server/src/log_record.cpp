#include "log_record.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <vector>

void init_log(const std::string& file, int size_mb, int max_files, int level) {
    std::vector<spdlog::sink_ptr> sinks;
    if (!file.empty()) {
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file, static_cast<std::size_t>(size_mb) * 1024 * 1024, max_files));
    }

    auto logger = std::make_shared<spdlog::logger>("ddz", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info);

    switch (level) {
    case 1:
        spdlog::set_level(spdlog::level::debug);
        break;
    case 3:
        spdlog::set_level(spdlog::level::warn);
        break;
    case 4:
        spdlog::set_level(spdlog::level::err);
        break;
    case 5:
        spdlog::set_level(spdlog::level::critical);
        break;
    default:
        spdlog::set_level(spdlog::level::info);
        break;
    }
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
