#include "runtime/engine/capability_bridge.hpp"

#include "runtime/engine/connection_pool.hpp"
#include "runtime/engine/wire_value.hpp"

#include <memory>
#include <utility>

namespace ahfl::runtime {
namespace {

ConnectionPool &global_connection_pool() {
    static ConnectionPool pool;
    return pool;
}

[[nodiscard]] std::string extract_host(const std::string &url) {
    const auto scheme_end = url.find("://");
    const std::size_t host_start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
    auto host_end = url.find('/', host_start);
    if (host_end == std::string::npos) {
        host_end = url.size();
    }
    return url.substr(host_start, host_end - host_start);
}

[[nodiscard]] CapabilityCallResult value_from_wire_response_body(std::string body) {
    auto parsed = parse_value_from_wire_json(body);
    if (parsed.has_value()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = std::move(*parsed),
            .error_message = {},
            .attempts = 1,
        };
    }

    if (body.empty()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = evaluator::make_none(),
            .error_message = {},
            .attempts = 1,
        };
    }

    return CapabilityCallResult{
        .status = CapabilityCallStatus::Success,
        .value = Value{evaluator::StringValue{std::move(body)}},
        .error_message = {},
        .attempts = 1,
    };
}

[[nodiscard]] CapabilityCallResult
execute_http_capability_call(const HTTPCapabilityConfig &config,
                             const CapabilityTransportAdapter &transport,
                             const std::vector<Value> &args) {
    const auto host = extract_host(config.url);
    auto lease = global_connection_pool().try_acquire(host);
    if (!lease.acquired()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "connection pool exhausted for host: " + host,
            .attempts = 1,
        };
    }

    HttpRequest request;
    request.url = config.url;
    request.method = config.method;
    request.headers = config.headers;
    request.body = serialize_args_for_wire_json(args);
    request.timeout_seconds = static_cast<int>(config.timeout.deadline.count() / 1000);

    if (request.headers.find("Content-Type") == request.headers.end()) {
        request.headers["Content-Type"] = "application/json";
    }

    const auto response = transport.execute_http(request);

    if (response.is_timeout()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Timeout,
            .value = std::nullopt,
            .error_message = "HTTP request timed out",
            .attempts = 1,
        };
    }

    if (!response.error.empty() && response.status_code == 0) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "HTTP transport error: " + response.error,
            .attempts = 1,
        };
    }

    if (!response.is_success()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "HTTP " + std::to_string(response.status_code) + ": " + response.body,
            .attempts = 1,
        };
    }

    return value_from_wire_response_body(response.body);
}

struct ParsedGrpcJsonTranscodingEndpoint {
    std::string host;
    uint16_t port{50051};
    bool use_tls{false};
};

[[nodiscard]] ParsedGrpcJsonTranscodingEndpoint
parse_grpc_json_transcoding_endpoint(const std::string &endpoint) {
    ParsedGrpcJsonTranscodingEndpoint result;
    std::string working = endpoint;

    if (working.rfind("https://", 0) == 0) {
        result.use_tls = true;
        working = working.substr(8);
    } else if (working.rfind("http://", 0) == 0) {
        result.use_tls = false;
        working = working.substr(7);
    }

    const auto colon_pos = working.rfind(':');
    if (colon_pos != std::string::npos) {
        result.host = working.substr(0, colon_pos);
        try {
            result.port = static_cast<uint16_t>(std::stoi(working.substr(colon_pos + 1)));
        } catch (...) {
            result.port =
                result.use_tls ? static_cast<uint16_t>(443) : static_cast<uint16_t>(50051);
        }
    } else {
        result.host = working;
        result.port = result.use_tls ? static_cast<uint16_t>(443) : static_cast<uint16_t>(50051);
    }

    return result;
}

[[nodiscard]] CapabilityCallResult
execute_grpc_json_transcoding_capability_call(const GrpcJsonTranscodingCapabilityConfig &config,
                                              const CapabilityTransportAdapter &transport,
                                              const std::vector<Value> &args) {
    const auto parsed = parse_grpc_json_transcoding_endpoint(config.endpoint);
    const auto host_key = parsed.host + ":" + std::to_string(parsed.port);
    auto lease = global_connection_pool().try_acquire(host_key);
    if (!lease.acquired()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "connection pool exhausted for gRPC host: " + host_key,
            .attempts = 1,
        };
    }

    GrpcJsonTranscodingEndpoint endpoint;
    endpoint.host = parsed.host;
    endpoint.port = parsed.port;
    endpoint.service_name = config.service;
    endpoint.method_name = config.method;
    endpoint.use_tls = parsed.use_tls;

    GrpcJsonTranscodingRequest request;
    request.endpoint = std::move(endpoint);
    request.serialized_body = serialize_args_for_grpc_json_transcoding(args);
    request.timeout = std::chrono::duration_cast<std::chrono::seconds>(config.timeout.deadline);

    const auto response = transport.execute_grpc_json_transcoding(request);

    if (response.status_code == GrpcStatusCode::DeadlineExceeded) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Timeout,
            .value = std::nullopt,
            .error_message = response.error_message,
            .attempts = 1,
        };
    }

    if (!response.is_ok()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = response.error_message,
            .attempts = 1,
        };
    }

    return value_from_wire_response_body(response.body);
}

} // namespace

CapabilityBinding make_http_capability(const std::string &name, HTTPCapabilityConfig config) {
    return make_http_capability(name, std::move(config), default_capability_transport_adapter());
}

CapabilityBinding make_http_capability(const std::string &name,
                                       HTTPCapabilityConfig config,
                                       CapabilityTransportAdapterPtr transport) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.circuit_breaker = config.circuit_breaker;

    if (config.circuit_breaker.enabled) {
        binding.circuit_state = std::make_shared<CircuitBreakerState>(config.circuit_breaker);
    }

    auto shared_config = std::make_shared<HTTPCapabilityConfig>(std::move(config));
    auto shared_transport =
        transport != nullptr ? std::move(transport) : default_capability_transport_adapter();
    binding.handler = [shared_config,
                       shared_transport](const std::vector<Value> &args) -> CapabilityCallResult {
        return execute_http_capability_call(*shared_config, *shared_transport, args);
    };
    return binding;
}

CapabilityBinding
make_grpc_json_transcoding_capability(const std::string &name,
                                      GrpcJsonTranscodingCapabilityConfig config) {
    return make_grpc_json_transcoding_capability(
        name, std::move(config), default_capability_transport_adapter());
}

CapabilityBinding make_grpc_json_transcoding_capability(const std::string &name,
                                                        GrpcJsonTranscodingCapabilityConfig config,
                                                        CapabilityTransportAdapterPtr transport) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.circuit_breaker = config.circuit_breaker;

    if (config.circuit_breaker.enabled) {
        binding.circuit_state = std::make_shared<CircuitBreakerState>(config.circuit_breaker);
    }

    auto shared_config = std::make_shared<GrpcJsonTranscodingCapabilityConfig>(std::move(config));
    auto shared_transport =
        transport != nullptr ? std::move(transport) : default_capability_transport_adapter();
    binding.handler = [shared_config,
                       shared_transport](const std::vector<Value> &args) -> CapabilityCallResult {
        return execute_grpc_json_transcoding_capability_call(
            *shared_config, *shared_transport, args);
    };
    return binding;
}

} // namespace ahfl::runtime
