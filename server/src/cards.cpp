#include "cards.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <unistd.h>

namespace ddz {
namespace {

constexpr int RANK_3 = 3;
constexpr int RANK_A = 14;
constexpr int RANK_2 = 15;
constexpr int RANK_X = 16;
constexpr int RANK_D = 17;

// 按点数统计张数。cards：牌。返回下标为点数、值为张数的表。
std::array<int, 18> counts_of(const std::vector<Card>& cards) {
    std::array<int, 18> cnt{};
    for (Card c : cards) {
        int r = rank_of(c);
        if (r >= 3 && r <= 17) {
            cnt[static_cast<std::size_t>(r)]++;
        }
    }
    return cnt;
}

// 点数是否从 3 到 A、无重复、且相邻差 1。2 和王不能出现。
// ranks：待检查的点数。满足则为 true。
bool consecutive_3_to_a(const std::vector<int>& ranks) {
    if (ranks.empty()) {
        return false;
    }

    for (int r : ranks) {
        if (r < RANK_3 || r > RANK_A) {
            return false;
        }
    }

    std::vector<int> s = ranks;
    std::sort(s.begin(), s.end());
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] == s[i - 1]) {
            return false;
        }
        if (s[i] != s[i - 1] + 1) {
            return false;
        }
    }
    return true;
}

// 是否为单张。cards：要判定的牌。对得上返回点数，对不上返回空。
std::optional<int> match_solo(const std::vector<Card>& cards) {
    if (cards.size() != 1) {
        return std::nullopt;
    }
    return rank_of(cards[0]);
}

// 是否为火箭（大王小王）。cards：要判定的牌。对得上返回大王的点数，对不上返回空。
std::optional<int> match_rocket(const std::vector<Card>& cards) {
    if (cards.size() != 2) {
        return std::nullopt;
    }

    int a = rank_of(cards[0]);
    int b = rank_of(cards[1]);
    if ((a == RANK_X && b == RANK_D) || (a == RANK_D && b == RANK_X)) {
        return RANK_D;
    }
    return std::nullopt;
}

// 是否为一对（不含火箭）。cards：要判定的牌。对得上返回点数，对不上返回空。
std::optional<int> match_pair(const std::vector<Card>& cards) {
    if (cards.size() != 2) {
        return std::nullopt;
    }
    if (match_rocket(cards)) {
        return std::nullopt;
    }

    int r = rank_of(cards[0]);
    if (r != rank_of(cards[1])) {
        return std::nullopt;
    }
    if (r >= RANK_X) {
        return std::nullopt;
    }
    return r;
}

// 是否为三张不带牌。cards：要判定的牌。对得上返回点数，对不上返回空。
std::optional<int> match_trio(const std::vector<Card>& cards) {
    if (cards.size() != 3) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    for (int r = RANK_3; r <= RANK_2; ++r) {
        if (cnt[static_cast<std::size_t>(r)] == 3) {
            return r;
        }
    }
    return std::nullopt;
}

// 是否为四张炸弹。cards：要判定的牌。对得上返回点数，对不上返回空。
std::optional<int> match_bomb(const std::vector<Card>& cards) {
    if (cards.size() != 4) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    for (int r = RANK_3; r <= RANK_2; ++r) {
        if (cnt[static_cast<std::size_t>(r)] == 4) {
            return r;
        }
    }
    return std::nullopt;
}

// 是否为三带一。cards：要判定的牌。对得上返回主体点数，对不上返回空。
std::optional<int> match_trio_solo(const std::vector<Card>& cards) {
    if (cards.size() != 4) {
        return std::nullopt;
    }
    // 四张炸弹不算三带一。
    if (match_bomb(cards)) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int body = 0;
    int others = 0;
    for (int r = RANK_3; r <= RANK_D; ++r) {
        int n = cnt[static_cast<std::size_t>(r)];
        if (n == 0) {
            continue;
        }
        if (n == 3 && r <= RANK_2 && body == 0) {
            body = r;
        } else {
            others += n;
        }
    }

    if (body != 0 && others == 1) {
        return body;
    }
    return std::nullopt;
}

// 是否为三带一对。cards：要判定的牌。对得上返回主体点数，对不上返回空。
std::optional<int> match_trio_pair(const std::vector<Card>& cards) {
    if (cards.size() != 5) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int body = 0;
    int pair = 0;
    for (int r = RANK_3; r <= RANK_2; ++r) {
        int n = cnt[static_cast<std::size_t>(r)];
        if (n == 3 && body == 0) {
            body = r;
        } else if (n == 2 && pair == 0) {
            pair = r;
        } else if (n != 0) {
            return std::nullopt;
        }
    }

    if (cnt[RANK_X] || cnt[RANK_D]) {
        return std::nullopt;
    }
    if (body != 0 && pair != 0 && body != pair) {
        return body;
    }
    return std::nullopt;
}

