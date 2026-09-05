#include "config_parse.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// 去掉首尾空白。s：原串。返回修剪后的副本。
static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());

    return s;
}

bool Config::load(const std::string& path) {
    fs::path p = fs::absolute(path);
    ini_dir = p.parent_path().string();
    std::ifstream in(p);
    if (!in) {
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "SYS") {
            if (key == "listen_addr") {
                listen_addr = val;
            } else if (key == "http_port") {
                http_port = static_cast<std::uint16_t>(std::stoi(val));
            } else if (key == "www_root") {
                www_root = val;
            } else if (key == "base_score") {
                base_score = std::stoi(val);
            }
        } else if (section == "LOG") {
            if (key == "log_level") {
                log_level = std::stoi(val);
            } else if (key == "log_file") {
                log_file = val;
            } else if (key == "log_file_size_mb") {
                log_file_size_mb = std::stoi(val);
            } else if (key == "log_max_files") {
                log_max_files = std::stoi(val);
            }
        } else if (section == "MYSQL") {
            if (key == "host") {
                mysql_host = val;
            } else if (key == "port") {
                mysql_port = static_cast<std::uint16_t>(std::stoi(val));
            } else if (key == "user") {
                mysql_user = val;
            } else if (key == "password") {
                mysql_password = val;
            } else if (key == "database") {
                mysql_database = val;
            }
        } else if (section == "NET") {
            if (key == "heartbeat_interval_sec") {
                heartbeat_interval_sec = std::stoi(val);
            } else if (key == "heartbeat_timeout_sec") {
                heartbeat_timeout_sec = std::stoi(val);
            } else if (key == "retry_max") {
                retry_max = std::stoi(val);
            } else if (key == "retry_timeout_sec") {
                retry_timeout_sec = std::stoi(val);
            }
        }
    }

    www_root = resolve(www_root);
    log_file = resolve(log_file);
    return true;
}

std::string Config::resolve(const std::string& maybe_rel) const {
    if (maybe_rel.empty()) {
        return maybe_rel;
    }

    fs::path x(maybe_rel);
    if (x.is_absolute()) {
        return x.lexically_normal().string();
    }

    return (fs::path(ini_dir) / x).lexically_normal().string();
}
