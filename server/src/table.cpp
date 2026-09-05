#include "table.h"

#include "log_record.h"

namespace {
// 出牌非法时的固定提示。无参数。返回短句。
std::string illegal() {
    return "所选牌型不符合规则";
}
} // namespace

Table::Table(Config cfg, MysqlHandle& db, HttpWsServer& net) : cfg_(std::move(cfg)), db_(db), net_(net) {}

void Table::start() {
    heartbeat();
}

void Table::on_open(HttpWsServer::ConnId id) {
    conn_seen_[id] = std::chrono::steady_clock::now();
    log_info("ws open id=" + std::to_string(id));
}

int Table::seat_of_conn(HttpWsServer::ConnId id) const {
    for (int i = 0; i < 3; ++i) {
        if (at(i).occupied && at(i).conn_id == id) {
            return i;
        }
    }
    return -1;
}

int Table::seat_of_account(int account_id) const {
    for (int i = 0; i < 3; ++i) {
        if (at(i).occupied && at(i).account_id == account_id) {
            return i;
        }
    }
    return -1;
}

int Table::free_seat() const {
    for (int i = 0; i < 3; ++i) {
        if (!at(i).occupied) {
            return i;
        }
    }
    return -1;
}

void Table::attach(int seat, HttpWsServer::ConnId id) {
    auto& s = at(seat);
    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
        s.retry_timer = 0;
    }

    s.conn_id = id;
    s.online = true;
    s.retry_left = 0;
    s.last_seen = std::chrono::steady_clock::now();
}

void Table::vacate(int seat) {
    auto& s = at(seat);
    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
    }
    s = Seat{};
}

void Table::leave_seat(int seat, HttpWsServer::ConnId id) {
    log_info("leave seat=" + std::to_string(seat));
    const bool had_game = static_cast<bool>(game_);
    vacate(seat);
    if (had_game) {
        abort_game("有玩家离开房间");
    }

    Json j = Json::object();
    j.set("op", std::string("leave_ok"));
    net_.send(id, j.dump());
    broadcast_state("lobby");
}

void Table::begin_retry(int seat) {
    auto& s = at(seat);
    s.online = false;
    s.conn_id = 0;
    s.retry_left = cfg_.retry_max;

    if (s.retry_timer) {
        net_.cancel(s.retry_timer);
    }
    s.retry_timer = net_.defer(cfg_.retry_timeout_sec * 1000, [this, seat]() { on_retry(seat); });

    log_info("seat " + std::to_string(seat) + " disconnect, retry window start");
}

void Table::on_retry(int seat) {
    auto& s = at(seat);
    s.retry_timer = 0;
    if (!s.occupied || s.online) {
        return;
    }

    s.retry_left--;
    if (s.retry_left > 0) {
        s.retry_timer = net_.defer(cfg_.retry_timeout_sec * 1000, [this, seat]() { on_retry(seat); });
        log_info("seat " + std::to_string(seat) + " retry left=" + std::to_string(s.retry_left));
        return;
    }

    log_info("seat " + std::to_string(seat) + " retry failed");
    if (game_) {
        abort_game("连接已断开");
    }
    vacate(seat);
    broadcast_state("lobby");
}

void Table::abort_game(const std::string& reason) {
    game_.reset();
    for (auto& s : seats_) {
        s.ready = false;
        s.again = false;
    }

    for (int i = 0; i < 3; ++i) {
        if (at(i).occupied && at(i).online) {
            Json out = snapshot(i);
            out.set("op", std::string("abort"));
            out.set("reason", reason);
            net_.send(at(i).conn_id, out.dump());
        }
    }

    log_info("game abort: " + reason);
}

