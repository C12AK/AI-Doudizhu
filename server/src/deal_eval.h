#pragma once

#include "cards.h"
#include "config_parse.h"
#include "deepseek_client.h"

#include <array>
#include <string>
#include <vector>

// 尚未按座位分配的一副牌：三手 17 张加 3 张底牌。
struct DealtPack {
    std::array<std::vector<ddz::Card>, 3> hands;
    std::vector<ddz::Card> bottom;
};

// 一次洗出若干种发牌，交给 AI 选出一副（不含座位分配）。
class DealEval {
public:
    static constexpr int kChoices = 5;

    // 从配置里取出 DeepSeek 地址、密钥、模型，并读提示词文件。
    // cfg：进程配置。提示词源文件是 server/src/deal_prompt.txt；
    // 运行时读配置文件同目录的 deal_prompt.txt（构建时从源文件拷入 deploy/config）。
    explicit DealEval(const Config& cfg);

    // 随机生成 kChoices 种发牌，问模型选一副。不问座位、不算得分。
    // 无参数。返回选中的一副；接口失败则退回第一种。
    DealtPack pick() const;

private:
    DeepseekClient client_;
    std::string prompt_;
};
