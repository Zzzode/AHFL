#include "tooling/registry/http.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::registry {

namespace {

constexpr std::string_view kHeaderEnd = "\r\n\r\n";

[[nodiscard]] bool is_token_char(unsigned char value) {
    // RFC 9110 tchar.
    if (std::isalnum(value) != 0) {
        return true;
    }
    constexpr std::string_view kSpecial = "!#$%&'*+-.^_`|~";
    return kSpecial.find(static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] bool is_digit(unsigned char value) {
    return value >= '0' && value <= '9';
}

[[nodiscard]] std::string_view trim_ows(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> parse_content_length(std::string_view value) {
    value = trim_ows(value);
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    for (const char digit : value) {
        if (!is_digit(static_cast<unsigned char>(digit))) {
            return std::nullopt;
        }
        const std::uint64_t next = result * 10 + static_cast<std::uint64_t>(digit - '0');
        if (next < result) {
            return std::nullopt; // overflow
        }
        result = next;
    }
    return result;
}

[[nodiscard]] std::string_view reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Content Too Large";
    case 500:
        return "Internal Server Error";
    default:
        return status >= 400 ? "Error" : "OK";
    }
}

} // namespace

std::string HttpRequest::path() const {
    const auto query = target.find('?');
    return query == std::string::npos ? target : target.substr(0, query);
}

std::optional<std::string> HttpRequest::header(std::string_view name) const {
    for (const auto &[key, value] : headers) {
        if (iequals(key, name)) {
            return value;
        }
    }
    return std::nullopt;
}

HttpResponse HttpResponse::json(int status, std::string body) {
    return HttpResponse{
        .status = status,
        .reason = {},
        .headers = {{"Content-Type", "application/json"}},
        .body = std::move(body),
    };
}

HttpResponse HttpResponse::text(int status, std::string content_type, std::string body) {
    return HttpResponse{
        .status = status,
        .reason = {},
        .headers = {{"Content-Type", std::move(content_type)}},
        .body = std::move(body),
    };
}

std::optional<HttpRequestHead> parse_http_head(std::string_view raw_head) {
    const auto header_end = raw_head.find(kHeaderEnd);
    if (header_end == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view head = raw_head.substr(0, header_end);

    const auto line_end = head.find("\r\n");
    const std::string_view request_line = line_end == std::string_view::npos
                                              ? head
                                              : head.substr(0, line_end);

    // Request line: METHOD SP TARGET SP HTTP-version
    const auto first_space = request_line.find(' ');
    if (first_space == std::string_view::npos || first_space == 0) {
        return std::nullopt;
    }
    const auto last_space = request_line.rfind(' ');
    if (last_space == first_space || last_space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view method = request_line.substr(0, first_space);
    const std::string_view target =
        request_line.substr(first_space + 1, last_space - first_space - 1);
    const std::string_view version = request_line.substr(last_space + 1);
    if (target.empty() || (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        return std::nullopt;
    }
    for (const unsigned char value : method) {
        if (!is_token_char(value)) {
            return std::nullopt;
        }
    }

    HttpRequestHead parsed;
    parsed.method = std::string(method);
    parsed.target = std::string(target);

    bool saw_content_length = false;
    std::uint64_t content_length = 0;

    std::size_t offset = line_end == std::string_view::npos ? head.size() : line_end + 2;
    while (offset < head.size()) {
        const auto next_line = head.find("\r\n", offset);
        const std::string_view line =
            next_line == std::string_view::npos ? head.substr(offset)
                                                : head.substr(offset, next_line - offset);
        if (line.empty()) {
            return std::nullopt;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return std::nullopt;
        }
        const std::string_view name = line.substr(0, colon);
        for (const unsigned char value : name) {
            if (!is_token_char(value)) {
                return std::nullopt;
            }
        }
        const std::string_view value = trim_ows(line.substr(colon + 1));

        if (iequals(name, "content-length")) {
            const auto parsed_length = parse_content_length(value);
            if (!parsed_length.has_value()) {
                return std::nullopt;
            }
            if (saw_content_length && *parsed_length != content_length) {
                return std::nullopt; // conflicting duplicates
            }
            saw_content_length = true;
            content_length = *parsed_length;
        } else if (iequals(name, "transfer-encoding")) {
            // The registry protocol only uses Content-Length bodies.
            return std::nullopt;
        }

        parsed.headers.emplace_back(std::string(name), std::string(value));
        if (next_line == std::string_view::npos) {
            break;
        }
        offset = next_line + 2;
    }

    parsed.content_length = content_length;
    return parsed;
}

std::optional<HttpRequest> build_http_request(HttpRequestHead head, std::string body) {
    if (body.size() != head.content_length) {
        return std::nullopt;
    }
    return HttpRequest{
        .method = std::move(head.method),
        .target = std::move(head.target),
        .headers = std::move(head.headers),
        .body = std::move(body),
    };
}

std::string serialize_http_response(const HttpResponse &response) {
    const std::string_view reason =
        response.reason.empty() ? reason_phrase(response.status) : std::string_view(response.reason);

    std::string serialized;
    serialized.reserve(64 + response.body.size());
    serialized.append("HTTP/1.1 ");
    serialized.append(std::to_string(response.status));
    serialized.push_back(' ');
    serialized.append(reason);
    serialized.append("\r\n");

    bool has_content_type = false;
    for (const auto &[name, value] : response.headers) {
        if (iequals(name, "content-type")) {
            has_content_type = true;
        }
        serialized.append(name);
        serialized.append(": ");
        serialized.append(value);
        serialized.append("\r\n");
    }
    if (!has_content_type) {
        serialized.append("Content-Type: application/octet-stream\r\n");
    }
    serialized.append("Content-Length: ");
    serialized.append(std::to_string(response.body.size()));
    serialized.append("\r\nConnection: close\r\n\r\n");
    serialized.append(response.body);
    return serialized;
}

} // namespace ahfl::registry