void Table::on_close(HttpWsServer::ConnId id) {
    conn_seen_.erase(id);
    int seat = seat_of_conn(id);
    if (seat < 0) {
        return;
    }

    log_info("ws close seat=" + std::to_string(seat));
    begin_retry(seat);
    broadcast_state("lobby");
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

std::string Table::phase_name() const {
    if (!game_) {
        return "lobby";
    }
    switch (game_->phase()) {
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

Json Table::snapshot(int viewer) const {
    Json j = Json::object();
    j.set("op", std::string("state"));
    j.set("you", viewer);
    j.set("phase", phase_name());
    j.set("public_mult", game_ ? (viewer >= 0 ? game_->seat_mult(viewer) : game_->display_mult())
                               : cfg_.base_score);
    j.set("score", game_ && viewer >= 0 ? game_->seat_score(viewer) : 0);
    j.set("actor", game_ ? game_->actor() : -1);
    j.set("landlord", game_ ? game_->landlord() : -1);
    j.set("winner_side", game_ ? game_->winner_side() : std::string());
    j.set("spring", game_ ? game_->spring() : std::string("none"));

    Json seats = Json::array();
    for (int i = 0; i < 3; ++i) {
        const auto& s = at(i);
        Json one = Json::object();
        one.set("occupied", s.occupied);
        one.set("online", s.online);
        one.set("ready", s.ready);
        one.set("again", s.again);
        one.set("username", s.username);

        int remain = 0;
        std::string role = "unknown";
        if (game_) {
            remain = game_->remain(i);
            if (game_->landlord() >= 0) {
                role = (i == game_->landlord()) ? "landlord" : "farmer";
            }
            one.set("doubled", game_->doubled(i));
        } else {
            one.set("doubled", false);
        }
        one.set("remain", remain);
        one.set("role", role);
        seats.push(one);
    }
    j.set("seats", seats);

    Json hand = Json::array();
    if (viewer >= 0 && game_) {
        for (auto c : game_->hand(viewer)) {
            hand.push(Json::integer(c));
        }
    }
    j.set("hand", hand);

    Json bottom = Json::array();
    if (game_ && game_->bottom_revealed()) {
        for (auto c : game_->bottom()) {
            bottom.push(Json::integer(c));
        }
    }
    j.set("bottom", bottom);

    if (game_ && game_->last_play()) {
        Json lp = Json::object();
        lp.set("seat", game_->last_play()->seat);
        lp.set("pattern", ddz::pattern_name(game_->last_play()->pattern));
        Json cards = Json::array();
        for (auto c : game_->last_play()->cards) {
            cards.push(Json::integer(c));
        }
        lp.set("cards", cards);
        j.set("last_play", lp);
    } else {
        j.set_null("last_play");
    }

    bool seated = viewer >= 0;
    bool my_turn = seated && game_ && game_->actor() == viewer;
    j.set("can_ready", seated && !game_ && !at(viewer).ready);
    j.set("can_unready", seated && !game_ && at(viewer).ready);
    j.set("can_call", my_turn && game_ && game_->phase() == Game::Phase::Call);
    j.set("can_rob", my_turn && game_ && game_->phase() == Game::Phase::Rob);
    j.set("can_double", seated && game_ && game_->phase() == Game::Phase::Double &&
                            !game_->double_chosen(viewer));
    j.set("can_play", my_turn && game_ && game_->phase() == Game::Phase::Play);
    j.set("can_pass", my_turn && game_ && game_->phase() == Game::Phase::Play && !game_->is_lead());
    j.set("can_again", seated && game_ && game_->phase() == Game::Phase::Settle && !at(viewer).again);
    return j;
}

void Table::send_state(int seat, const std::string& op) {
    if (seat < 0) {
        return;
    }
    auto& s = at(seat);
    if (!s.occupied || !s.online) {
        return;
    }

    Json j = snapshot(seat);
    j.set("op", op);
    net_.send(s.conn_id, j.dump());
}

void Table::broadcast_state(const std::string& op) {
    for (int i = 0; i < 3; ++i) {
        send_state(i, op);
    }
}

void Table::try_start() {
    for (const auto& s : seats_) {
        if (!s.occupied || !s.online || !s.ready) {
            return;
        }
    }

    int first = ddz::random_int(0, 2);
    game_ = std::make_unique<Game>(cfg_.base_score);
    game_->start_deal(first);
    for (auto& s : seats_) {
        s.again = false;
    }

    log_info("deal, first_caller=" + at(first).username + " seat=" + std::to_string(first));
    broadcast_state("deal");
}

void Table::try_again() {
    for (const auto& s : seats_) {
        if (!s.occupied || !s.again) {
            return;
        }
    }

    for (auto& s : seats_) {
        s.ready = true;
        s.again = false;
    }

    try_start();
}

void Table::on_message(HttpWsServer::ConnId id, const std::string& payload) {
    conn_seen_[id] = std::chrono::steady_clock::now();
    int seat = seat_of_conn(id);
    if (seat >= 0) {
        at(seat).last_seen = conn_seen_[id];
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

        int exist = seat_of_account(acc->id);
        if (exist >= 0) {
            auto& s = at(exist);
            if (s.online) {
                send_error(id, "该账号已在线");
                return;
            }
            attach(exist, id);
            log_info("reconnect " + acc->username + " seat=" + std::to_string(exist));
            send_state(exist, "login_ok");
            broadcast_state("lobby");
            return;
        }

        int seat = free_seat();
        if (seat < 0) {
            send_error(id, "房间已满");
            return;
        }
        auto& s = at(seat);
        s.occupied = true;
        s.account_id = acc->id;
        s.username = acc->username;
        s.ready = false;
        s.again = false;
        attach(seat, id);
        log_info("login " + acc->username + " seat=" + std::to_string(seat));
        send_state(seat, "login_ok");
        broadcast_state("lobby");
        return;
    }

    int seat = seat_of_conn(id);
    if (seat < 0) {
        send_error(id, "现在不能做该操作");
        return;
    }

    if (op == "leave") {
        leave_seat(seat, id);
        return;
    }

    std::string err;
    if (op == "ready") {
        if (game_) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(seat).ready = true;
        broadcast_state("lobby");
        try_start();
        return;
    }
    if (op == "unready") {
        if (game_) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(seat).ready = false;
        broadcast_state("lobby");
        return;
    }
    if (op == "again") {
        if (!game_ || game_->phase() != Game::Phase::Settle) {
            send_error(id, "现在不能做该操作");
            return;
        }
        at(seat).again = true;
        broadcast_state("settle");
        try_again();
        return;
    }

    if (!game_) {
        send_error(id, "现在不能做该操作");
        return;
    }

    if (op == "call") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, game_->on_call(seat, yes, err), err,
                         "call seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        if (game_->take_call_redeal()) {
            log_info("流局，重新发牌 first_caller=" + std::to_string(game_->first_caller()));
            broadcast_state("deal");
            return;
        }
        broadcast_state(phase_name());
        return;
    }
    if (op == "rob") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, game_->on_rob(seat, yes, err), err,
                         "rob seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        broadcast_state(phase_name());
        return;
    }
    if (op == "double") {
        const bool yes = msg.get_bool("yes");
        if (!accept_move(id, game_->on_double(seat, yes, err), err,
                         "double seat=" + std::to_string(seat) + " yes=" + (yes ? "1" : "0"))) {
            return;
        }
        broadcast_state(phase_name());
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
        if (!accept_move(id, game_->on_play(seat, cards, msg.get_str("pattern"), err), err,
                         "play seat=" + std::to_string(seat) + " " + ddz::cards_debug(cards))) {
            return;
        }
        broadcast_state(phase_name());
        return;
    }
    if (op == "pass") {
        if (!accept_move(id, game_->on_pass(seat, err), err, "pass seat=" + std::to_string(seat))) {
            return;
        }
        broadcast_state(phase_name());
        return;
    }

    send_error(id, "现在不能做该操作");
}

void Table::heartbeat() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(cfg_.heartbeat_timeout_sec);
    for (int i = 0; i < 3; ++i) {
        auto& s = at(i);
        if (!s.occupied || !s.online) {
            continue;
        }
        if (now - s.last_seen > timeout) {
            log_warn("heartbeat timeout seat=" + std::to_string(i));
            auto cid = s.conn_id;
            begin_retry(i);
            net_.close(cid);
            broadcast_state(phase_name());
            continue;
        }
        Json ping = Json::object();
        ping.set("op", std::string("ping"));
        net_.send(s.conn_id, ping.dump());
    }

    net_.defer(cfg_.heartbeat_interval_sec * 1000, [this]() { heartbeat(); });
}
