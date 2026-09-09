#include "table.h"

#include "log_record.h"

#include <algorithm>
#include <cctype>
#include <vector>
#include <utility>

namespace {
// 出牌非法时的固定提示。无参数。返回短句。
std::string illegal() {
    return "所选牌型不符合规则";
}

// 去掉首尾空白。s：原串。返回修剪后的副本。
std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// 房间名长度：ASCII 计 1，其余 Unicode 码点计 2。s：UTF-8。非法 UTF-8 返回 -1。
int name_weight(const std::string& s) {
    int n = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const auto* end = p + s.size();
    while (p < end) {
        if (*p < 0x80) {
            n += 1;
            p += 1;
        } else if ((*p & 0xE0) == 0xC0 && p + 1 < end && (p[1] & 0xC0) == 0x80) {
            n += 2;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p + 2 < end && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80) {
            n += 2;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p + 3 < end && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            n += 2;
            p += 4;
        } else {
            return -1;
        }
    }
    return n;
}
} // namespace

Table::Table(Config cfg, MysqlHandle& db, HttpWsServer& net) : cfg_(std::move(cfg)), db_(db), net_(net) {}

void Table::start() {
    heartbeat();
}

void Table::on_open(HttpWsServer::ConnId id) {
    log_info("ws open id=" + std::to_string(id));
}

int Table::occupied_count(const Room& r) const {
    int n = 0;
    for (const auto& s : r.seats) {
        if (s.occupied) {
            n++;
        }
    }
    return n;
}

Table::Located Table::locate_conn(HttpWsServer::ConnId id) {
    for (auto& [rid, rp] : rooms_) {
        int seat = seat_of_conn(*rp, id);
        if (seat >= 0) {
            return Located{rp.get(), seat};
        }
    }
    return Located{};
}

Table::Located Table::locate_account(int account_id) {
    for (auto& [rid, rp] : rooms_) {
        for (int i = 0; i < 3; ++i) {
            if (at(*rp, i).occupied && at(*rp, i).account_id == account_id) {
                return Located{rp.get(), i};
            }
        }
    }
    return Located{};
}

Table::Room* Table::find_room(const std::string& id) {
    auto it = rooms_.find(id);
    if (it == rooms_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::string Table::alloc_room_id() {
    for (int t = 0; t < 64; ++t) {
        int n = ddz::random_int(10000000, 99999999);
        std::string id = std::to_string(n);
        if (!rooms_.count(id)) {
            return id;
        }
    }
    return {};
}

bool Table::name_taken(const std::string& name) const {
    for (const auto& [id, rp] : rooms_) {
        if (rp->name == name) {
            return true;
        }
    }
    return false;
}

int Table::seat_of_conn(const Room& r, HttpWsServer::ConnId id) const {
    for (int i = 0; i < 3; ++i) {
        if (at(r, i).occupied && at(r, i).conn_id == id) {
            return i;
        }
    }
    return -1;
}

int Table::free_seat(const Room& r) const {
    for (int i = 0; i < 3; ++i) {
        if (!at(r, i).occupied) {
            return i;
        }
    }
    return -1;
}

void Table::attach(Room& r, int seat, HttpWsServer::ConnId id) {
    auto& s = at(r, seat);
    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
        s.retry_timer = 0;
    }

    s.conn_id = id;
    s.online = true;
    s.retry_left = 0;
    s.last_seen = std::chrono::steady_clock::now();

    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.room_id = r.id;
        it->second.last_seen = s.last_seen;
    }
}

void Table::vacate(Room& r, int seat) {
    auto& s = at(r, seat);
    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
    }
    s = Seat{};
}

void Table::maybe_dissolve(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return;
    }
    if (!room_empty(*it->second)) {
        return;
    }

    log_info("dissolve room " + room_id);
    rooms_.erase(it);
    broadcast_hall();
}