// 是否为顺子（至少 5 张，不能含 2 和王）。cards：要判定的牌。对得上返回最大点数，对不上返回空。
std::optional<int> match_straight(const std::vector<Card>& cards) {
    const int k = static_cast<int>(cards.size());
    if (k < 5) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    std::vector<int> ranks;
    for (int r = RANK_3; r <= RANK_D; ++r) {
        int n = cnt[static_cast<std::size_t>(r)];
        if (n == 0) {
            continue;
        }
        if (n != 1) {
            return std::nullopt;
        }
        ranks.push_back(r);
    }

    if (static_cast<int>(ranks.size()) != k) {
        return std::nullopt;
    }
    if (!consecutive_3_to_a(ranks)) {
        return std::nullopt;
    }
    return *std::max_element(ranks.begin(), ranks.end());
}

// 是否为连对（至少 3 对）。cards：要判定的牌。对得上返回最大点数，对不上返回空。
std::optional<int> match_pair_straight(const std::vector<Card>& cards) {
    const int k = static_cast<int>(cards.size());
    if (k < 6 || k % 2 != 0) {
        return std::nullopt;
    }
    const int n = k / 2;
    if (n < 3) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    std::vector<int> ranks;
    for (int r = RANK_3; r <= RANK_D; ++r) {
        int c = cnt[static_cast<std::size_t>(r)];
        if (c == 0) {
            continue;
        }
        if (c != 2) {
            return std::nullopt;
        }
        ranks.push_back(r);
    }

    if (static_cast<int>(ranks.size()) != n) {
        return std::nullopt;
    }
    if (!consecutive_3_to_a(ranks)) {
        return std::nullopt;
    }
    return *std::max_element(ranks.begin(), ranks.end());
}

// 是否为飞机不带翅膀（至少两连三张）。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
std::optional<int> match_plane(const std::vector<Card>& cards) {
    const int k = static_cast<int>(cards.size());
    if (k < 6 || k % 3 != 0) {
        return std::nullopt;
    }
    const int n = k / 3;
    if (n < 2) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    std::vector<int> ranks;
    for (int r = RANK_3; r <= RANK_D; ++r) {
        int c = cnt[static_cast<std::size_t>(r)];
        if (c == 0) {
            continue;
        }
        if (c != 3) {
            return std::nullopt;
        }
        ranks.push_back(r);
    }

    if (static_cast<int>(ranks.size()) != n) {
        return std::nullopt;
    }
    if (!consecutive_3_to_a(ranks)) {
        return std::nullopt;
    }
    return *std::max_element(ranks.begin(), ranks.end());
}

// 是否为四带二单张。cards：要判定的牌。对得上返回主体点数，对不上返回空。
std::optional<int> match_four_solo(const std::vector<Card>& cards) {
    if (cards.size() != 6) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int body = 0;
    for (int r = RANK_3; r <= RANK_2; ++r) {
        if (cnt[static_cast<std::size_t>(r)] == 4) {
            if (body != 0) {
                return std::nullopt;
            }
            body = r;
        }
    }
    if (body == 0) {
        return std::nullopt;
    }

    int rest = 0;
    for (int r = RANK_3; r <= RANK_D; ++r) {
        if (r == body) {
            continue;
        }
        rest += cnt[static_cast<std::size_t>(r)];
    }
    if (rest != 2) {
        return std::nullopt;
    }
    return body;
}

// 是否为四带两对。cards：要判定的牌。对得上返回主体点数，对不上返回空。
std::optional<int> match_four_pair(const std::vector<Card>& cards) {
    if (cards.size() != 8) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int body = 0;
    int pairs = 0;
    for (int r = RANK_3; r <= RANK_2; ++r) {
        int c = cnt[static_cast<std::size_t>(r)];
        if (c == 4) {
            // 已经有一个四张当主体后，再来一个四张就是两副炸弹当翅膀，不允许。
            if (body != 0) {
                return std::nullopt;
            }
            body = r;
        } else if (c == 2) {
            pairs++;
        } else if (c != 0) {
            return std::nullopt;
        }
    }

    if (cnt[RANK_X] || cnt[RANK_D]) {
        return std::nullopt;
    }
    if (body != 0 && pairs == 2) {
        return body;
    }
    return std::nullopt;
}

