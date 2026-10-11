#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::support {

enum class HttpVersion {
    Default,
    Http2,
    Http2PriorKnowledge,
};

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<std::string> body;
    int timeout_seconds{30};
    HttpVersion version{HttpVersion::Default};
    bool capture_headers{false};
    std::string tls_client_certificate_path;
    std::string tls_client_key_path;
    std::string tls_ca_certificate_path;
    bool verify_tls{true};
};

struct HttpHeaderBlock {
    bool has_status_line{false};
    std::vector<std::pair<std::string, std::string>> headers;
};

struct HttpResponse {
    int status_code{0};
    int exit_code{-1};
    std::string body;
    std::string error;
    bool timed_out{false};
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<HttpHeaderBlock> response_header_blocks;

    [[nodiscard]] bool is_success() const {
        return status_code >= 200 && status_code < 300;
    }

    [[nodiscard]] bool is_timeout() const {
        return timed_out || (status_code == 0 && error.find("timeout") != std::string::npos);
    }
};

/// Callbacks for incremental streaming HTTP responses.
struct HttpStreamCallbacks {
    /// Called for each chunk of response body data as it arrives.
    std::function<void(std::string_view bytes)> on_body;
    /// Called periodically during the transfer; return true to abort.
    std::function<bool()> should_cancel;
};

/// Execute an HTTP request and buffer the full response.
[[nodiscard]] HttpResponse execute_http(const HttpRequest &request);

/// Execute an HTTP request and deliver response body incrementally.
/// The response body is also returned in full for convenience.
[[nodiscard]] HttpResponse execute_http_streaming(const HttpRequest &request,
                                                   HttpStreamCallbacks callbacks);

/// Describe the request as a human-readable curl-style command string.
[[nodiscard]] std::string describe_http_command(const HttpRequest &request);

} // namespace ahfl::support
