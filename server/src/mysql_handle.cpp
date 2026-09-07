#include "mysql_handle.h"

#include "log_record.h"

#include <mysql/mysql.h>

#include <cstring>

struct MysqlHandle::Impl {
    MYSQL* conn = nullptr;
    std::string host;
    std::uint16_t port = 0;
    std::string user;
    std::string password;
    std::string database;
};

MysqlHandle::~MysqlHandle() {
    if (impl_) {
        if (impl_->conn) {
            mysql_close(impl_->conn);
        }
        delete impl_;
    }
}

bool MysqlHandle::open_conn() {
    if (!impl_) {
        return false;
    }

    if (impl_->conn) {
        mysql_close(impl_->conn);
        impl_->conn = nullptr;
    }

    impl_->conn = mysql_init(nullptr);
    if (!impl_->conn) {
        return false;
    }

    if (!mysql_real_connect(impl_->conn, impl_->host.c_str(), impl_->user.c_str(), impl_->password.c_str(),
                            impl_->database.c_str(), impl_->port, nullptr, 0)) {
        mysql_close(impl_->conn);
        impl_->conn = nullptr;
        return false;
    }

    mysql_set_character_set(impl_->conn, "utf8mb4");
    return true;
}

bool MysqlHandle::connect(const std::string& host, std::uint16_t port, const std::string& user,
                          const std::string& password, const std::string& database) {
    if (!impl_) {
        impl_ = new Impl();
    }

    impl_->host = host;
    impl_->port = port;
    impl_->user = user;
    impl_->password = password;
    impl_->database = database;
    return open_conn();
}

bool MysqlHandle::ensure_conn() {
    if (!impl_) {
        return false;
    }

    if (impl_->conn && mysql_ping(impl_->conn) == 0) {
        return true;
    }

    log_warn("mysql ping failed, reconnecting");
    if (!open_conn()) {
        log_error("mysql reconnect failed");
        return false;
    }

    log_info("mysql reconnected");
    return true;
}

std::optional<AccountRow> MysqlHandle::login(const std::string& username, const std::string& password) {
    if (!ensure_conn()) {
        return std::nullopt;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(impl_->conn);
    if (!stmt) {
        return std::nullopt;
    }

    // 准备查询语句。
    const char* sql = "SELECT id, username FROM account WHERE username=? AND password=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    // 绑上用户名和密码并执行。
    MYSQL_BIND bind[2]{};
    unsigned long ulen = static_cast<unsigned long>(username.size());
    unsigned long plen = static_cast<unsigned long>(password.size());
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(username.data());
    bind[0].buffer_length = ulen;
    bind[0].length = &ulen;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(password.data());
    bind[1].buffer_length = plen;
    bind[1].length = &plen;
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return std::nullopt;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    // 取出编号和用户名。
    int id = 0;
    char uname[33]{};
    unsigned long uname_len = 0;
    MYSQL_BIND out[2]{};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &id;
    out[1].buffer_type = MYSQL_TYPE_STRING;
    out[1].buffer = uname;
    out[1].buffer_length = 32;
    out[1].length = &uname_len;
    if (mysql_stmt_bind_result(stmt, out) != 0) {
        mysql_stmt_close(stmt);
        return std::nullopt;
    }
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        return std::nullopt;
    }
    mysql_stmt_close(stmt);

    AccountRow row;
    row.id = id;
    row.username.assign(uname, uname_len);
    return row;
}
