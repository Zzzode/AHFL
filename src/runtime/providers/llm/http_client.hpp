#pragma once

#include <string>

namespace ahfl::llm_provider {

// HTTP 响应
struct HttpResponse {
    int status_code = 0;
    std::string body;
    [[nodiscard]] bool success() const {
        return status_code >= 200 && status_code < 300;
    }
};

// HTTP client for OpenAI-compatible APIs.
class HttpClient {
  public:
    explicit HttpClient(std::string base_url, std::string api_key, int timeout_seconds = 30);

    // POST /chat/completions
    [[nodiscard]] HttpResponse chat_completions(const std::string &request_json);

  private:
    std::string base_url_;
    std::string api_key_;
    int timeout_seconds_;
};

} // namespace ahfl::llm_provider
