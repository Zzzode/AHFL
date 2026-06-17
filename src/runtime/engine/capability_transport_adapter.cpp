#include "runtime/engine/capability_transport_adapter.hpp"

#include "runtime/engine/wire_transport_adapter.hpp"

#include <memory>
#include <string>

namespace ahfl::runtime {
namespace {

class DefaultCapabilityTransportAdapter final : public CapabilityTransportAdapter {
  public:
    [[nodiscard]] HttpResponse execute_http(const HttpRequest &request) const override {
        const auto response = default_wire_transport().execute(request);
        return HttpResponse{
            .status_code = response.status_code,
            .body = response.body,
            .error = response.error,
        };
    }

    [[nodiscard]] GrpcJsonTranscodingResponse
    execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request) const override {
        const auto response = default_wire_transport().execute(request);
        if (response.timed_out) {
            return GrpcJsonTranscodingResponse{
                .status_code = GrpcStatusCode::DeadlineExceeded,
                .body = {},
                .error_message = "gRPC JSON transcoding deadline exceeded",
                .response_metadata = response.response_headers,
                .trailers = response.trailers,
            };
        }
        if (response.status_code == 0 && !response.error.empty()) {
            return GrpcJsonTranscodingResponse{
                .status_code = GrpcStatusCode::Unavailable,
                .body = response.body,
                .error_message = response.error,
                .response_metadata = response.response_headers,
                .trailers = response.trailers,
            };
        }

        const auto grpc_status = grpc_status_from_http_status(response.status_code);
        if (grpc_status != GrpcStatusCode::Ok) {
            return GrpcJsonTranscodingResponse{
                .status_code = grpc_status,
                .body = response.body,
                .error_message = std::string("gRPC ") + grpc_status_name(grpc_status) + " (HTTP " +
                                 std::to_string(response.status_code) + ")",
                .response_metadata = response.response_headers,
                .trailers = response.trailers,
            };
        }

        return GrpcJsonTranscodingResponse{
            .status_code = GrpcStatusCode::Ok,
            .body = response.body,
            .error_message = {},
            .response_metadata = response.response_headers,
            .trailers = response.trailers,
        };
    }
};

} // namespace

CapabilityTransportAdapterPtr default_capability_transport_adapter() {
    static const auto adapter = std::make_shared<DefaultCapabilityTransportAdapter>();
    return adapter;
}

} // namespace ahfl::runtime
