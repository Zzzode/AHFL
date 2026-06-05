#pragma once

#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/http_transport.hpp"

#include <memory>

namespace ahfl::runtime {

class CapabilityTransportAdapter {
  public:
    virtual ~CapabilityTransportAdapter() = default;

    [[nodiscard]] virtual HttpResponse execute_http(const HttpRequest &request) const = 0;
    [[nodiscard]] virtual GrpcJsonTranscodingResponse
    execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request) const = 0;
};

using CapabilityTransportAdapterPtr = std::shared_ptr<const CapabilityTransportAdapter>;

[[nodiscard]] CapabilityTransportAdapterPtr default_capability_transport_adapter();

} // namespace ahfl::runtime
