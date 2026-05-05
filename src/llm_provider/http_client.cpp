// http_client.cpp - HTTP 客户端（基于 popen + curl）

#include "ahfl/llm_provider/http_client.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>

namespace ahfl::llm_provider {

HttpClient::HttpClient(std::string base_url, std::string api_key, int timeout_seconds)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds) {}

HttpResponse HttpClient::chat_completions(const std::string &request_json) {
    // 转义 request_json 中的单引号，供 shell 使用
    std::string escaped_json;
    escaped_json.reserve(request_json.size());
    for (char c : request_json) {
        if (c == '\'') {
            escaped_json += "'\\''";
        } else {
            escaped_json += c;
        }
    }

    // 构建 curl 命令
    std::ostringstream cmd;
    cmd << "curl -s -w '\\n%{http_code}' -X POST "
        << "'" << base_url_ << "/chat/completions' "
        << "-H 'Content-Type: application/json' "
        << "-H 'Authorization: Bearer " << api_key_ << "' "
        << "-d '" << escaped_json << "' "
        << "--max-time " << timeout_seconds_;

    std::string command_str = cmd.str();

    // 通过 popen 执行 curl
    FILE *pipe = popen(command_str.c_str(), "r");
    if (pipe == nullptr) {
        return HttpResponse{0, "popen failed"};
    }

    // 读取输出
    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    int exit_code = pclose(pipe);

    if (exit_code != 0 && output.empty()) {
        return HttpResponse{0, "curl command failed"};
    }

    // 从输出末尾提取 HTTP 状态码（curl -w 写入的最后一行）
    int status_code = 200;
    std::string body = output;

    auto last_newline = output.rfind('\n');
    if (last_newline != std::string::npos && last_newline > 0) {
        // 最后一行是状态码
        std::string status_str = output.substr(last_newline + 1);
        body = output.substr(0, last_newline);
        try {
            status_code = std::stoi(status_str);
        } catch (...) {
            // 解析失败则假定 200
            status_code = 200;
            body = output;
        }
    }

    return HttpResponse{status_code, body};
}

} // namespace ahfl::llm_provider