void Table::begin_retry(Room& r, int seat) {
    auto& s = at(r, seat);
    s.online = false;
    s.conn_id = 0;
    s.retry_left = cfg_.retry_max;

    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
    }
    const std::string rid = r.id;
    s.retry_timer = net_.defer(cfg_.retry_timeout_sec * 1000, [this, rid, seat]() { on_retry(rid, seat); });

    log_info("room " + r.id + " seat " + std::to_string(seat) + " disconnect, retry window start");
}

void Table::on_retry(const std::string& room_id, int seat) {
    Room* r = find_room(room_id);
    if (!r) {
        return;
    }

    auto& s = at(*r, seat);
    s.retry_timer = 0;
    if (!s.occupied || s.online) {
        return;
    }

    s.retry_left--;
    if (s.retry_left > 0) {
        s.retry_timer = net_.defer(cfg_.retry_timeout_sec * 1000, [this, room_id, seat]() { on_retry(room_id, seat); });
        log_info("room " + room_id + " seat " + std::to_string(seat) +
                 " retry left=" + std::to_string(s.retry_left));
        return;
    }

    log_info("room " + room_id + " seat " + std::to_string(seat) + " retry failed");
    if (match_in_progress(*r)) {
        abort_game(*r, "连接已断开");
    }
    vacate(*r, seat);
    if (room_empty(*r)) {
        maybe_dissolve(room_id);
        return;
    }
    broadcast_state(*r, phase_name(*r));
    broadcast_hall();
}

void Table::abort_game(Room& r, const std::string& reason) {
    r.game.reset();
    r.match_done = 0;
    r.set_score = {};
    r.last_score = {};
    r.set_over = false;
    r.last_spring = "none";
    for (auto& s : r.seats) {
        s.ready = false;
        s.board_closed = false;
    }

    for (int i = 0; i < 3; ++i) {
        if (at(r, i).occupied && at(r, i).online) {
            Json out = snapshot(r, i);
            out.set("op", std::string("abort"));
            out.set("reason", reason);
            net_.send(at(r, i).conn_id, out.dump());
        }
    }

    log_info("room " + r.id + " game abort: " + reason);
}

void Table::on_small_over(Room& r) {
    if (!r.game || r.game->phase() != Game::Phase::Settle) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        int sc = r.game->seat_score(i);
        r.last_score[static_cast<std::size_t>(i)] = sc;
        r.set_score[static_cast<std::size_t>(i)] += sc;
    }
    r.last_spring = r.game->spring();

    for (auto& s : r.seats) {
        s.ready = false;
        s.board_closed = false;
    }

    if (r.match_done >= r.match_total) {
        r.set_over = true;
    }

    r.game.reset();
}

bool Table::match_in_progress(const Room& r) const {
    return static_cast<bool>(r.game) || r.match_done > 0 || r.set_over;
}

void Table::leave_room(Room& r, int seat, HttpWsServer::ConnId id) {
    log_info("leave room " + r.id + " seat=" + std::to_string(seat));
    const std::string rid = r.id;
    const bool in_match = match_in_progress(r);
    vacate(r, seat);

    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second.room_id.clear();
    }

    if (in_match) {
        abort_game(r, "有玩家离开房间");
    }

    if (room_empty(r)) {
        maybe_dissolve(rid);
    } else {
        broadcast_state(r, phase_name(r));
        broadcast_hall();
    }

    send_hall(id);
}

void Table::on_close(HttpWsServer::ConnId id) {
    Located loc = locate_conn(id);
    sessions_.erase(id);

    if (!loc.room || loc.seat < 0) {
        return;
    }

    log_info("ws close room " + loc.room->id + " seat=" + std::to_string(loc.seat));
    begin_retry(*loc.room, loc.seat);
    broadcast_state(*loc.room, phase_name(*loc.room));
    broadcast_hall();
}

void Table::send_error(HttpWsServer::ConnId id, const std::string& msg) {
    Json j = Json::object();
    j.set("op", std::string("error"));
    j.set("msg", msg);
    net_.send(id, j.dump());
}

