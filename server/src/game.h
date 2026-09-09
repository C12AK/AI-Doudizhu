#pragma once

#include "cards.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// 一局牌：叫地主、抢地主、加倍、出牌、结算。不处理网络。
class Game {
public:
    enum class Phase { Call, Rob, Double, Play, Settle };

    // 场上最后一手牌，用来跟牌比较。
    struct PlayOnTable {
        int seat = -1;
        ddz::Pattern pattern{};
        int key = 0;
        std::vector<ddz::Card> cards;
    };

    // 记下房间底分。base_score：小于 1 时按 1 用。
    explicit Game(int base_score);

    // 套上已洗好的牌，进入叫地主。
    // first_caller：首叫座位号 0～2。hands：三座各 17 张。bottom：3 张底牌。
    // 无返回值。
    void start_deal(int first_caller, const std::array<std::vector<ddz::Card>, 3>& hands,
                    const std::vector<ddz::Card>& bottom);

    // 处理叫地主或不叫。
    // seat：座位号。yes：true 为叫。err：失败时写入给玩家看的短句。
    // 返回：接受这次操作为 true。
    bool on_call(int seat, bool yes, std::string& err);

    // 处理抢或不抢。参数、返回含义同 on_call。
    bool on_rob(int seat, bool yes, std::string& err);

    // 处理加倍或不加倍。三人各自点一次。参数、返回含义同 on_call。
    bool on_double(int seat, bool yes, std::string& err);

    // 处理出牌。
    // seat：座位号。cards：打出的牌编号。pattern：可选的牌型名，空则需能唯一判定。
    // err：失败时写入短句。
    // 返回：接受这次出牌为 true。
    bool on_play(int seat, const std::vector<ddz::Card>& cards, const std::string& pattern, std::string& err);

    // 处理不出。seat：座位号。err：失败时写入短句。返回：接受为 true。
    bool on_pass(int seat, std::string& err);

    // 当前阶段。无参数。
    Phase phase() const { return phase_; }

    // 当前该谁行动；加倍阶段为 -1。无参数。返回座位号。
    int actor() const { return actor_; }

    // 地主座位；尚未确定为 -1。无参数。
    int landlord() const { return landlord_; }

    // 尚未计入春天时的公共倍数。无参数。
    int public_mult() const { return public_mult_; }

    // 给界面看的公共倍数（春天或反春再乘 2）。无参数。
    int display_mult() const;

    // 该座在自己界面上看到的倍数。
    // 已定地主后：农民为公共倍数乘上地主与该农民的加倍；地主为对两名农民的倍数之和。
    // 未定地主时等于公共倍数。春天或反春已定时，公共部分已乘过 2。
    // seat：座位号。返回倍数。
    int seat_mult(int seat) const;

    // 该座本局得分：等于自己界面上的倍数，输了则为相反数。未到结算为 0。
    // seat：座位号。返回得分。
    int seat_score(int seat) const;

    // 某座的手牌。seat：座位号。返回只读引用。
    const std::vector<ddz::Card>& hand(int seat) const { return hands_[static_cast<std::size_t>(seat)]; }

    // 三张底牌。未亮出时仍可在内部读到。无参数。
    const std::vector<ddz::Card>& bottom() const { return bottom_; }

    // 底牌是否已亮给三家看。无参数。返回是/否。
    bool bottom_revealed() const { return bottom_revealed_; }

    // 该座是否选择了加倍。seat：座位号。返回是/否。
    bool doubled(int seat) const { return double_yes_[static_cast<std::size_t>(seat)]; }

    // 该座是否已经点过加倍或不加倍。seat：座位号。返回是/否。
    bool double_chosen(int seat) const { return double_chosen_[static_cast<std::size_t>(seat)]; }

    // 场上最后一手；新一圈开始时为空。无参数。
    const std::optional<PlayOnTable>& last_play() const { return last_play_; }

    // 某座还剩几张。seat：座位号。返回张数。
    int remain(int seat) const { return static_cast<int>(hands_[static_cast<std::size_t>(seat)].size()); }

    // 胜方。无参数。返回 landlord、farmer，或未结束时的空串。
    std::string winner_side() const { return winner_side_; }

    // 春天标记。无参数。返回 spring、anti 或 none。
    std::string spring() const { return spring_; }

    // 是否轮到引牌（场上没有要跟的牌）。无参数。返回是/否。
    bool is_lead() const { return !last_play_.has_value(); }

    // 本局首叫座位。无参数。返回 0～2。
    int first_caller() const { return first_caller_; }

    // 读走「刚刚因三人都不叫而重新发牌」标记，读完清掉。
    // 返回：若上一记叫地主触发了流局则为 true。
    bool take_call_redeal();

private:
    // 进入抢地主；若无人可抢则直接亮底牌。无参数。无返回值。
    void begin_rob();

    // 下一家座位。s：当前座位。返回 (s+1) 对 3 取余。
    int next_seat(int s) const { return (s + 1) % 3; }

    // 从 from 起找下一个有抢地主资格的人。
    // from：开始搜索的座位。
    // 返回：座位号；没有人可抢则为 -1。
    int next_rob_actor(int from) const;

    // 亮底牌并入地主手，进入加倍。无参数。无返回值。
    void reveal();

    // 进入出牌，由地主先出。无参数。无返回值。
    void begin_play();

    // 有人出完，写入胜负和春天。winner_seat：出完牌的座位。无返回值。
    void settle(int winner_seat);

    // 把一组牌判定成一种牌型。
    // cards：牌。pattern：指定的牌型名，空则要求只能有一种解释。
    // 返回：对得上则带牌型和比较键；否则空。
    std::optional<ddz::PatternHit> resolve_play(const std::vector<ddz::Card>& cards,
                                                const std::string& pattern) const;

    int base_score_;
    int public_mult_ = 1;
    Phase phase_ = Phase::Call;
    int first_caller_ = 0;
    int caller_ = -1;
    int actor_ = 0;
    int landlord_ = -1;
    std::array<bool, 3> called_no_{};
    std::array<bool, 3> rob_done_{};
    bool anyone_robbed_ = false;
    int rob_count_ = 0;
    std::array<std::vector<ddz::Card>, 3> hands_{};
    std::vector<ddz::Card> bottom_;
    bool bottom_revealed_ = false;
    std::array<bool, 3> double_chosen_{};
    std::array<bool, 3> double_yes_{};
    std::optional<PlayOnTable> last_play_;
    int consecutive_pass_ = 0;
    std::array<int, 3> play_count_{};
    int bomb_rocket_count_ = 0;
    std::string winner_side_;
    std::string spring_ = "none";
    bool call_redeal_ = false;
};
