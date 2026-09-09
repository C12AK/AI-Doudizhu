#include "deal_assign.h"

#include "log_record.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 相邻名次分差不超过此值则视为同一档，档内随机发牌。
// 7/6/5 两两相差 1，整桌同一档；5/1/2 里 5 与第二名相差 3，领先者单独一档。
constexpr int kScoreCloseGap = 2;

// 估一手牌的相对强度，只用来给三副牌排序，不替代 AI 评估。
// hand：一手牌。返回越大越强。
int hand_power(const std::vector<ddz::Card>& hand) {
    std::array<int, 18> cnt{};
    for (ddz::Card c : hand) {
        int r = ddz::rank_of(c);
        if (r >= 3 && r <= 17) {
            cnt[static_cast<std::size_t>(r)]++;
        }
    }

    int p = 0;
    const int n_x = cnt[16];
    const int n_d = cnt[17];
    if (n_x && n_d) {
        p += 80;
    } else {
        p += n_x * 12;
        p += n_d * 18;
    }

    for (int r = 3; r <= 15; ++r) {
        int n = cnt[static_cast<std::size_t>(r)];
        if (n == 4) {
            p += 36;
        } else if (n == 3) {
            p += 6;
        } else if (n == 2) {
            p += 2;
        }
    }

    p += cnt[15] * 8;
    p += cnt[14] * 4;
    p += cnt[13] * 2;
    return p;
}

// 原地打乱下标。v：要打乱的座位或手牌下标。无返回值。
void shuffle_idx(std::vector<int>& v) {
    for (int i = static_cast<int>(v.size()) - 1; i > 0; --i) {
        int j = ddz::random_int(0, i);
        std::swap(v[static_cast<std::size_t>(i)], v[static_cast<std::size_t>(j)]);
    }
}

}  // namespace

void DealAssign::apply(std::array<std::vector<ddz::Card>, 3>& hands,
                       const std::array<int, 3>& scores) const {
    std::array<int, 3> hand_ord{0, 1, 2};
    std::sort(hand_ord.begin(), hand_ord.end(), [&](int a, int b) {
        int pa = hand_power(hands[static_cast<std::size_t>(a)]);
        int pb = hand_power(hands[static_cast<std::size_t>(b)]);
        if (pa != pb) {
            return pa > pb;
        }
        return a < b;
    });

    std::array<int, 3> seat_ord{0, 1, 2};
    std::sort(seat_ord.begin(), seat_ord.end(), [&](int a, int b) {
        if (scores[static_cast<std::size_t>(a)] != scores[static_cast<std::size_t>(b)]) {
            return scores[static_cast<std::size_t>(a)] > scores[static_cast<std::size_t>(b)];
        }
        return a < b;
    });

    std::vector<std::vector<int>> tiers;
    for (int s : seat_ord) {
        if (tiers.empty()) {
            tiers.push_back({s});
            continue;
        }

        int prev = tiers.back().back();
        if (scores[static_cast<std::size_t>(prev)] - scores[static_cast<std::size_t>(s)] > kScoreCloseGap) {
            tiers.push_back({s});
        } else {
            tiers.back().push_back(s);
        }
    }

    std::vector<int> rest(hand_ord.begin(), hand_ord.end());
    std::array<std::vector<ddz::Card>, 3> placed{};

    for (auto& tier : tiers) {
        const int k = static_cast<int>(tier.size());
        std::vector<int> give;
        give.reserve(static_cast<std::size_t>(k));
        for (int i = 0; i < k; ++i) {
            give.push_back(rest.back());
            rest.pop_back();
        }

        shuffle_idx(tier);
        for (int i = 0; i < k; ++i) {
            placed[static_cast<std::size_t>(tier[static_cast<std::size_t>(i)])] =
                hands[static_cast<std::size_t>(give[static_cast<std::size_t>(i)])];
        }
    }

    std::ostringstream oss;
    oss << "deal assign scores=";
    for (int i = 0; i < 3; ++i) {
        if (i) {
            oss << ',';
        }
        oss << scores[static_cast<std::size_t>(i)];
    }
    oss << " power=";
    for (int i = 0; i < 3; ++i) {
        if (i) {
            oss << ',';
        }
        oss << hand_power(placed[static_cast<std::size_t>(i)]);
    }
    log_info(oss.str());

    hands = std::move(placed);
}
