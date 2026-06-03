#include "runtime/engine/curl_transport_adapter.hpp"

#include "base/support/curl.hpp"
#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/http_transport.hpp"

namespace ahfl::runtime {
namespace {

[[nodiscard]] ahfl::support::CurlRequest make_curl_request(const HttpRequest &request) {
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
    return curl_request;
}

[[nodiscard]] ahfl::support::CurlRequest make_curl_request(const GrpcRequest &request) {
    ahfl::support::CurlRequest curl_request;
    curl_request.method = "POST";
    curl_request.url = request.endpoint.to_url();
    curl_request.headers = {
        {"Content-Type", "application/json"},
        {"TE", "trailers"},
    };
    for (const auto &[key, value] : request.metadata) {
        curl_request.headers.emplace_back(key, value);
    }
    curl_request.body = request.serialized_body.empty() ? "{}" : request.serialized_body;
    curl_request.timeout_seconds = static_cast<int>(request.timeout.count());
    curl_request.http_version = request.endpoint.use_tls
                                    ? ahfl::support::CurlHttpVersion::Http2
                                    : ahfl::support::CurlHttpVersion::Http2PriorKnowledge;
    return curl_request;
}

[[nodiscard]] WireTransportResponse to_wire_response(const ahfl::support::CurlResponse &response) {
    return WireTransportResponse{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
        .timed_out = response.timed_out,
    };
}

} // namespace

WireTransportResponse execute_curl_transport(const HttpRequest &request) {
    return to_wire_response(ahfl::support::execute_curl(make_curl_request(request)));
}

WireTransportResponse execute_curl_transport(const GrpcRequest &request) {
    return to_wire_response(ahfl::support::execute_curl(make_curl_request(request)));
}

std::string describe_curl_transport(const HttpRequest &request) {
    return ahfl::support::describe_curl_command(make_curl_request(request));
}

std::string describe_curl_transport(const GrpcRequest &request) {
    return ahfl::support::describe_curl_command(make_curl_request(request));
}

} // namespace ahfl::runtime
