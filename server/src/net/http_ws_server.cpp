#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#ifndef _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_STL_
#endif
#ifndef _WEBSOCKETPP_CPP11_FUNCTIONAL_
#define _WEBSOCKETPP_CPP11_FUNCTIONAL_
#endif
#ifndef _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
#endif
#ifndef _WEBSOCKETPP_CPP11_MEMORY_
#define _WEBSOCKETPP_CPP11_MEMORY_
#endif

#include "http_ws_server.h"

#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using WpServer = websocketpp::server<websocketpp::config::asio>;
using Hdl = websocketpp::connection_hdl;

struct HttpWsServer::Impl {
    WpServer server;
    std::string www_root;
    OpenCb on_open;
    CloseCb on_close;
    MessageCb on_message;
    LogCb on_log;
    ConnId next_id = 1;
    TimerId next_timer = 1;
    std::map<Hdl, ConnId, std::owner_less<Hdl>> hdl_to_id;
    std::map<ConnId, Hdl> id_to_hdl;
    std::map<TimerId, std::shared_ptr<asio::steady_timer>> timers;

    // 若设置了日志回调则写一行。s：正文。无返回值。
    void log(const std::string& s) {
        if (on_log) {
            on_log(s);
        }
    }
};

// 按文件后缀给出内容类型。path：文件路径。返回头里用的类型串。
static std::string mime_of(const std::string& path) {
    auto dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    if (ext == ".html") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (ext == ".json") {
        return "application/json; charset=utf-8";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".ico") {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

HttpWsServer::HttpWsServer() : impl_(new Impl()) {
    // 关掉库自带的刷屏日志，改走我们的回调。
    impl_->server.clear_access_channels(websocketpp::log::alevel::all);
    impl_->server.clear_error_channels(websocketpp::log::elevel::all);
    impl_->server.init_asio();
    impl_->server.set_reuse_addr(true);
}

HttpWsServer::~HttpWsServer() {
    delete impl_;
}

void HttpWsServer::set_www_root(const std::string& root) {
    impl_->www_root = root;
}

void HttpWsServer::set_on_open(OpenCb cb) {
    impl_->on_open = std::move(cb);
}

void HttpWsServer::set_on_close(CloseCb cb) {
    impl_->on_close = std::move(cb);
}

void HttpWsServer::set_on_message(MessageCb cb) {
    impl_->on_message = std::move(cb);
}

void HttpWsServer::set_on_log(LogCb cb) {
    impl_->on_log = std::move(cb);
}

bool HttpWsServer::listen(const std::string& addr, std::uint16_t port) {
    auto& s = impl_->server;

    // 网页请求：去掉问号后的查询串，默认首页。
    s.set_http_handler([this](Hdl hdl) {
        auto con = impl_->server.get_con_from_hdl(hdl);
        try {
            std::string resource = con->get_resource();
            auto q = resource.find('?');
            if (q != std::string::npos) {
                resource = resource.substr(0, q);
            }
            if (resource.empty() || resource == "/") {
                resource = "/index.html";
            }
            // 禁止用 .. 走出网页根目录。
            if (resource.find("..") != std::string::npos) {
                con->set_status(websocketpp::http::status_code::forbidden);
                return;
            }

            fs::path root = fs::path(impl_->www_root).lexically_normal();
            fs::path full = (root / resource.substr(1)).lexically_normal();
            std::error_code ec;
            if (!fs::exists(full, ec) || !fs::is_regular_file(full, ec)) {
                con->set_body("not found");
                con->set_status(websocketpp::http::status_code::not_found);
                return;
            }

            std::ifstream in(full, std::ios::binary);
            std::ostringstream oss;
            oss << in.rdbuf();
            con->set_body(oss.str());
            con->append_header("Content-Type", mime_of(full.string()));
            con->append_header("Connection", "close");
            con->set_status(websocketpp::http::status_code::ok);
        } catch (const std::exception& e) {
            impl_->log(std::string("http handler: ") + e.what());
            con->set_body("error");
            con->set_status(websocketpp::http::status_code::internal_server_error);
        }
    });

    // 长连接只允许路径 /ws。
    s.set_validate_handler([this](Hdl hdl) {
        auto con = impl_->server.get_con_from_hdl(hdl);
        std::string resource = con->get_resource();
        auto q = resource.find('?');
        if (q != std::string::npos) {
            resource = resource.substr(0, q);
        }
        return resource == "/ws";
    });

    // 开关连接、收文本。
    s.set_open_handler([this](Hdl hdl) {
        ConnId id = impl_->next_id++;
        impl_->hdl_to_id[hdl] = id;
        impl_->id_to_hdl[id] = hdl;
        if (impl_->on_open) {
            impl_->on_open(id);
        }
    });

    s.set_close_handler([this](Hdl hdl) {
        auto it = impl_->hdl_to_id.find(hdl);
        if (it == impl_->hdl_to_id.end()) {
            return;
        }
        ConnId id = it->second;
        impl_->hdl_to_id.erase(it);
        impl_->id_to_hdl.erase(id);
        if (impl_->on_close) {
            impl_->on_close(id);
        }
    });

    s.set_message_handler([this](Hdl hdl, WpServer::message_ptr msg) {
        auto it = impl_->hdl_to_id.find(hdl);
        if (it == impl_->hdl_to_id.end() || !impl_->on_message) {
            return;
        }
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            impl_->on_message(it->second, msg->get_payload());
        }
    });

    // 绑定端口并开始接受连接。
    try {
        asio::ip::tcp::endpoint ep(asio::ip::make_address(addr), port);
        s.listen(ep);
        s.start_accept();
        impl_->log("listen " + addr + ":" + std::to_string(port));
        return true;
    } catch (const std::exception& e) {
        impl_->log(std::string("listen failed: ") + e.what());
        return false;
    }
}

void HttpWsServer::run() {
    impl_->server.run();
}

void HttpWsServer::send(ConnId id, const std::string& payload) {
    auto it = impl_->id_to_hdl.find(id);
    if (it == impl_->id_to_hdl.end()) {
        return;
    }

    websocketpp::lib::error_code ec;
    impl_->server.send(it->second, payload, websocketpp::frame::opcode::text, ec);
}

void HttpWsServer::close(ConnId id) {
    auto it = impl_->id_to_hdl.find(id);
    if (it == impl_->id_to_hdl.end()) {
        return;
    }

    websocketpp::lib::error_code ec;
    impl_->server.close(it->second, websocketpp::close::status::normal, "bye", ec);
}

HttpWsServer::TimerId HttpWsServer::defer(int ms, std::function<void()> fn) {
    TimerId id = impl_->next_timer++;
    auto t = std::make_shared<asio::steady_timer>(impl_->server.get_io_service());
    impl_->timers[id] = t;

    t->expires_after(std::chrono::milliseconds(ms));
    t->async_wait([this, id, fn](const asio::error_code& ec) {
        // 到期后从列表里删掉，免得取消时还握着已失效的定时器。
        impl_->timers.erase(id);
        if (!ec && fn) {
            fn();
        }
    });

    return id;
}

void HttpWsServer::post(std::function<void()> fn) {
    impl_->server.get_io_service().post(std::move(fn));
}

void HttpWsServer::cancel(TimerId id) {
    auto it = impl_->timers.find(id);
    if (it == impl_->timers.end()) {
        return;
    }
    it->second->cancel();
    impl_->timers.erase(it);
}
