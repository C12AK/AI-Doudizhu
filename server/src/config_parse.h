#pragma once

#include <cstdint>
#include <string>

// 进程启动时读入的各项设置。相对路径一律相对本配置文件所在目录来解析。
struct Config {
    std::string listen_addr = "127.0.0.1";
    std::uint16_t http_port = 8080;
    std::string www_root;
    int base_score = 1;

    int log_level = 2;
    std::string log_file;
    int log_file_size_mb = 50;
    int log_max_files = 5;

    std::string mysql_host = "127.0.0.1";
    std::uint16_t mysql_port = 3306;
    std::string mysql_user = "root";
    std::string mysql_password;
    std::string mysql_database = "ddz";

    int heartbeat_interval_sec = 5;
    int heartbeat_timeout_sec = 10;
    int retry_max = 3;
    int retry_timeout_sec = 10;

    std::string ini_dir;

    // 读配置文件并填入本结构。
    // path：配置文件路径。
    // 返回：打开并读完为 true，打不开为 false。
    bool load(const std::string& path);

    // 把可能是相对路径的字符串收成绝对路径。
    // maybe_rel：空串、绝对路径、或相对路径。
    // 返回：空串原样返回；否则是规范化后的绝对路径。
    std::string resolve(const std::string& maybe_rel) const;
};
