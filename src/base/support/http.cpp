#include "base/support/http.hpp"

#include "base/support/curl.hpp"

namespace ahfl::support {
namespace {

[[nodiscard]] CurlHttpVersion to_curl_http_version(HttpVersion version) {
    switch (version) {
    case HttpVersion::Default:
        return CurlHttpVersion::Default;
    case HttpVersion::Http2:
        return CurlHttpVersion::Http2;
    case HttpVersion::Http2PriorKnowledge:
        return CurlHttpVersion::Http2PriorKnowledge;
    }
    return CurlHttpVersion::Default;
}

[[nodiscard]] CurlRequest to_curl_request(const HttpRequest &request) {
    CurlRequest curl_request;
    curl_request.method = request.method;
    curl_request.url = request.url;
    curl_request.headers = request.headers;
    curl_request.body = request.body;
    curl_request.timeout_seconds = request.timeout_seconds;
    curl_request.http_version = to_curl_http_version(request.version);
    return curl_request;
}

[[nodiscard]] HttpResponse to_http_response(const CurlResponse &response) {
    return HttpResponse{
        .status_code = response.status_code,
        .exit_code = response.exit_code,
        .body = response.body,
        .error = response.error,
        .timed_out = response.timed_out,
    };
}

} // namespace

HttpResponse execute_http(const HttpRequest &request) {
    return to_http_response(execute_curl(to_curl_request(request)));
}

std::string describe_http_command(const HttpRequest &request) {
    return describe_curl_command(to_curl_request(request));
}

} // namespace ahfl::support
