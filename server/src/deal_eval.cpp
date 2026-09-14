#include "deal_eval.h"

#include "log_record.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

namespace {

const char* kFallbackPrompt =
    "你从 5 种斗地主发牌方案中选一种。只输出 PICK 和空格和 1 到 5 的编号。";

// 读出文本文件全部内容。path：文件路径。成功返回正文；打不开返回空串。
std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// 把一张牌写成点数记号，不含花色。c：牌编号。返回如 3、10、X、D。
std::string rank_token(ddz::Card c) {
    static const char* names[] = {
        "", "", "", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A", "2", "X", "D"};
    int r = ddz::rank_of(c);
    if (r < 3 || r > 17) {
        return "?";
    }
    return names[r];
}

// 把一手牌编成从大到小的点数串。hand：17 张。返回空格分隔文本。
std::string format_hand(const std::vector<ddz::Card>& hand) {
    auto h = hand;
    ddz::sort_hand(h);
    std::ostringstream oss;
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (i) {
            oss << ' ';
        }
        oss << rank_token(h[i]);
    }
    return oss.str();
}

// 洗一副牌，切成三手 17 张和 3 张底牌。无参数。返回未分座位的一副。
DealtPack shuffle_one() {
    DealtPack pack;
    auto deck = ddz::full_deck();
    ddz::shuffle_deck(deck);

    for (int s = 0; s < 3; ++s) {
        pack.hands[static_cast<std::size_t>(s)].assign(deck.begin() + s * 17, deck.begin() + (s + 1) * 17);
        ddz::sort_hand(pack.hands[static_cast<std::size_t>(s)]);
    }

    pack.bottom.assign(deck.begin() + 51, deck.end());
    return pack;
}

// 把五种方案编成用户消息。cands：五种发牌。dice：0～9，写入提示里的骰子。
// 返回给模型看的正文。
std::string format_user(const std::array<DealtPack, DealEval::kChoices>& cands, int dice) {
    std::ostringstream oss;
    oss << "骰子：" << dice << "\n";
    for (int i = 0; i < DealEval::kChoices; ++i) {
        oss << "\n方案" << (i + 1) << "：\n";
        oss << "手牌甲：" << format_hand(cands[static_cast<std::size_t>(i)].hands[0]) << "\n";
        oss << "手牌乙：" << format_hand(cands[static_cast<std::size_t>(i)].hands[1]) << "\n";
        oss << "手牌丙：" << format_hand(cands[static_cast<std::size_t>(i)].hands[2]) << "\n";
    }
    return oss.str();
}

// 从模型回复里读出 1～5 的方案编号。
// text：choices[0].message.content。
// 返回：1～5；认不出则为空。
std::optional<int> parse_pick(const std::string& text) {
    std::string upper;
    upper.reserve(text.size());
    for (unsigned char c : text) {
        upper.push_back(static_cast<char>(std::toupper(c)));
    }

    std::size_t from = 0;
    auto pick = upper.find("PICK");
    if (pick != std::string::npos) {
        from = pick + 4;
    }

    for (std::size_t i = from; i < text.size(); ++i) {
        char c = text[i];
        if (c >= '1' && c <= '0' + DealEval::kChoices) {
            return c - '0';
        }
    }

    return std::nullopt;
}

}  // namespace

DealEval::DealEval(const Config& cfg)
    : client_(cfg.deepseek_url, cfg.deepseek_apikey, cfg.deepseek_model) {
    const std::string path = cfg.resolve("deal_prompt.txt");
    prompt_ = read_file(path);
    if (prompt_.empty()) {
        log_warn("deal prompt missing, use fallback: " + path);
        prompt_ = kFallbackPrompt;
    }
    if (!client_.configured()) {
        log_warn("deepseek not configured, skip deal eval");
    }
}

DealtPack DealEval::pick() const {
    std::array<DealtPack, kChoices> cands;
    for (int i = 0; i < kChoices; ++i) {
        cands[static_cast<std::size_t>(i)] = shuffle_one();
    }

    if (!client_.configured()) {
        log_ai("skip, deepseek not configured, use scheme 1");
        return cands[0];
    }

    const int dice = ddz::random_int(0, 9);
    const std::string user = format_user(cands, dice);
    log_ai("request\n" + user);
    auto content = client_.chat(prompt_, user);
    if (!content) {
        log_ai("response failed, fallback scheme 1");
        log_warn("deal pick call failed, use scheme 1");
        return cands[0];
    }

    log_ai("response " + *content);
    auto n = parse_pick(*content);
    if (!n) {
        log_ai("parse fail, fallback scheme 1");
        log_warn("deal pick reply is not PICK 1-5: " + *content);
        return cands[0];
    }

    log_ai("pick=" + std::to_string(*n));
    return cands[static_cast<std::size_t>(*n - 1)];
}
