#include "deal_eval.h"

#include "deal_assign.h"
#include "log_record.h"

#include <cctype>
#include <optional>
#include <sstream>

namespace {

const char* kSystemPrompt =
    "你是斗地主发牌平衡裁判。你会看到三副各 17 张的手牌（不含尚未亮出的底牌）。"
    "请判断这三副牌的实力差距是否大到会破坏对局体验。"
    "建议（供你参考，最终由你判断）："
    "可接受——三家都有一定牌力，差距属于正常开局的强弱差"
    "（例如一家多一对 2、另一家多一张单王，或炸弹数量只差一个）。"
    "应重发——某一家明显碾压（例如独占火箭且另有多个 2 或炸弹，而另外两家几乎没有大牌），"
    "或两家极弱一家极强，导致另外两家几乎没有叫地主或对抗的空间。"
    "只回答 YES 或 NO。"
    "YES 表示差距过大、需要重新发牌。"
    "NO 表示可以接受、不必重发。";

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

// 从模型回复文本里读 YES/NO。
// text：choices[0].message.content。
// 返回：YES 为 true，NO 为 false；认不出则为空。
std::optional<bool> parse_yes_no(const std::string& text) {
    std::string tok;
    for (unsigned char c : text) {
        if (std::isalpha(c)) {
            tok.push_back(static_cast<char>(std::toupper(c)));
        } else if (!tok.empty()) {
            break;
        }
    }

    if (tok == "YES") {
        return true;
    }
    if (tok == "NO") {
        return false;
    }
    return std::nullopt;
}

}  // namespace

DealEval::DealEval(const Config& cfg)
    : client_(cfg.deepseek_url, cfg.deepseek_apikey, cfg.deepseek_model) {
    if (!client_.configured()) {
        log_warn("deepseek not configured, skip deal eval");
    }
}

bool DealEval::accept(const std::array<std::vector<ddz::Card>, 3>& hands) const {
    if (!client_.configured()) {
        return true;
    }

    std::ostringstream user;
    user << "手牌1：" << format_hand(hands[0]) << "\n";
    user << "手牌2：" << format_hand(hands[1]) << "\n";
    user << "手牌3：" << format_hand(hands[2]);

    log_debug("deal eval hands:\n" + user.str());
    auto content = client_.chat(kSystemPrompt, user.str());
    if (!content) {
        log_warn("deal eval call failed, keep current deal");
        return true;
    }

    auto yn = parse_yes_no(*content);
    if (!yn) {
        log_warn("deal eval reply is not YES/NO: " + *content);
        return true;
    }

    if (*yn) {
        log_info("deal eval YES, reshuffle");
        return false;
    }

    log_info("deal eval NO, keep");
    return true;
}

DealtPack DealEval::make(const std::array<int, 3>& scores) const {
    constexpr int kMaxTries = 4;
    DealtPack pack;

    for (int t = 1; t <= kMaxTries; ++t) {
        auto deck = ddz::full_deck();
        ddz::shuffle_deck(deck);

        for (int s = 0; s < 3; ++s) {
            pack.hands[static_cast<std::size_t>(s)].assign(deck.begin() + s * 17, deck.begin() + (s + 1) * 17);
            ddz::sort_hand(pack.hands[static_cast<std::size_t>(s)]);
        }

        pack.bottom.assign(deck.begin() + 51, deck.end());

        if (accept(pack.hands)) {
            break;
        }
        log_info("deal eval reject try=" + std::to_string(t) + "/" + std::to_string(kMaxTries));
    }

    DealAssign assign;
    assign.apply(pack.hands, scores);
    return pack;
}