bool Table::accept_move(HttpWsServer::ConnId id, bool ok, const std::string& err, const std::string& logline) {
    if (!ok) {
        send_error(id, err);
        return false;
    }
    log_info(logline);
    return true;
}

std::string Table::phase_name(const Room& r) const {
    if (r.set_over) {
        return "set_over";
    }
    if (!r.game) {
        return "lobby";
    }
    switch (r.game->phase()) {
    case Game::Phase::Call:
        return "call";
    case Game::Phase::Rob:
        return "rob";
    case Game::Phase::Double:
        return "double";
    case Game::Phase::Play:
        return "play";
    case Game::Phase::Settle:
        return "settle";
    }
    return "lobby";
}

Json Table::hall_snapshot(const Session& s) const {
    Json j = Json::object();
    j.set("op", std::string("hall"));
    j.set("phase", std::string("hall"));
    j.set("you", -1);
    j.set("username", s.username);

    std::vector<const Room*> list;
    list.reserve(rooms_.size());
    for (const auto& [id, rp] : rooms_) {
        list.push_back(rp.get());
    }
    std::sort(list.begin(), list.end(), [](const Room* a, const Room* b) { return a->id < b->id; });

    Json rooms = Json::array();
    for (const Room* rp : list) {
        Json one = Json::object();
        one.set("id", rp->id);
        one.set("name", rp->name);
        one.set("n", occupied_count(*rp));
        one.set("match_total", rp->match_total);
        one.set("busy", match_in_progress(*rp));
        rooms.push(one);
    }
    j.set("rooms", rooms);
    return j;
}

void Table::send_hall(HttpWsServer::ConnId id) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        return;
    }
    net_.send(id, hall_snapshot(it->second).dump());
}

void Table::broadcast_hall() {
    for (auto& [id, s] : sessions_) {
        if (s.room_id.empty()) {
            net_.send(id, hall_snapshot(s).dump());
        }
    }
}

Json Table::snapshot(const Room& r, int viewer) const {
    Json j = Json::object();
    j.set("op", std::string("state"));
    j.set("you", viewer);
    j.set("phase", phase_name(r));
    j.set("room_id", r.id);
    j.set("room_name", r.name);
    j.set("match_total", r.match_total);
    j.set("match_done", r.match_done);
    j.set("set_over", r.set_over);
    j.set("public_mult", r.game ? (viewer >= 0 ? r.game->seat_mult(viewer) : r.game->display_mult())
                                : cfg_.base_score);
    int score = 0;
    if (viewer >= 0) {
        score = r.game ? r.game->seat_score(viewer) : r.last_score[static_cast<std::size_t>(viewer)];
    }
    j.set("score", score);
    j.set("actor", r.game ? r.game->actor() : -1);
    j.set("landlord", r.game ? r.game->landlord() : -1);
    j.set("winner_side", r.game ? r.game->winner_side() : std::string());
    j.set("spring", r.game ? r.game->spring() : r.last_spring);

    Json seats = Json::array();
    for (int i = 0; i < 3; ++i) {
        const auto& s = at(r, i);
        Json one = Json::object();
        one.set("occupied", s.occupied);
        one.set("online", s.online);
        one.set("ready", s.ready);
        one.set("username", s.username);
        one.set("set_score", r.set_score[static_cast<std::size_t>(i)]);
        one.set("board_closed", s.board_closed);

        int remain = 0;
        std::string role = "unknown";
        if (r.game) {
            remain = r.game->remain(i);
            if (r.game->landlord() >= 0) {
                role = (i == r.game->landlord()) ? "landlord" : "farmer";
            }
            one.set("doubled", r.game->doubled(i));
        } else {
            one.set("doubled", false);
        }
        one.set("remain", remain);
        one.set("role", role);
        seats.push(one);
    }
    j.set("seats", seats);

    Json hand = Json::array();
    if (viewer >= 0 && r.game) {
        for (auto c : r.game->hand(viewer)) {
            hand.push(Json::integer(c));
        }
    }
    j.set("hand", hand);

    Json bottom = Json::array();
    if (r.game && r.game->bottom_revealed()) {
        for (auto c : r.game->bottom()) {
            bottom.push(Json::integer(c));
        }
    }
    j.set("bottom", bottom);

    if (r.game && r.game->last_play()) {
        Json lp = Json::object();
        lp.set("seat", r.game->last_play()->seat);
        lp.set("pattern", ddz::pattern_name(r.game->last_play()->pattern));
        Json cards = Json::array();
        for (auto c : r.game->last_play()->cards) {
            cards.push(Json::integer(c));
        }
        lp.set("cards", cards);
        j.set("last_play", lp);
    } else {
        j.set_null("last_play");
    }

    bool seated = viewer >= 0;
    bool my_turn = seated && r.game && r.game->actor() == viewer;
    bool closed = seated && at(r, viewer).board_closed;
    j.set("can_ready", seated && !r.game && !at(r, viewer).ready && (!r.set_over || closed));
    j.set("can_unready", seated && !r.game && at(r, viewer).ready && (!r.set_over || closed));
    j.set("can_call", my_turn && r.game && r.game->phase() == Game::Phase::Call);
    j.set("can_rob", my_turn && r.game && r.game->phase() == Game::Phase::Rob);
    j.set("can_double", seated && r.game && r.game->phase() == Game::Phase::Double &&
                            !r.game->double_chosen(viewer));
    j.set("can_play", my_turn && r.game && r.game->phase() == Game::Phase::Play);
    j.set("can_pass", my_turn && r.game && r.game->phase() == Game::Phase::Play && !r.game->is_lead());
    j.set("can_close_board", seated && r.set_over && !closed);
    return j;
}

