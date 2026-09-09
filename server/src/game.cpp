#include "game.h"

#include <algorithm>

namespace {
constexpr const char* kIllegal = "所选牌型不符合规则";
constexpr const char* kCannot = "现在不能做该操作";
} // namespace

Game::Game(int base_score) : base_score_(base_score < 1 ? 1 : base_score) {}

void Game::start_deal(int first_caller, const std::array<std::vector<ddz::Card>, 3>& hands,
                     const std::vector<ddz::Card>& bottom) {
    first_caller_ = first_caller;
    caller_ = -1;
    landlord_ = -1;
    actor_ = first_caller_;
    called_no_ = {};
    rob_done_ = {};
    anyone_robbed_ = false;
    rob_count_ = 0;
    public_mult_ = base_score_;
    double_chosen_ = {};
    double_yes_ = {};
    last_play_.reset();
    consecutive_pass_ = 0;
    play_count_ = {};
    bomb_rocket_count_ = 0;
    winner_side_.clear();
    spring_ = "none";
    phase_ = Phase::Call;
    call_redeal_ = false;
    hands_ = hands;
    bottom_ = bottom;
    bottom_revealed_ = false;
}

int Game::next_rob_actor(int from) const {
    for (int i = 0; i < 3; ++i) {
        int s = (from + i) % 3;
        if (rob_done_[static_cast<std::size_t>(s)]) {
            continue;
        }
        if (called_no_[static_cast<std::size_t>(s)]) {
            continue;
        }
        // 还没人抢过时，叫地主的人先不问；有人抢了才轮到他抢回。
        if (s == caller_ && !anyone_robbed_) {
            continue;
        }
        return s;
    }
    return -1;
}

void Game::begin_rob() {
    phase_ = Phase::Rob;
    rob_done_ = {};
    anyone_robbed_ = false;
    landlord_ = caller_;

    actor_ = next_rob_actor(next_seat(caller_));
    if (actor_ < 0) {
        reveal();
    }
}

void Game::reveal() {
    landlord_ = (landlord_ >= 0) ? landlord_ : caller_;

    for (auto c : bottom_) {
        hands_[static_cast<std::size_t>(landlord_)].push_back(c);
    }
    ddz::sort_hand(hands_[static_cast<std::size_t>(landlord_)]);
    bottom_revealed_ = true;

    phase_ = Phase::Double;
    actor_ = -1;
    double_chosen_ = {};
    double_yes_ = {};
}

void Game::begin_play() {
    phase_ = Phase::Play;
    actor_ = landlord_;
    last_play_.reset();
    consecutive_pass_ = 0;
}

int Game::display_mult() const {
    int m = public_mult_;
    if (spring_ != "none") {
        m *= 2;
    }
    return m;
}

int Game::seat_mult(int seat) const {
    int m = display_mult();
    if (landlord_ < 0) {
        return m;
    }

    int L = doubled(landlord_) ? 2 : 1;
    auto farmer_u = [&](int farmer) {
        int F = doubled(farmer) ? 2 : 1;
        return m * L * F;
    };

    if (seat == landlord_) {
        int f1 = next_seat(landlord_);
        int f2 = next_seat(f1);
        return farmer_u(f1) + farmer_u(f2);
    }
    return farmer_u(seat);
}

int Game::seat_score(int seat) const {
    if (phase_ != Phase::Settle || winner_side_.empty()) {
        return 0;
    }

    int u = seat_mult(seat);
    bool landlord_win = (winner_side_ == "landlord");
    bool win = (seat == landlord_) ? landlord_win : !landlord_win;
    return win ? u : -u;
}

bool Game::take_call_redeal() {
    bool v = call_redeal_;
    call_redeal_ = false;
    return v;
}

bool Game::on_call(int seat, bool yes, std::string& err) {
    call_redeal_ = false;
    if (phase_ != Phase::Call || seat != actor_) {
        err = kCannot;
        return false;
    }

    if (yes) {
        caller_ = seat;
        begin_rob();
        return true;
    }

    called_no_[static_cast<std::size_t>(seat)] = true;
    int n = next_seat(seat);
    // 转回首叫座位，说明三人都过了，流局重发。
    if (n == first_caller_) {
        call_redeal_ = true;
        return true;
    }
    actor_ = n;
    return true;
}

