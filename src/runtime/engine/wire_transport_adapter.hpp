#pragma once

#include <string>

namespace ahfl::runtime {

struct GrpcJsonTranscodingRequest;
struct HttpRequest;

struct WireTransportResponse {
    int status_code{0};
    std::string body;
    std::string error;
    bool timed_out{false};
};

[[nodiscard]] WireTransportResponse execute_wire_transport(const HttpRequest &request);
[[nodiscard]] WireTransportResponse
execute_wire_transport(const GrpcJsonTranscodingRequest &request);

[[nodiscard]] std::string describe_wire_transport(const HttpRequest &request);
[[nodiscard]] std::string describe_wire_transport(const GrpcJsonTranscodingRequest &request);

} // namespace ahfl::runtime