// 是否为飞机带单张。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
std::optional<int> match_plane_solo(const std::vector<Card>& cards) {
    const int k = static_cast<int>(cards.size());
    if (k < 8 || k % 4 != 0) {
        return std::nullopt;
    }
    const int n = k / 4;
    if (n < 2) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int best = 0;
    bool found = false;
    // 滑动窗口：每一段连续 n 个点数都至少 3 张，当作主体。
    for (int start = RANK_3; start + n - 1 <= RANK_A; ++start) {
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            if (cnt[static_cast<std::size_t>(start + i)] < 3) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }

        // 扣掉主体各 3 张后，剩下应正好 n 张当翅膀。
        std::array<int, 18> rem = cnt;
        int left = k;
        for (int i = 0; i < n; ++i) {
            rem[static_cast<std::size_t>(start + i)] -= 3;
            left -= 3;
        }
        if (left != n) {
            continue;
        }

        // 翅膀不能落在主体窗口内；同一点数不能超过 3 张。
        bool wings_ok = true;
        int wing_cards = 0;
        for (int r = RANK_3; r <= RANK_D; ++r) {
            int c = rem[static_cast<std::size_t>(r)];
            if (c == 0) {
                continue;
            }
            if (r >= start && r <= start + n - 1) {
                wings_ok = false;
                break;
            }
            if (c > 3) {
                wings_ok = false;
                break;
            }
            wing_cards += c;
        }
        if (!wings_ok || wing_cards != n) {
            continue;
        }

        found = true;
        best = std::max(best, start + n - 1);
    }

    if (!found) {
        return std::nullopt;
    }
    return best;
}

// 是否为飞机带对子。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
std::optional<int> match_plane_pair(const std::vector<Card>& cards) {
    const int k = static_cast<int>(cards.size());
    if (k < 10 || k % 5 != 0) {
        return std::nullopt;
    }
    const int n = k / 5;
    if (n < 2) {
        return std::nullopt;
    }

    auto cnt = counts_of(cards);
    int best = 0;
    bool found = false;
    for (int start = RANK_3; start + n - 1 <= RANK_A; ++start) {
        // 主体每个点数必须恰好 3 张。
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            if (cnt[static_cast<std::size_t>(start + i)] != 3) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }

        // 翅膀必须是对子，且不能是王。
        int pair_n = 0;
        bool wings_ok = true;
        for (int r = RANK_3; r <= RANK_D; ++r) {
            if (r >= start && r <= start + n - 1) {
                continue;
            }
            int c = cnt[static_cast<std::size_t>(r)];
            if (c == 0) {
                continue;
            }
            if (c != 2 || r >= RANK_X) {
                wings_ok = false;
                break;
            }
            pair_n++;
        }
        if (!wings_ok || pair_n != n) {
            continue;
        }

        found = true;
        best = std::max(best, start + n - 1);
    }

    if (!found) {
        return std::nullopt;
    }
    return best;
}

using Matcher = std::optional<int> (*)(const std::vector<Card>&);

struct NamedMatcher {
    Pattern pattern;
    Matcher fn;
};

const NamedMatcher kMatchers[] = {
    {Pattern::Rocket, match_rocket},
    {Pattern::Bomb, match_bomb},
    {Pattern::Solo, match_solo},
    {Pattern::Pair, match_pair},
    {Pattern::Trio, match_trio},
    {Pattern::TrioSolo, match_trio_solo},
    {Pattern::TrioPair, match_trio_pair},
    {Pattern::Straight, match_straight},
    {Pattern::PairStraight, match_pair_straight},
    {Pattern::Plane, match_plane},
    {Pattern::PlaneSolo, match_plane_solo},
    {Pattern::PlanePair, match_plane_pair},
    {Pattern::FourSolo, match_four_solo},
    {Pattern::FourPair, match_four_pair},
};

} // namespace

int rank_of(Card c) {
    if (c == 52) {
        return RANK_X;
    }
    if (c == 53) {
        return RANK_D;
    }
    return static_cast<int>(c / 4) + 3;
}

std::string pattern_name(Pattern p) {
    switch (p) {
    case Pattern::Solo:
        return "solo";
    case Pattern::Pair:
        return "pair";
    case Pattern::Trio:
        return "trio";
    case Pattern::TrioSolo:
        return "trio_solo";
    case Pattern::TrioPair:
        return "trio_pair";
    case Pattern::Straight:
        return "straight";
    case Pattern::PairStraight:
        return "pair_straight";
    case Pattern::Plane:
        return "plane";
    case Pattern::PlaneSolo:
        return "plane_solo";
    case Pattern::PlanePair:
        return "plane_pair";
    case Pattern::Bomb:
        return "bomb";
    case Pattern::Rocket:
        return "rocket";
    case Pattern::FourSolo:
        return "four_solo";
    case Pattern::FourPair:
        return "four_pair";
    }
    return "";
}

