#include "deepseek_client.h"

#include "log_record.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

namespace {

constexpr const char* kCurl = "/usr/bin/curl";
constexpr int kHttpRetryMax = 3;
constexpr const char* kCurlTimeoutSec = "120";

// 从管道读完所有字节。fd：可读端。返回读到的文本。
std::string read_all(int fd) {
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    return out;
}

// 把整段数据写入管道。fd：可写端。data：内容。返回是否写完。
bool write_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

// HTTP 4xx 不重试；无状态码、000、429、5xx 视为可重试。
// code：curl 写出的 HTTP 状态码。返回是否值得再试。
bool http_status_retryable(const std::string& code) {
    if (code.empty() || code == "000") {
        return true;
    }

    int n = 0;
    try {
        n = std::stoi(code);
    } catch (...) {
        return true;
    }

    if (n == 429) {
        return true;
    }
    if (n >= 500 && n <= 599) {
        return true;
    }
    return false;
}

struct HttpOnce {
    std::optional<std::string> body;
    bool retryable = false;
};

// 一次 curl POST，不重试。
// url：接口地址。apikey：Bearer 令牌。json_body：请求体。
// 返回：200 时带正文；失败时 body 为空，retryable 表示是否值得再试。
HttpOnce http_post_once(const std::string& url, const std::string& apikey, const std::string& json_body) {
    int in_pipe[2];
    int out_pipe[2];
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) {
        log_warn(std::string("deepseek pipe failed: ") + std::strerror(errno));
        return HttpOnce{std::nullopt, true};
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        log_warn(std::string("deepseek fork failed: ") + std::strerror(errno));
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return HttpOnce{std::nullopt, true};
    }

    if (pid == 0) {
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);

        std::string auth = "Authorization: Bearer " + apikey;
        const char* args[] = {kCurl,
                              "-sS",
                              "--noproxy",
                              "*",
                              "-X",
                              "POST",
                              url.c_str(),
                              "-H",
                              "Content-Type: application/json",
                              "-H",
                              auth.c_str(),
                              "--data-binary",
                              "@-",
                              "-w",
                              "\n%{http_code}",
                              "-m",
                              kCurlTimeoutSec,
                              nullptr};
        ::execv(kCurl, const_cast<char**>(args));
        ::_exit(127);
    }

    ::close(in_pipe[0]);
    ::close(out_pipe[1]);

    bool wrote = write_all(in_pipe[1], json_body);
    ::close(in_pipe[1]);

    std::string raw = read_all(out_pipe[0]);
    ::close(out_pipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        log_warn(std::string("deepseek waitpid failed: ") + std::strerror(errno));
        return HttpOnce{std::nullopt, true};
    }

    if (!wrote) {
        log_warn("deepseek failed to write request body to curl");
        return HttpOnce{std::nullopt, true};
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_warn("deepseek curl failed, raw=" + raw);
        return HttpOnce{std::nullopt, true};
    }

    auto nl = raw.rfind('\n');
    if (nl == std::string::npos) {
        log_warn("deepseek curl output has no status line");
        return HttpOnce{std::nullopt, true};
    }

    std::string body = raw.substr(0, nl);
    std::string code = raw.substr(nl + 1);
    if (!body.empty() && body.back() == '\r') {
        body.pop_back();
    }
    if (!code.empty() && code.back() == '\r') {
        code.pop_back();
    }

    if (code != "200") {
        log_warn("deepseek http status " + code + " body=" + body);
        return HttpOnce{std::nullopt, http_status_retryable(code)};
    }

    return HttpOnce{body, false};
}

// 用 curl 对 url 发 JSON POST；网络类失败最多再试 kHttpRetryMax 次。
// url：接口地址。apikey：Bearer 令牌。json_body：请求体。
// 成功时返回 HTTP 正文；失败时返回空。
std::optional<std::string> http_post(const std::string& url, const std::string& apikey,
                                     const std::string& json_body) {
    for (int attempt = 0; attempt <= kHttpRetryMax; ++attempt) {
        if (attempt > 0) {
            log_info("deepseek http retry " + std::to_string(attempt) + "/" + std::to_string(kHttpRetryMax));
            ::sleep(1);
        }

        HttpOnce once = http_post_once(url, apikey, json_body);
        if (once.body) {
            return once.body;
        }
        if (!once.retryable) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

// 从接口 JSON 里取出助手正文。
// body：HTTP 响应。成功返回 content；失败返回空。
std::optional<std::string> extract_content(const std::string& body) {
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        log_warn("deepseek invalid json");
        return std::nullopt;
    }

    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        log_warn("deepseek response missing choices");
        return std::nullopt;
    }

    const auto& msg = j["choices"][0]["message"];
    if (!msg.is_object() || !msg.contains("content") || !msg["content"].is_string()) {
        log_warn("deepseek response missing message.content");
        return std::nullopt;
    }

    return msg["content"].get<std::string>();
}

}  // namespace

DeepseekClient::DeepseekClient(std::string url, std::string apikey, std::string model)
    : url_(std::move(url)), apikey_(std::move(apikey)), model_(std::move(model)) {}

bool DeepseekClient::configured() const {
    return !url_.empty() && !apikey_.empty() && !model_.empty();
}

std::optional<std::string> DeepseekClient::chat(const std::string& system_prompt,
                                               const std::string& user) const {
    if (!configured()) {
        return std::nullopt;
    }

    nlohmann::json sys = {{"role", "system"}, {"content", system_prompt}};
    nlohmann::json usr = {{"role", "user"}, {"content", user}};

    nlohmann::json thinking = {{"type", "enabled"}};

    nlohmann::json req;
    req["model"] = model_;
    req["stream"] = false;
    req["thinking"] = thinking;
    req["reasoning_effort"] = "high";
    req["messages"] = nlohmann::json::array({sys, usr});

    log_info("deepseek chat model=" + model_);
    auto body = http_post(url_, apikey_, req.dump());
    if (!body) {
        return std::nullopt;
    }

    return extract_content(*body);
}
