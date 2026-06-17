#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::support {

enum class CurlHttpVersion {
    Default,
    Http2,
    Http2PriorKnowledge,
};

struct CurlRequest {
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<std::string> body;
    int timeout_seconds{30};
    CurlHttpVersion http_version{CurlHttpVersion::Default};
    bool capture_headers{false};
    std::string tls_client_certificate_path;
    std::string tls_client_key_path;
    std::string tls_ca_certificate_path;
    bool verify_tls{true};
};

struct CurlHeaderBlock {
    bool has_status_line{false};
    std::vector<std::pair<std::string, std::string>> headers;
};

struct CurlResponse {
    int status_code{0};
    int exit_code{-1};
    std::string body;
    std::string error;
    bool timed_out{false};
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<CurlHeaderBlock> response_header_blocks;

    [[nodiscard]] bool is_success() const {
        return status_code >= 200 && status_code < 300;
    }
};

[[nodiscard]] CurlResponse execute_curl(const CurlRequest &request);

[[nodiscard]] std::string describe_curl_command(const CurlRequest &request);

} // namespace ahfl::support
