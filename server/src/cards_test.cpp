#include "cards.h"

#include <cassert>
#include <iostream>
#include <vector>

using ddz::Card;
using ddz::Pattern;

// 按点数和花色造一张普通牌。rank：3～15。suit：0～3。返回牌编号。
static Card mk(int rank, int suit) {
    return static_cast<Card>((rank - 3) * 4 + suit);
}

// 同一点数取 n 张（花色从 0 起）。rank：点数。n：张数。返回这些牌。
static std::vector<Card> nrank(int rank, int n) {
    std::vector<Card> v;
    for (int i = 0; i < n; ++i) {
        v.push_back(mk(rank, i));
    }
    return v;
}

// 把 b 接到 a 后面。a：被追加的列表。b：要接上的牌。无返回值。
static void append(std::vector<Card>& a, const std::vector<Card>& b) {
    a.insert(a.end(), b.begin(), b.end());
}

// 连续单张。from、to：起止点数，都包含。返回这些牌。
static std::vector<Card> seq_solo(int from, int to) {
    std::vector<Card> v;
    int suit = 0;
    for (int r = from; r <= to; ++r) {
        v.push_back(mk(r, suit++ % 4));
    }
    return v;
}

// 这组牌是否任何牌型都套不上。c：牌。套不上为 true。
static bool reject(const std::vector<Card>& c) {
    return !ddz::unique_hit(c) && ddz::all_hits(c).empty();
}

// 这组牌是否恰好是指定牌型。c：牌。p：期望牌型。对上为 true。
static bool accept(const std::vector<Card>& c, Pattern p) {
    auto u = ddz::unique_hit(c);
    return u && u->pattern == p;
}

int main() {
    // 应被拒绝的组合。
    {
        auto c = nrank(5, 4);
        append(c, nrank(3, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(5, 4);
        append(c, nrank(3, 1));
        append(c, nrank(4, 1));
        append(c, nrank(8, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 1));
        append(c, nrank(5, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 2);
        append(c, nrank(4, 2));
        assert(reject(c));
    }
    assert(reject(seq_solo(3, 6)));
    {
        auto c = nrank(14, 3);
        append(c, nrank(15, 3));
        assert(reject(c));
    }
    {
        auto c = nrank(15, 3);
        append(c, nrank(3, 3));
        assert(reject(c));
    }
    {
        auto c = seq_solo(10, 14);
        append(c, nrank(15, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(5, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(5, 3));
        append(c, nrank(6, 1));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 4);
        append(c, nrank(4, 4));
        assert(reject(c));
    }
    {
        auto c = nrank(5, 4);
        append(c, nrank(6, 4));
        assert(reject(c));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(6, 4));
        assert(!ddz::hit_as(c, Pattern::PlanePair));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(5, 3));
        append(c, nrank(6, 3));
        append(c, nrank(7, 4));
        assert(!ddz::hit_as(c, Pattern::PlaneSolo));
    }
    {
        auto c = seq_solo(3, 7);
        c.push_back(52);
        assert(reject(c));
    }
    {
        std::vector<Card> c{52, mk(3, 0)};
        assert(reject(c));
    }

    // 应被接受的组合。
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(5, 2));
        assert(accept(c, Pattern::PlaneSolo));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        c.push_back(52);
        c.push_back(53);
        assert(accept(c, Pattern::PlaneSolo));
    }
    {
        auto c = nrank(15, 4);
        c.push_back(52);
        c.push_back(53);
        assert(accept(c, Pattern::FourSolo));
    }
    {
        auto c = nrank(5, 4);
        append(c, nrank(3, 2));
        assert(accept(c, Pattern::FourSolo));
    }
    {
        auto c = nrank(3, 3);
        c.push_back(53);
        assert(accept(c, Pattern::TrioSolo));
    }
    {
        auto c = nrank(15, 3);
        append(c, nrank(3, 2));
        assert(accept(c, Pattern::TrioPair));
    }
    {
        auto c = nrank(13, 3);
        append(c, nrank(14, 3));
        assert(accept(c, Pattern::Plane));
    }
    {
        auto c = seq_solo(3, 14);
        assert(accept(c, Pattern::Straight));
    }
    {
        auto c = nrank(3, 3);
        append(c, nrank(4, 3));
        append(c, nrank(5, 3));
        append(c, nrank(7, 3));
        assert(accept(c, Pattern::PlaneSolo));
    }

    {
        int cnt[3] = {};
        const int n = 12000;
        for (int i = 0; i < n; ++i) {
            int v = ddz::random_int(0, 2);
            assert(v >= 0 && v <= 2);
            cnt[v]++;
        }
        for (int c : cnt) {
            assert(c > n / 5 && c < (n * 2) / 5);
        }
    }

    std::cout << "cards_test ok\n";
    return 0;
}
