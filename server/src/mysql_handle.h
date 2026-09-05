#pragma once

#include <cstdint>
#include <optional>
#include <string>

// 登录成功后带回的账号信息。
struct AccountRow {
    int id = 0;
    std::string username;
};

// 数据库连接。头文件不引入数据库厂商的头，实现藏在 .cpp 里。
class MysqlHandle {
public:
    MysqlHandle() = default;

    // 关闭连接并释放内部对象。无参数。无返回值。
    ~MysqlHandle();

    MysqlHandle(const MysqlHandle&) = delete;
    MysqlHandle& operator=(const MysqlHandle&) = delete;

    // 连上数据库。
    // host、port、user、password、database：连接参数。
    // 返回：连上为 true。
    bool connect(const std::string& host, std::uint16_t port, const std::string& user,
                 const std::string& password, const std::string& database);

    // 按用户名和密码查账号。
    // username、password：客户端送来的明文。
    // 返回：找到则带编号和用户名；找不到或出错则为空。
    std::optional<AccountRow> login(const std::string& username, const std::string& password);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