void Table::send_state(Room& r, int seat, const std::string& op) {
    if (seat < 0) {
        return;
    }
    auto& s = at(r, seat);
    if (!s.occupied || !s.online) {
        return;
    }

    Json j = snapshot(r, seat);
    j.set("op", op);
    net_.send(s.conn_id, j.dump());
}

void Table::broadcast_state(Room& r, const std::string& op) {
    for (int i = 0; i < 3; ++i) {
        send_state(r, i, op);
    }
}

int Table::sit_down(Room& r, HttpWsServer::ConnId id, Session& s) {
    int seat = free_seat(r);
    if (seat < 0) {
        return -1;
    }

    auto& st = at(r, seat);
    st.occupied = true;
    st.account_id = s.account_id;
    st.username = s.username;
    st.ready = false;
    st.board_closed = r.set_over;
    attach(r, seat, id);
    return seat;
}

void Table::try_start(Room& r) {
    for (const auto& s : r.seats) {
        if (!s.occupied || !s.online || !s.ready) {
            return;
        }
        if (r.set_over && !s.board_closed) {
            return;
        }
    }

    if (r.set_over) {
        r.set_over = false;
        r.set_score = {};
        r.last_score = {};
        r.match_done = 0;
        r.last_spring = "none";
        for (auto& s : r.seats) {
            s.board_closed = false;
        }
    }

    r.match_done++;
    int first = ddz::random_int(0, 2);
    r.game = std::make_unique<Game>(cfg_.base_score);
    r.game->start_deal(first);
    r.last_spring = "none";
    r.last_score = {};

    log_info("room " + r.id + " deal " + std::to_string(r.match_done) + "/" +
             std::to_string(r.match_total) + " first_caller=" + at(r, first).username);
    broadcast_state(r, "deal");
    broadcast_hall();
}

