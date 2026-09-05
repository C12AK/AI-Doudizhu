#pragma once

#include <string>

// 初始化日志：只写入滚动文件，不写终端。
// file：日志文件路径，空表示丢弃日志。
// size_mb：单个文件大约多少兆字节。
// max_files：最多保留几个旧文件。
// level：1 最细，2 普通，3 警告，4 错误，5 只记致命。
void init_log(const std::string& file, int size_mb, int max_files, int level);

// 写一条调试日志。msg：正文。无返回值。
void log_debug(const std::string& msg);

// 写一条普通日志。msg：正文。无返回值。
void log_info(const std::string& msg);

// 写一条警告。msg：正文。无返回值。
void log_warn(const std::string& msg);

// 写一条错误。msg：正文。无返回值。
void log_error(const std::string& msg);
