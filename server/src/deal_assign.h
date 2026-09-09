#pragma once

#include "cards.h"

#include <array>
#include <vector>

// 按本大局得分，把已经通过强度评估的三副手牌分给三个座位。
class DealAssign {
public:
    // 按分差分层后再发牌：分差不大的人同一层，层内随机；层与层之间，高分层拿相对更弱的牌。
    // hands：三副待发手牌，调用后改为座位 0/1/2 的手牌。
    // scores：三座当前大局累计分。
    // 无返回值。
    void apply(std::array<std::vector<ddz::Card>, 3>& hands, const std::array<int, 3>& scores) const;
};
