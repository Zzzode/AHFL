#include "runtime/engine/capability_transport_adapter.hpp"

#include <memory>

namespace ahfl::runtime {
namespace {

class DefaultCapabilityTransportAdapter final : public CapabilityTransportAdapter {
  public:
    [[nodiscard]] HttpResponse execute_http(const HttpRequest &request) const override {
        HttpTransport transport;
        return transport.execute(request);
    }

    [[nodiscard]] GrpcJsonTranscodingResponse
    execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request) const override {
        return ahfl::runtime::execute_grpc_json_transcoding(request);
    }
};

} // namespace

CapabilityTransportAdapterPtr default_capability_transport_adapter() {
    static const auto adapter = std::make_shared<DefaultCapabilityTransportAdapter>();
    return adapter;
}

} // namespace ahfl::runtime
