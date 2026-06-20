#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::llm_provider {

struct HttpAuthConfig {
    std::string scheme{"bearer"};
    std::string header;
    std::string mtls_client_cert_path;
    std::string mtls_client_key_path;
    std::string mtls_ca_cert_path;
    bool mtls_verify_tls{true};
};

// HTTP response
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
    using ChatCompletionsTransport =
        std::function<HttpResponse(const std::string &base_url,
                                   const std::vector<std::pair<std::string, std::string>> &headers,
                                   int timeout_seconds,
                                   const std::string &request_json)>;

    explicit HttpClient(std::string base_url, std::string api_key, int timeout_seconds = 30);
    HttpClient(std::string base_url,
               std::string api_key,
               int timeout_seconds,
               HttpAuthConfig auth_config);
    HttpClient(std::string base_url,
               std::string api_key,
               int timeout_seconds,
               ChatCompletionsTransport transport);
    HttpClient(std::string base_url,
               std::string api_key,
               int timeout_seconds,
               ChatCompletionsTransport transport,
               HttpAuthConfig auth_config);

    // POST /chat/completions
    [[nodiscard]] HttpResponse chat_completions(const std::string &request_json);

  private:
    std::string base_url_;
    std::string api_key_;
    int timeout_seconds_;
    ChatCompletionsTransport transport_;
    HttpAuthConfig auth_config_;
};

[[nodiscard]] std::optional<std::string> validate_auth_config(const HttpAuthConfig &config);

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
chat_completion_headers(std::string_view api_key, const HttpAuthConfig &auth_config);

} // namespace ahfl::llm_provider