bool Game::on_rob(int seat, bool yes, std::string& err) {
    if (phase_ != Phase::Rob || seat != actor_) {
        err = kCannot;
        return false;
    }

    rob_done_[static_cast<std::size_t>(seat)] = true;
    if (yes) {
        anyone_robbed_ = true;
        rob_count_++;
        landlord_ = seat;
        public_mult_ *= 2;
    }

    actor_ = next_rob_actor(next_seat(seat));
    if (actor_ < 0) {
        reveal();
    }
    return true;
}

bool Game::on_double(int seat, bool yes, std::string& err) {
    if (phase_ != Phase::Double) {
        err = kCannot;
        return false;
    }
    if (double_chosen_[static_cast<std::size_t>(seat)]) {
        err = kCannot;
        return false;
    }

    double_chosen_[static_cast<std::size_t>(seat)] = true;
    double_yes_[static_cast<std::size_t>(seat)] = yes;

    if (double_chosen_[0] && double_chosen_[1] && double_chosen_[2]) {
        begin_play();
    }
    return true;
}

std::optional<ddz::PatternHit> Game::resolve_play(const std::vector<ddz::Card>& cards,
                                                 const std::string& pattern) const {
    if (!pattern.empty()) {
        auto p = ddz::pattern_from_name(pattern);
        if (!p) {
            return std::nullopt;
        }
        return ddz::hit_as(cards, *p);
    }

    return ddz::unique_hit(cards);
}

bool Game::on_play(int seat, const std::vector<ddz::Card>& cards, const std::string& pattern, std::string& err) {
    if (phase_ != Phase::Play || seat != actor_) {
        err = kCannot;
        return false;
    }
    if (cards.empty()) {
        err = kIllegal;
        return false;
    }

    auto hit = resolve_play(cards, pattern);
    if (!hit) {
        err = kIllegal;
        return false;
    }
    if (last_play_) {
        if (!ddz::beats({last_play_->pattern, last_play_->key}, *hit, last_play_->cards.size(), cards.size())) {
            err = kIllegal;
            return false;
        }
    }

    auto& hand = hands_[static_cast<std::size_t>(seat)];
    auto copy = hand;
    if (!ddz::remove_cards(copy, cards)) {
        err = kIllegal;
        return false;
    }
    hand.swap(copy);

    if (hit->pattern == ddz::Pattern::Bomb || hit->pattern == ddz::Pattern::Rocket) {
        bomb_rocket_count_++;
        public_mult_ *= 2;
    }
    play_count_[static_cast<std::size_t>(seat)]++;
    last_play_ = PlayOnTable{seat, hit->pattern, hit->key, cards};
    consecutive_pass_ = 0;

    if (hand.empty()) {
        settle(seat);
        return true;
    }
    actor_ = next_seat(seat);
    return true;
}

bool Game::on_pass(int seat, std::string& err) {
    if (phase_ != Phase::Play || seat != actor_) {
        err = kCannot;
        return false;
    }
    if (!last_play_) {
        err = kCannot;
        return false;
    }

    consecutive_pass_++;
    if (consecutive_pass_ >= 2) {
        // 另外两家都过了，这一圈结束，由上次出牌的人继续引牌。
        actor_ = last_play_->seat;
        last_play_.reset();
        consecutive_pass_ = 0;
        return true;
    }

    actor_ = next_seat(seat);
    return true;
}

void Game::settle(int winner_seat) {
    phase_ = Phase::Settle;
    actor_ = -1;

    bool landlord_win = (winner_seat == landlord_);
    winner_side_ = landlord_win ? "landlord" : "farmer";

    int f1 = next_seat(landlord_);
    int f2 = next_seat(f1);
    // 农民一手没出过是春天；地主只出过一手是反春。
    if (landlord_win && play_count_[static_cast<std::size_t>(f1)] == 0 &&
        play_count_[static_cast<std::size_t>(f2)] == 0) {
        spring_ = "spring";
    } else if (!landlord_win && play_count_[static_cast<std::size_t>(landlord_)] == 1) {
        spring_ = "anti";
    } else {
        spring_ = "none";
    }
}
