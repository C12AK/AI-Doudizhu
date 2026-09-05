#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ddz {

using Card = std::uint8_t; // 0-51 普通牌，52 小王，53 大王

enum class Pattern {
    Solo,
    Pair,
    Trio,
    TrioSolo,
    TrioPair,
    Straight,
    PairStraight,
    Plane,
    PlaneSolo,
    PlanePair,
    Bomb,
    Rocket,
    FourSolo,
    FourPair
};

struct PatternHit {
    Pattern pattern;
    int key; // 比较键
};

// 取点数。c：牌编号。返回 3～15 为 3～2，16 小王，17 大王。
int rank_of(Card c);

// 牌型的英文短名，给协议用。p：牌型。返回如 solo；未知则为空串。
std::string pattern_name(Pattern p);

// 英文短名反查牌型。name：短名。对得上则返回牌型，否则空。
std::optional<Pattern> pattern_from_name(const std::string& name);

// 一副 54 张，编号 0～53。无参数。
std::vector<Card> full_deck();

// 原地打乱。deck：要洗的牌。无返回值。
void shuffle_deck(std::vector<Card>& deck);

// 闭区间上均匀随机一个整数。lo、hi：两端都包含。返回 [lo, hi] 中的值。
int random_int(int lo, int hi);

// 按点数从大到小排手牌。hand：要排的牌。无返回值。
void sort_hand(std::vector<Card>& hand);

// 从手牌里拿掉若干张。hand：手牌，成功时被改掉。play：要拿掉的牌。
// 返回：每张都能在手里找到则为 true；否则手牌不变。
bool remove_cards(std::vector<Card>& hand, const std::vector<Card>& play);

// 列出这组牌所有合法牌型。cards：牌。返回可能为空、一条或多条。
std::vector<PatternHit> all_hits(const std::vector<Card>& cards);

// 若这组牌恰好只有一种合法解释则返回它，否则空。cards：牌。
std::optional<PatternHit> unique_hit(const std::vector<Card>& cards);

// 按指定牌型去套。cards：牌。p：牌型。套得上则返回带比较键，否则空。
std::optional<PatternHit> hit_as(const std::vector<Card>& cards, Pattern p);

// cand 能否压过场上的 field。
// field、cand：场上与候选。field_n、cand_n：各自张数。
// 返回：能压过为 true。
bool beats(const PatternHit& field, const PatternHit& cand, std::size_t field_n, std::size_t cand_n);

// 单张的调试字符串。c：牌编号。返回如 3S、X、D。
std::string card_debug(Card c);

// 若干张的调试字符串，空格分开。cards：牌。
std::string cards_debug(const std::vector<Card>& cards);

} // namespace ddz