void Table::create_room(HttpWsServer::ConnId id, const Json& msg) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        send_error(id, "现在不能做该操作");
        return;
    }
    Session& s = it->second;
    if (!s.room_id.empty()) {
        send_error(id, "已在房间中");
        return;
    }

    std::string name = trim_copy(msg.get_str("name"));
    int match_count = msg.get_int("match_count", 0);
    int nw = name_weight(name);
    if (name.empty() || nw < 0 || nw > 60) {
        send_error(id, "房间名不合法");
        return;
    }
    if (match_count < 1 || match_count > 99) {
        send_error(id, "局数不合法");
        return;
    }
    if (name_taken(name)) {
        send_error(id, "房间名已被使用");
        return;
    }

    std::string rid = alloc_room_id();
    if (rid.empty()) {
        send_error(id, "现在不能做该操作");
        return;
    }

    auto rp = std::make_unique<Room>();
    rp->id = rid;
    rp->name = name;
    rp->match_total = match_count;
    rooms_[rid] = std::move(rp);
    Room& r = *rooms_[rid];

    int seat = sit_down(r, id, s);
    log_info("create room " + rid + " name=" + name + " matches=" + std::to_string(match_count) +
             " by " + s.username + " seat=" + std::to_string(seat));
    send_state(r, seat, "enter_ok");
    broadcast_hall();
}

void Table::enter_room(HttpWsServer::ConnId id, const Json& msg) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        send_error(id, "现在不能做该操作");
        return;
    }
    Session& s = it->second;
    if (!s.room_id.empty()) {
        send_error(id, "已在房间中");
        return;
    }

    std::string rid = trim_copy(msg.get_str("room_id"));
    Room* r = find_room(rid);
    if (!r) {
        send_error(id, "房间不存在");
        return;
    }
    if (free_seat(*r) < 0) {
        send_error(id, "房间已满");
        return;
    }

    int seat = sit_down(*r, id, s);
    log_info("enter room " + rid + " " + s.username + " seat=" + std::to_string(seat));
    send_state(*r, seat, "enter_ok");
    broadcast_state(*r, phase_name(*r));
    broadcast_hall();
}

void Table::on_message(HttpWsServer::ConnId id, const std::string& payload) {
    auto sit = sessions_.find(id);
    if (sit != sessions_.end()) {
        sit->second.last_seen = std::chrono::steady_clock::now();
    }
    Located loc = locate_conn(id);
    if (loc.room && loc.seat >= 0) {
        at(*loc.room, loc.seat).last_seen = std::chrono::steady_clock::now();
    }

    Json msg = Json::parse(payload);
    if (!msg.ok() || !msg.contains("op")) {
        send_error(id, "现在不能做该操作");
        return;
    }

    handle(id, msg);
}

