#pragma once

#include "config_parse.h"
#include "deal_eval.h"
#include "game.h"
#include "json_util.h"
#include "mysql_handle.h"
#include "net/http_ws_server.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

// 全服大厅：多房间、入座、对局、断线重试。
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
        bool board_closed = false;
        int retry_left = 0;
        HttpWsServer::TimerId retry_timer = 0;
        std::chrono::steady_clock::time_point last_seen{};
    };

    struct Room {
        std::string id;
        std::string name;
        int match_total = 1;
        int match_done = 0;
        bool set_over = false;
        std::string last_spring = "none";
        std::array<int, 3> set_score{};
        std::array<int, 3> last_score{};
        std::array<Seat, 3> seats{};
        std::unique_ptr<Game> game;
        bool dealing = false;
        int deal_seq = 0;
    };

    struct Session {
        int account_id = 0;
        std::string username;
        std::string room_id;
        std::chrono::steady_clock::time_point last_seen{};
    };

    struct Located {
        Room* room = nullptr;
        int seat = -1;
    };

    // 按座位号取座位。r：房间。i：0..2。返回该座引用。
    Seat& at(Room& r, int i) { return r.seats[static_cast<std::size_t>(i)]; }
    const Seat& at(const Room& r, int i) const { return r.seats[static_cast<std::size_t>(i)]; }

    // 房间里已占座人数。r：房间。返回 0～3。
    int occupied_count(const Room& r) const;

    // 房间是否无人。r：房间。无人则为 true。
    bool room_empty(const Room& r) const { return occupied_count(r) == 0; }

    // 按连接找所在房间和座位。id：连接编号。不在房间则 room 为空。
    Located locate_conn(HttpWsServer::ConnId id);

    // 按账号找所在房间和座位（含断线占座）。account_id：账号编号。
    Located locate_account(int account_id);

    // 按编号取房间。id：房间号。没有则为空。
    Room* find_room(const std::string& id);

    // 生成不与现有房间号冲突的 8 位数字。无参数。失败返回空串。
    std::string alloc_room_id();

    // 是否已有同名房间。name：房间名。已有则为 true。
    bool name_taken(const std::string& name) const;

    // 对局操作失败则回错误；成功则记日志。
    // id：连接。ok：规则是否接受。err：失败短句。logline：成功日志。
    // 返回：ok。
    bool accept_move(HttpWsServer::ConnId id, bool ok, const std::string& err, const std::string& logline);

    // 按消息类型分发。id：连接编号。msg：已解析的对象。无返回值。
    void handle(HttpWsServer::ConnId id, const Json& msg);

    // 给该连接回一句错误。id：连接编号。msg：给玩家看的短句。无返回值。
    void send_error(HttpWsServer::ConnId id, const std::string& msg);

    // 拼房间列表给大厅。s：该玩家会话。返回可发出去的对象。
    Json hall_snapshot(const Session& s) const;

    // 把大厅快照发给该连接。id：连接编号。无返回值。
    void send_hall(HttpWsServer::ConnId id);

    // 给所有还在大厅的人刷新房间列表。无参数。无返回值。
    void broadcast_hall();

    // 给某个座位推一份只含他自己手牌的状态。
    // r：房间。seat：座位号。op：这条消息的名字。无返回值。
    void send_state(Room& r, int seat, const std::string& op);

    // 给该房间三个在线座位各推一份状态。r：房间。op：这条消息的名字。无返回值。
    void broadcast_state(Room& r, const std::string& op);

    // 拼出某个视角看到的房间状态。
    // r：房间。viewer：座位号；未入座用负数。
    // 返回：可发出去的对象。
    Json snapshot(const Room& r, int viewer) const;

    // 这条连接坐在该房间哪。r：房间。id：连接编号。返回座位号，没有则 -1。
    int seat_of_conn(const Room& r, HttpWsServer::ConnId id) const;

    // 找一个空座位。r：房间。返回座位号，满员则 -1。
    int free_seat(const Room& r) const;

    // 把连接绑到座位上，并取消该座的重试定时器。
    // r：房间。seat：座位号。id：连接编号。无返回值。
    void attach(Room& r, int seat, HttpWsServer::ConnId id);

    // 该座掉线，开始占座等待重连。r：房间。seat：座位号。无返回值。
    void begin_retry(Room& r, int seat);

    // 一次重试窗口到期。room_id：房间号。seat：座位号。无返回值。
    void on_retry(const std::string& room_id, int seat);

    // 清空该座。r：房间。seat：座位号。无返回值。
    void vacate(Room& r, int seat);

    // 若房间已空则删掉并刷新大厅。room_id：房间号。无返回值。
    void maybe_dissolve(const std::string& room_id);

    // 退出房间，回到大厅。r：房间。seat：座位号。id：连接。无返回值。
    void leave_room(Room& r, int seat, HttpWsServer::ConnId id);

    // 一大局是否已经开始（打牌、小局间隙、得分榜都算）。r：房间。已开始则为 true。
    bool match_in_progress(const Room& r) const;

    // 本小局作废，不结算；大局进度清掉。r：房间。reason：告诉玩家的原因。无返回值。
    void abort_game(Room& r, const std::string& reason);

    // 一小局刚打完：累加得分，必要时进入大局得分榜。r：房间。无返回值。
    void on_small_over(Room& r);

    // 把玩家坐进房间一个空位。r：房间。id：连接。s：会话。返回座位号，满员则 -1。
    int sit_down(Room& r, HttpWsServer::ConnId id, Session& s);

    // 创建房间并自动进入。id：连接。msg：含 name、match_count。无返回值。
    void create_room(HttpWsServer::ConnId id, const Json& msg);

    // 进入已有房间。id：连接。msg：含 room_id。无返回值。
    void enter_room(HttpWsServer::ConnId id, const Json& msg);

    // 三人在线且都准备则开始异步发牌（新的一小局）。r：房间。无返回值。
    void try_start(Room& r);

    // 在后台洗牌并问 AI，完成后回到网络线程套牌。
    // r：房间。redeal：true 表示流局重发，不增加小局计数。无返回值。
    void begin_async_deal(Room& r, bool redeal);

    // 后台发牌完成。rid：房间号。seq：这次发牌的序号。first：首叫座位。
    // pack：已评估并按得分分好的牌。redeal：是否流局重发。无返回值。
    void on_deal_ready(const std::string& rid, int seq, int first, DealtPack pack, bool redeal);

    // 检查有没有人太久没说话，并向在线连接发探活。无参数。无返回值。
    void heartbeat();

    // 当前阶段的短名字，给客户端用。r：房间。返回英文小写名。
    std::string phase_name(const Room& r) const;

    Config cfg_;
    DealEval deal_eval_;
    MysqlHandle& db_;
    HttpWsServer& net_;
    std::unordered_map<HttpWsServer::ConnId, Session> sessions_;
    std::unordered_map<std::string, std::unique_ptr<Room>> rooms_;
};
