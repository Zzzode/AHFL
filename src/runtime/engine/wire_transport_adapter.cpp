#include "runtime/engine/wire_transport_adapter.hpp"

#include "base/support/http.hpp"
#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/http_transport.hpp"

#include <optional>

namespace ahfl::runtime {
namespace {

struct HeaderProjection {
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<std::pair<std::string, std::string>> trailers;
};

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
    http_request.tls_client_certificate_path = request.tls_client_certificate_path;
    http_request.tls_client_key_path = request.tls_client_key_path;
    return http_request;
}

[[nodiscard]] ahfl::support::HttpRequest
make_http_request(const GrpcJsonTranscodingRequest &request) {
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
    http_request.capture_headers = true;
    http_request.tls_client_certificate_path = request.tls_client_certificate_path;
    http_request.tls_client_key_path = request.tls_client_key_path;
    return http_request;
}

[[nodiscard]] HeaderProjection project_header_blocks(const ahfl::support::HttpResponse &response) {
    HeaderProjection projection;
    if (response.response_header_blocks.empty()) {
        projection.response_headers = response.response_headers;
        return projection;
    }

    std::optional<std::size_t> final_response_header_block;
    for (std::size_t index = 0; index < response.response_header_blocks.size(); ++index) {
        if (response.response_header_blocks[index].has_status_line) {
            final_response_header_block = index;
        }
    }

    if (!final_response_header_block.has_value()) {
        projection.response_headers = response.response_headers;
        return projection;
    }

    projection.response_headers =
        response.response_header_blocks[*final_response_header_block].headers;
    for (std::size_t index = *final_response_header_block + 1;
         index < response.response_header_blocks.size();
         ++index) {
        const auto &block = response.response_header_blocks[index];
        if (block.has_status_line) {
            continue;
        }
        projection.trailers.insert(
            projection.trailers.end(), block.headers.begin(), block.headers.end());
    }
    return projection;
}

[[nodiscard]] WireTransportResponse to_wire_response(const ahfl::support::HttpResponse &response) {
    const auto projection = project_header_blocks(response);
    return WireTransportResponse{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
        .timed_out = response.timed_out,
        .response_headers = projection.response_headers,
        .trailers = projection.trailers,
    };
}

} // namespace

WireTransportResponse WireTransport::execute(const HttpRequest &request) const {
    return to_wire_response(ahfl::support::execute_http(make_http_request(request)));
}

WireTransportResponse WireTransport::execute(const GrpcJsonTranscodingRequest &request) const {
    return to_wire_response(ahfl::support::execute_http(make_http_request(request)));
}

std::string WireTransport::describe(const HttpRequest &request) const {
    return ahfl::support::describe_http_command(make_http_request(request));
}

std::string WireTransport::describe(const GrpcJsonTranscodingRequest &request) const {
    return ahfl::support::describe_http_command(make_http_request(request));
}

const WireTransport &default_wire_transport() {
    static const WireTransport transport;
    return transport;
}

WireTransportResponse execute_wire_transport(const HttpRequest &request) {
    return default_wire_transport().execute(request);
}

WireTransportResponse execute_wire_transport(const GrpcJsonTranscodingRequest &request) {
    return default_wire_transport().execute(request);
}

std::string describe_wire_transport(const HttpRequest &request) {
    return default_wire_transport().describe(request);
}

std::string describe_wire_transport(const GrpcJsonTranscodingRequest &request) {
    return default_wire_transport().describe(request);
}

} // namespace ahfl::runtime
