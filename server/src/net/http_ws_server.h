#pragma once

#include <cstdint>
#include <functional>
#include <string>

// 同一端口上提供网页文件，并把路径 /ws 升级成长连接。
class HttpWsServer {
public:
    using ConnId = std::uint64_t;
    using TimerId = std::uint64_t;
    using OpenCb = std::function<void(ConnId)>;
    using CloseCb = std::function<void(ConnId)>;
    using MessageCb = std::function<void(ConnId, const std::string&)>;
    using LogCb = std::function<void(const std::string&)>;

    // 准备网络库。无参数。
    HttpWsServer();

    // 释放内部对象。无参数。无返回值。
    ~HttpWsServer();

    HttpWsServer(const HttpWsServer&) = delete;
    HttpWsServer& operator=(const HttpWsServer&) = delete;

    // 指定网页文件根目录。root：目录路径。无返回值。
    void set_www_root(const std::string& root);

    // 有人连上时长连接时回调。cb：参数是连接编号。无返回值。
    void set_on_open(OpenCb cb);

    // 长连接断开时回调。cb：参数是连接编号。无返回值。
    void set_on_close(CloseCb cb);

    // 收到一条文本时回调。cb：连接编号和正文。无返回值。
    void set_on_message(MessageCb cb);

    // 网络层要记日志时回调。cb：一行文字。无返回值。
    void set_on_log(LogCb cb);

    // 开始在指定地址和端口上接连接。
    // addr：监听地址。port：端口。
    // 返回：听成功为 true。
    bool listen(const std::string& addr, std::uint16_t port);

    // 进入事件循环，直到进程结束。无参数。无返回值。
    void run();

    // 向某条连接发一段文本。id：连接编号。payload：正文。无返回值。
    void send(ConnId id, const std::string& payload);

    // 主动断开某条连接。id：连接编号。无返回值。
    void close(ConnId id);

    // 若干毫秒后执行一次函数。
    // ms：延迟毫秒数。fn：到期要做的事。
    // 返回：定时器编号，可供取消。
    TimerId defer(int ms, std::function<void()> fn);

    // 把函数投到网络线程执行（可从其他线程调用）。
    // fn：要在事件循环里做的事。无返回值。
    void post(std::function<void()> fn);

    // 取消尚未触发的定时器。id：定时器编号。无返回值。
    void cancel(TimerId id);

private:
    struct Impl;
    Impl* impl_;
};
