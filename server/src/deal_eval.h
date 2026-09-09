#pragma once

#include "cards.h"
#include "config_parse.h"
#include "deepseek_client.h"

#include <array>
#include <string>
#include <vector>

// 洗好并评估过的一副牌：三手 17 张加 3 张底牌。
struct DealtPack {
    std::array<std::vector<ddz::Card>, 3> hands;
    std::vector<ddz::Card> bottom;
};

// 发牌后请 AI 判断三副 17 张的强度差会不会破坏体验。
class DealEval {
public:
    // 从配置里取出 DeepSeek 地址、密钥和模型。
    // cfg：进程配置。
    explicit DealEval(const Config& cfg);

    // 洗牌直到评估通过，再按大局得分把三手分到座位。
    // scores：三座当前大局累计分。
    // 返回：可直接发给 Game 的一副牌。
    DealtPack make(const std::array<int, 3>& scores) const;

    // 三副手牌是否可以收下。
    // hands：三家各 17 张，不含底牌。
    // 返回：可以发则为 true（含接口未配置或调用失败时放行）；要重洗则为 false。
    bool accept(const std::array<std::vector<ddz::Card>, 3>& hands) const;

private:
    DeepseekClient client_;
};
