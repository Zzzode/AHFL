#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::registry {

/// A parsed HTTP/1.1 request head: request line plus headers, with the
/// Content-Length surfaced separately so the transport knows how many body
/// bytes to read before assembling the full request.
struct HttpRequestHead {
    std::string method;
    std::string target;
    std::vector<std::pair<std::string, std::string>> headers;
    std::uint64_t content_length{0};
};

/// A complete HTTP/1.1 request (head + body). The registry protocol only
/// uses Content-Length bodies; chunked transfer-encoding is rejected.
struct HttpRequest {
    std::string method;
    std::string target;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    /// The request path with any query component removed.
    [[nodiscard]] std::string path() const;
    /// Case-insensitive header lookup; returns the first matching value.
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

/// An HTTP/1.1 response. The serializer always emits Content-Length and
/// Connection: close. `reason` may be left empty for the standard reason
/// phrases the serializer knows.
struct HttpResponse {
    int status{200};
    std::string reason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    [[nodiscard]] static HttpResponse json(int status, std::string body);
    [[nodiscard]] static HttpResponse text(int status, std::string content_type, std::string body);
};

/// Parses a request head (request line + headers, terminated by CRLF CRLF).
/// Returns nullopt on any protocol violation. The trailing blank line must
/// be present in `raw_head`.
[[nodiscard]] std::optional<HttpRequestHead> parse_http_head(std::string_view raw_head);

/// Assembles a request from a parsed head and its body, validating that the
/// body length matches the head's Content-Length. Returns nullopt on mismatch.
[[nodiscard]] std::optional<HttpRequest> build_http_request(HttpRequestHead head, std::string body);

/// Serializes a response as HTTP/1.1 with Content-Length and Connection: close.
[[nodiscard]] std::string serialize_http_response(const HttpResponse &response);

} // namespace ahfl::registry
