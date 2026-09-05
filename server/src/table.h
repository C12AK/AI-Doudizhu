#pragma once

#include "config_parse.h"
#include "game.h"
#include "json_util.h"
#include "mysql_handle.h"
#include "net/http_ws_server.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

// 全服唯一的一张桌子：座位、大厅准备、对局、断线重试。
class Table {
public:
    // 记下配置、数据库和网络，不在这里开始心跳。
    // cfg：配置。db：账号查询。net：网页与长连接。
    Table(Config cfg, MysqlHandle& db, HttpWsServer& net);

    // 启动心跳循环。无参数。无返回值。
    void start();

    // 有新的长连接进来。id：连接编号。无返回值。
    void on_open(HttpWsServer::ConnId id);

    // 长连接断开。id：连接编号。无返回值。
    void on_close(HttpWsServer::ConnId id);

    // 收到一条客户端消息。id：连接编号。payload：正文。无返回值。
    void on_message(HttpWsServer::ConnId id, const std::string& payload);

private:
    struct Seat {
        bool occupied = false;
        bool online = false;
        int account_id = 0;
        std::string username;
        HttpWsServer::ConnId conn_id = 0;
        bool ready = false;
        bool again = false;
        int retry_left = 0;
        HttpWsServer::TimerId retry_timer = 0;
        std::chrono::steady_clock::time_point last_seen{};
    };

    // 按座位号取座位。i：0..2。返回该座引用。
    Seat& at(int i) { return seats_[static_cast<std::size_t>(i)]; }
    const Seat& at(int i) const { return seats_[static_cast<std::size_t>(i)]; }

    // 对局操作失败则回错误；成功则记日志。
    // id：连接。ok：规则是否接受。err：失败短句。logline：成功日志。
    // 返回：ok。
    bool accept_move(HttpWsServer::ConnId id, bool ok, const std::string& err, const std::string& logline);

    // 按消息类型分发到登录、准备、叫抢、出牌等。
    // id：连接编号。msg：已解析的对象。无返回值。
    void handle(HttpWsServer::ConnId id, const Json& msg);

    // 给该连接回一句错误。id：连接编号。msg：给玩家看的短句。无返回值。
    void send_error(HttpWsServer::ConnId id, const std::string& msg);

    // 给某个座位推一份只含他自己手牌的状态。
    // seat：座位号。op：这条消息的名字。无返回值。
    void send_state(int seat, const std::string& op);

    // 给三个在线座位各推一份状态。op：这条消息的名字。无返回值。
    void broadcast_state(const std::string& op);

    // 拼出某个视角看到的整桌状态。
    // viewer：座位号；未入座用负数。
    // 返回：可发出去的对象。
    Json snapshot(int viewer) const;

    // 这条连接坐在哪。id：连接编号。返回座位号，没有则 -1。
    int seat_of_conn(HttpWsServer::ConnId id) const;

    // 这个账号坐在哪。account_id：账号编号。返回座位号，没有则 -1。
    int seat_of_account(int account_id) const;

    // 找一个空座位。无参数。返回座位号，满员则 -1。
    int free_seat() const;

    // 把连接绑到座位上，并取消该座的重试定时器。
    // seat：座位号。id：连接编号。无返回值。
    void attach(int seat, HttpWsServer::ConnId id);

    // 该座掉线，开始占座等待重连。seat：座位号。无返回值。
    void begin_retry(int seat);

    // 一次重试窗口到期。seat：座位号。无返回值。
    void on_retry(int seat);

    // 清空该座。seat：座位号。无返回值。
    void vacate(int seat);

    // 玩家主动离开：取消重试、让出座位；对局中则作废本局。
    // seat：座位号。id：该玩家当前连接，用来回 leave_ok。
    // 无返回值。
    void leave_seat(int seat, HttpWsServer::ConnId id);

    // 本局作废，不结算。reason：告诉玩家的原因。无返回值。
    void abort_game(const std::string& reason);

    // 三人在线且都准备则发牌。无参数。无返回值。
    void try_start();

    // 三人在结算界面都点了再来则直接开下一局。无参数。无返回值。
    void try_again();

    // 检查有没有人太久没说话，并向在线座位发探活。无参数。无返回值。
    void heartbeat();

    // 当前阶段的短名字，给客户端用。无参数。返回英文小写名。
    std::string phase_name() const;

    Config cfg_;
    MysqlHandle& db_;
    HttpWsServer& net_;
    std::array<Seat, 3> seats_{};
    std::unique_ptr<Game> game_;
    std::unordered_map<HttpWsServer::ConnId, std::chrono::steady_clock::time_point> conn_seen_;
};