void Table::handle(HttpWsServer::ConnId id, const Json& msg) {
    const std::string op = msg.get_str("op");
    if (op == "pong") {
        return;
    }

    if (op == "login") {
        auto acc = db_.login(msg.get_str("username"), msg.get_str("password"));
        if (!acc) {
            send_error(id, "用户名或密码错误");
            return;
        }

        for (const auto& [cid, s] : sessions_) {
            if (cid != id && s.account_id == acc->id) {
                send_error(id, "该账号已在线");
                return;
            }
        }

        Located exist = locate_account(acc->id);
        Session sess;
        sess.account_id = acc->id;
        sess.username = acc->username;
        sess.last_seen = std::chrono::steady_clock::now();
        sessions_[id] = sess;

        if (exist.room && exist.seat >= 0) {
            auto& st = at(*exist.room, exist.seat);
            if (st.online) {
                sessions_.erase(id);
                send_error(id, "该账号已在线");
                return;
            }
            sessions_[id].room_id = exist.room->id;
            attach(*exist.room, exist.seat, id);
            log_info("reconnect " + acc->username + " room " + exist.room->id +
                     " seat=" + std::to_string(exist.seat));
            send_state(*exist.room, exist.seat, "login_ok");
            broadcast_state(*exist.room, phase_name(*exist.room));
            broadcast_hall();
            return;
        }

        log_info("login " + acc->username + " hall");
        send_hall(id);
        return;
    }

    auto sit = sessions_.find(id);
    if (sit == sessions_.end()) {
        send_error(id, "现在不能做该操作");
        return;
    }

    if (op == "logout") {
        Located loc = locate_conn(id);
        sessions_.erase(id);
        if (loc.room && loc.seat >= 0) {
            leave_room(*loc.room, loc.seat, id);
        }
        Json j = Json::object();
        j.set("op", std::string("leave_ok"));
        net_.send(id, j.dump());
        return;
    }

    if (op == "create_room") {
        create_room(id, msg);
        return;
    }
    if (op == "enter_room") {
        enter_room(id, msg);
        return;
    }

    Located loc = locate_conn(id);
    if (!loc.room || loc.seat < 0) {
        send_error(id, "现在不能做该操作");
        return;
    }
    Room& r = *loc.room;
    int seat = loc.seat;

    if (op == "leave" || op == "leave_room") {
        leave_room(r, seat, id);
        return;
    }

    std::string err;
    if (op == "close_board") {
        if (!r.set_over || at(r, seat).board_closed) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(r, seat).board_closed = true;
        broadcast_state(r, "set_over");
        return;
    }
    if (op == "ready") {
        if (r.game) {
            send_error(id, "现在不能做该操作");
            return;
        }
        if (r.set_over && !at(r, seat).board_closed) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(r, seat).ready = true;
        broadcast_state(r, phase_name(r));
        try_start(r);
        return;
    }
    if (op == "unready") {
        if (r.game) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(r, seat).ready = false;
        broadcast_state(r, phase_name(r));
        return;
    }

    if (!r.game) {
        send_error(id, "现在不能做该操作");
        return;
    }

    if (op == "call") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, r.game->on_call(seat, yes, err), err,
                         "call seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        if (r.game->take_call_redeal()) {
            log_info("流局，重新发牌 first_caller=" + std::to_string(r.game->first_caller()));
            broadcast_state(r, "deal");
            return;
        }
        broadcast_state(r, phase_name(r));
        return;
    }
    if (op == "rob") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, r.game->on_rob(seat, yes, err), err,
                         "rob seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        broadcast_state(r, phase_name(r));
        return;
    }
    if (op == "double") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, r.game->on_double(seat, yes, err), err,
                         "double seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        broadcast_state(r, phase_name(r));
        return;
    }
    if (op == "play") {
        std::vector<ddz::Card> cards;
        for (int x : msg.get_ints("cards")) {
            if (x < 0 || x > 53) {
                send_error(id, illegal());
                return;
            }
            cards.push_back(static_cast<ddz::Card>(x));
        }
        if (!accept_move(id, r.game->on_play(seat, cards, msg.get_str("pattern"), err), err,
                         "play seat=" + std::to_string(seat) + " " + ddz::cards_debug(cards))) {
            return;
        }
        on_small_over(r);
        broadcast_state(r, phase_name(r));
        return;
    }
    if (op == "pass") {
        if (!accept_move(id, r.game->on_pass(seat, err), err, "pass seat=" + std::to_string(seat))) {
            return;
        }
        broadcast_state(r, phase_name(r));
        return;
    }

    send_error(id, "现在不能做该操作");
}

void Table::heartbeat() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(cfg_.heartbeat_timeout_sec);

    std::vector<HttpWsServer::ConnId> ids;
    ids.reserve(sessions_.size());
    for (const auto& [cid, s] : sessions_) {
        ids.push_back(cid);
    }

    for (HttpWsServer::ConnId cid : ids) {
        auto it = sessions_.find(cid);
        if (it == sessions_.end()) {
            continue;
        }
        Session& s = it->second;
        if (now - s.last_seen > timeout) {
            log_warn("heartbeat timeout conn=" + std::to_string(cid));
            Located loc = locate_conn(cid);
            if (loc.room && loc.seat >= 0) {
                begin_retry(*loc.room, loc.seat);
                broadcast_state(*loc.room, phase_name(*loc.room));
            }
            net_.close(cid);
            continue;
        }

        Json ping = Json::object();
        ping.set("op", std::string("ping"));
        net_.send(cid, ping.dump());
    }

    net_.defer(cfg_.heartbeat_interval_sec * 1000, [this]() { heartbeat(); });
}
