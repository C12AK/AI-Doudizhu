#pragma once

#include <optional>
#include <string>

// 用 curl 调 DeepSeek chat/completions。显式开启 thinking，reasoning_effort 为 high。
class DeepseekClient {
public:
    // 记下接口地址、密钥和模型名。
    // url：完整 POST 地址。apikey：Bearer 令牌。model：模型名。
    DeepseekClient(std::string url, std::string apikey, std::string model);

    // 三项是否都已填。无参数。齐了才能发请求。
    bool configured() const;

    // 发一轮非流式对话。
    // system_prompt：系统提示。user：用户正文。
    // 返回：助手 message.content；失败为空。
    std::optional<std::string> chat(const std::string& system_prompt, const std::string& user) const;

private:
    std::string url_;
    std::string apikey_;
    std::string model_;
};
