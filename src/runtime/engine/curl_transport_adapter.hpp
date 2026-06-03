#pragma once

#include <string>

namespace ahfl::runtime {

struct GrpcRequest;
struct HttpRequest;

struct WireTransportResponse {
    int status_code{0};
    std::string body;
    std::string error;
    bool timed_out{false};
};

[[nodiscard]] WireTransportResponse execute_curl_transport(const HttpRequest &request);
[[nodiscard]] WireTransportResponse execute_curl_transport(const GrpcRequest &request);

[[nodiscard]] std::string describe_curl_transport(const HttpRequest &request);
[[nodiscard]] std::string describe_curl_transport(const GrpcRequest &request);

} // namespace ahfl::runtime
