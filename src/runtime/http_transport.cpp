#include "runtime/http_transport.hpp"

#include "support/curl.hpp"

namespace ahfl::runtime {

std::string HttpTransport::shell_quote(std::string_view value) {
    return std::string(value);
}

std::string HttpTransport::build_curl_command(const HttpRequest &request) {
    ahfl::support::CurlRequest curl_request;
    curl_request.method = request.method;
    curl_request.url = request.url;
    curl_request.timeout_seconds = request.timeout_seconds;
    for (const auto &[key, value] : request.headers) {
        curl_request.headers.emplace_back(key, value);
    }
    if (!request.body.empty()) {
        curl_request.body = request.body;
    }
    return ahfl::support::describe_curl_command(curl_request);
}

HttpResponse HttpTransport::execute(const HttpRequest &request) {
    ahfl::support::CurlRequest curl_request;
    curl_request.method = request.method;
    curl_request.url = request.url;
    curl_request.timeout_seconds = request.timeout_seconds;
    for (const auto &[key, value] : request.headers) {
        curl_request.headers.emplace_back(key, value);
    }
    if (!request.body.empty()) {
        curl_request.body = request.body;
    }

    const auto response = ahfl::support::execute_curl(curl_request);

    return HttpResponse{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
    };
}

} // namespace ahfl::runtime
