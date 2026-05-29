// http_client.cpp - HTTP client for OpenAI-compatible chat completions.

#include "runtime/providers/llm/http_client.hpp"

#include "base/support/curl.hpp"

namespace ahfl::llm_provider {

HttpClient::HttpClient(std::string base_url, std::string api_key, int timeout_seconds)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds) {}

HttpResponse HttpClient::chat_completions(const std::string &request_json) {
    ahfl::support::CurlRequest request;
    request.method = "POST";
    request.url = base_url_ + "/chat/completions";
    request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key_},
    };
    request.body = request_json;
    request.timeout_seconds = timeout_seconds_;

    const auto response = ahfl::support::execute_curl(request);
    if (response.status_code == 0 && !response.error.empty()) {
        return HttpResponse{0, response.error};
    }
    return HttpResponse{response.status_code, response.body};
}

} // namespace ahfl::llm_provider
