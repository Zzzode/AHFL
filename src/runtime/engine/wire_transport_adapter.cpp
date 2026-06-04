#include "runtime/engine/wire_transport_adapter.hpp"

#include "base/support/http.hpp"
#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/http_transport.hpp"

namespace ahfl::runtime {
namespace {

[[nodiscard]] ahfl::support::HttpRequest make_http_request(const HttpRequest &request) {
    ahfl::support::HttpRequest http_request;
    http_request.method = request.method;
    http_request.url = request.url;
    http_request.timeout_seconds = request.timeout_seconds;
    for (const auto &[key, value] : request.headers) {
        http_request.headers.emplace_back(key, value);
    }
    if (!request.body.empty()) {
        http_request.body = request.body;
    }
    return http_request;
}

[[nodiscard]] ahfl::support::HttpRequest make_http_request(const GrpcRequest &request) {
    ahfl::support::HttpRequest http_request;
    http_request.method = "POST";
    http_request.url = request.endpoint.to_url();
    http_request.headers = {
        {"Content-Type", "application/json"},
        {"TE", "trailers"},
    };
    for (const auto &[key, value] : request.metadata) {
        http_request.headers.emplace_back(key, value);
    }
    http_request.body = request.serialized_body.empty() ? "{}" : request.serialized_body;
    http_request.timeout_seconds = static_cast<int>(request.timeout.count());
    http_request.version = request.endpoint.use_tls
                               ? ahfl::support::HttpVersion::Http2
                               : ahfl::support::HttpVersion::Http2PriorKnowledge;
    return http_request;
}

[[nodiscard]] WireTransportResponse to_wire_response(const ahfl::support::HttpResponse &response) {
    return WireTransportResponse{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
        .timed_out = response.timed_out,
    };
}

} // namespace

WireTransportResponse execute_wire_transport(const HttpRequest &request) {
    return to_wire_response(ahfl::support::execute_http(make_http_request(request)));
}

WireTransportResponse execute_wire_transport(const GrpcRequest &request) {
    return to_wire_response(ahfl::support::execute_http(make_http_request(request)));
}

std::string describe_wire_transport(const HttpRequest &request) {
    return ahfl::support::describe_http_command(make_http_request(request));
}

std::string describe_wire_transport(const GrpcRequest &request) {
    return ahfl::support::describe_http_command(make_http_request(request));
}

} // namespace ahfl::runtime
