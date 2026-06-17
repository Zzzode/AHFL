// http_client.cpp - HTTP client for OpenAI-compatible chat completions.

#include "runtime/providers/llm/http_client.hpp"

#include "base/support/http.hpp"

#include <cctype>

namespace ahfl::llm_provider {
namespace {

[[nodiscard]] std::string normalized_scheme(std::string_view scheme) {
    std::string normalized;
    normalized.reserve(scheme.size());
    for (const auto ch : scheme) {
        if (ch == '-') {
            normalized.push_back('_');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

[[nodiscard]] std::string default_auth_header(std::string_view scheme) {
    const auto normalized = normalized_scheme(scheme);
    if (normalized == "bearer" || normalized == "oauth2_bearer" ||
        normalized == "oauth2_client_credentials") {
        return "Authorization";
    }
    return {};
}

[[nodiscard]] std::string resolved_auth_header(const HttpAuthConfig &config) {
    if (!config.header.empty()) {
        return config.header;
    }
    return default_auth_header(config.scheme);
}

} // namespace

HttpClient::HttpClient(std::string base_url, std::string api_key, int timeout_seconds)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds) {}

HttpClient::HttpClient(std::string base_url,
                       std::string api_key,
                       int timeout_seconds,
                       HttpAuthConfig auth_config)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds), auth_config_(std::move(auth_config)) {}

HttpClient::HttpClient(std::string base_url,
                       std::string api_key,
                       int timeout_seconds,
                       ChatCompletionsTransport transport)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds), transport_(std::move(transport)) {}

HttpClient::HttpClient(std::string base_url,
                       std::string api_key,
                       int timeout_seconds,
                       ChatCompletionsTransport transport,
                       HttpAuthConfig auth_config)
    : base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds), transport_(std::move(transport)),
      auth_config_(std::move(auth_config)) {}

HttpResponse HttpClient::chat_completions(const std::string &request_json) {
    const auto headers = chat_completion_headers(api_key_, auth_config_);
    if (transport_) {
        return transport_(base_url_, headers, timeout_seconds_, request_json);
    }

    ahfl::support::HttpRequest request;
    request.method = "POST";
    request.url = base_url_ + "/chat/completions";
    request.headers = headers;
    request.body = request_json;
    request.timeout_seconds = timeout_seconds_;
    request.tls_client_certificate_path = auth_config_.mtls_client_cert_path;
    request.tls_client_key_path = auth_config_.mtls_client_key_path;
    request.tls_ca_certificate_path = auth_config_.mtls_ca_cert_path;
    request.verify_tls = auth_config_.mtls_verify_tls;

    const auto response = ahfl::support::execute_http(request);
    if (response.status_code == 0 && !response.error.empty()) {
        return HttpResponse{0, response.error};
    }
    return HttpResponse{response.status_code, response.body};
}

std::optional<std::string> validate_auth_config(const HttpAuthConfig &config) {
    const auto scheme = normalized_scheme(config.scheme);
    if (scheme != "bearer" && scheme != "api_key_header" && scheme != "oauth2_bearer" &&
        scheme != "oauth2_client_credentials" && scheme != "mtls") {
        return "LLM config auth_scheme must be 'bearer', 'api_key_header', "
               "'oauth2_bearer', 'oauth2_client_credentials', or 'mtls'";
    }
    if (scheme != "mtls" && resolved_auth_header(config).empty()) {
        return "LLM config auth_header must not be empty for auth_scheme '" + scheme + "'";
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>>
chat_completion_headers(std::string_view api_key, const HttpAuthConfig &auth_config) {
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("Content-Type", "application/json");

    const auto scheme = normalized_scheme(auth_config.scheme);
    const auto header = resolved_auth_header(auth_config);
    if (scheme == "bearer" || scheme == "oauth2_bearer" || scheme == "oauth2_client_credentials") {
        headers.emplace_back(header, "Bearer " + std::string(api_key));
    } else if (scheme == "api_key_header") {
        headers.emplace_back(header, std::string(api_key));
    }
    return headers;
}

} // namespace ahfl::llm_provider
