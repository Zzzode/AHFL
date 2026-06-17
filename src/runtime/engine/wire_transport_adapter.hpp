#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ahfl::runtime {

struct GrpcJsonTranscodingRequest;
struct HttpRequest;

struct WireTransportResponse {
    int status_code{0};
    std::string body;
    std::string error;
    bool timed_out{false};
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<std::pair<std::string, std::string>> trailers;
};

class WireTransport {
  public:
    [[nodiscard]] WireTransportResponse execute(const HttpRequest &request) const;
    [[nodiscard]] WireTransportResponse execute(const GrpcJsonTranscodingRequest &request) const;

    [[nodiscard]] std::string describe(const HttpRequest &request) const;
    [[nodiscard]] std::string describe(const GrpcJsonTranscodingRequest &request) const;
};

[[nodiscard]] const WireTransport &default_wire_transport();

[[nodiscard]] WireTransportResponse execute_wire_transport(const HttpRequest &request);
[[nodiscard]] WireTransportResponse
execute_wire_transport(const GrpcJsonTranscodingRequest &request);

[[nodiscard]] std::string describe_wire_transport(const HttpRequest &request);
[[nodiscard]] std::string describe_wire_transport(const GrpcJsonTranscodingRequest &request);

} // namespace ahfl::runtime