std::optional<Pattern> pattern_from_name(const std::string& name) {
    for (const auto& m : kMatchers) {
        if (pattern_name(m.pattern) == name) {
            return m.pattern;
        }
    }
    return std::nullopt;
}

std::vector<Card> full_deck() {
    std::vector<Card> d(54);
    for (int i = 0; i < 54; ++i) {
        d[static_cast<std::size_t>(i)] = static_cast<Card>(i);
    }
    return d;
}

// 本线程共用的随机发生器，只给洗牌用。无参数。返回发生器引用。
static std::mt19937_64& rng() {
    static thread_local std::mt19937_64 gen = [] {
        std::random_device rd;
        const std::uint64_t mix[] = {
            (static_cast<std::uint64_t>(rd()) << 32) | rd(),
            (static_cast<std::uint64_t>(rd()) << 32) | rd(),
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            static_cast<std::uint64_t>(::getpid()),
        };
        std::seed_seq seq(std::begin(mix), std::end(mix));
        std::mt19937_64 g{seq};
        g.discard(256);
        return g;
    }();
    return gen;
}

void shuffle_deck(std::vector<Card>& deck) {
    std::shuffle(deck.begin(), deck.end(), rng());
}

int random_int(int lo, int hi) {
    if (lo > hi) {
        const int t = lo;
        lo = hi;
        hi = t;
    }
    if (lo == hi) {
        return lo;
    }

    const unsigned span = static_cast<unsigned>(hi - lo) + 1u;
    const unsigned limit = (0xFFFFFFFFu / span) * span;
    std::ifstream in("/dev/urandom", std::ios::binary);
    unsigned x = 0;
    while (in && in.read(reinterpret_cast<char*>(&x), sizeof(x))) {
        if (x < limit) {
            return lo + static_cast<int>(x % span);
        }
    }

    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng());
}

void sort_hand(std::vector<Card>& hand) {
    std::sort(hand.begin(), hand.end(), [](Card a, Card b) {
        int ra = rank_of(a);
        int rb = rank_of(b);
        if (ra != rb) {
            return ra > rb;
        }
        return a > b;
    });
}

bool remove_cards(std::vector<Card>& hand, const std::vector<Card>& play) {
    std::vector<Card> copy = hand;
    for (Card c : play) {
        auto it = std::find(copy.begin(), copy.end(), c);
        if (it == copy.end()) {
            return false;
        }
        copy.erase(it);
    }

    hand.swap(copy);
    return true;
}

std::vector<PatternHit> all_hits(const std::vector<Card>& cards) {
    std::vector<PatternHit> out;
    for (const auto& m : kMatchers) {
        if (auto key = m.fn(cards)) {
            out.push_back(PatternHit{m.pattern, *key});
        }
    }
    return out;
}

std::optional<PatternHit> unique_hit(const std::vector<Card>& cards) {
    auto hits = all_hits(cards);
    if (hits.size() == 1) {
        return hits[0];
    }
    return std::nullopt;
}

std::optional<PatternHit> hit_as(const std::vector<Card>& cards, Pattern p) {
    for (const auto& m : kMatchers) {
        if (m.pattern == p) {
            if (auto key = m.fn(cards)) {
                return PatternHit{p, *key};
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool beats(const PatternHit& field, const PatternHit& cand, std::size_t field_n, std::size_t cand_n) {
    // 火箭最大，炸弹次之；其余必须同型同张数，再比键。
    if (field.pattern == Pattern::Rocket) {
        return false;
    }
    if (cand.pattern == Pattern::Rocket) {
        return true;
    }

    if (cand.pattern == Pattern::Bomb) {
        if (field.pattern != Pattern::Bomb) {
            return true;
        }
        return cand.key > field.key;
    }

    if (field.pattern != cand.pattern) {
        return false;
    }
    if (field_n != cand_n) {
        return false;
    }
    return cand.key > field.key;
}

std::string card_debug(Card c) {
    static const char* ranks[] = {
        "", "", "", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A", "2", "X", "D"};
    int r = rank_of(c);
    if (r == RANK_X) {
        return "X";
    }
    if (r == RANK_D) {
        return "D";
    }
    const char suits[] = {'S', 'H', 'C', 'D'};
    std::string s = ranks[r];
    s.push_back(suits[c % 4]);
    return s;
}

std::string cards_debug(const std::vector<Card>& cards) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i) {
            oss << ' ';
        }
        oss << card_debug(cards[i]);
    }
    return oss.str();
}

} // namespace ddz
