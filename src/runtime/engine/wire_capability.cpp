#include "runtime/engine/capability_bridge.hpp"

#include "ahfl/compiler/ir/ir.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/connection_pool.hpp"
#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/wire_capability_admission.hpp"
#include "runtime/engine/wire_value.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

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

// RFC 0026 C2b G4a: decode a wire response body into a native Value.
//
// When `binding` is engaged the body is decoded EXACTLY under the projected wire
// schema (no schema-free value_from_json / no legacy TypeRef validator):
//   - Json format:     the body is parsed to a raw JSON DOM and fed to
//                      decode_json(dom, binding); an empty or malformed body is a
//                      hard error (never a silent None).
//   - TextPlain format: the body (INCLUDING the empty string) is a PRESENT String;
//                      it is decoded as a JSON String DOM under the binding, whose
//                      verified root is guaranteed to be String/BoundedString by
//                      the admission gate, so String length_bounds are enforced.
//
// When `binding` is absent the historical schema-free behavior is preserved exactly
// (TextPlain: empty -> None, else StringValue; Json: value_from_json, empty -> None,
// else "invalid wire JSON response body"). Diagnostics are schema-only and never
// echo the response body.
[[nodiscard]] CapabilityCallResult
value_from_wire_response_body(std::string body,
                              CapabilityResponseFormat response_format,
                              const std::optional<ir::core::VerifiedWireSchemaBinding> &binding) {
    if (binding.has_value()) {
        if (response_format == CapabilityResponseFormat::TextPlain) {
            // The verified root is String/BoundedString (admission gate); the text
            // body (empty included) is a PRESENT String value. Build the String DOM
            // node directly so bounds are enforced by the codec.
            ahfl::json::JsonValue dom;
            dom.kind = ahfl::json::Kind::String;
            dom.string_val = std::move(body);
            auto decoded = wire_codec::decode_json(dom, *binding);
            if (!decoded.ok()) {
                return CapabilityCallResult{
                    .status = CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "response schema validation failed: " + decoded.error,
                    .attempts = 1,
                };
            }
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = std::move(*decoded.value),
                .error_message = {},
                .attempts = 1,
            };
        }
        auto parsed = ahfl::json::parse_json(body);
        if (!parsed.has_value() || !*parsed) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "invalid wire JSON response body",
                .attempts = 1,
            };
        }
        auto decoded = wire_codec::decode_json(**parsed, *binding);
        if (!decoded.ok()) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "response schema validation failed: " + decoded.error,
                .attempts = 1,
            };
        }
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = std::move(*decoded.value),
            .error_message = {},
            .attempts = 1,
        };
    }

    // No binding: preserve the historical schema-free behavior verbatim.
    if (response_format == CapabilityResponseFormat::TextPlain) {
        if (body.empty()) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = runtime::make_none(),
                .error_message = {},
                .attempts = 1,
            };
        }
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = Value{runtime::StringValue{std::move(body)}},
            .error_message = {},
            .attempts = 1,
        };
    }

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
            .value = runtime::make_none(),
            .error_message = {},
            .attempts = 1,
        };
    }

    return CapabilityCallResult{
        .status = CapabilityCallStatus::Error,
        .value = std::nullopt,
        .error_message = "invalid wire JSON response body",
        .attempts = 1,
    };
}

// RFC 0026 C2b G4a: a factory-minted "poison" binding whose handler returns a fixed
// construction/config error WITHOUT touching transport. Pins retry.max_retries=0 and
// disables the circuit breaker on the CapabilityBinding so invoke_with_retry runs
// exactly one attempt and never rewrites the status to RetryExhausted / records a
// circuit-breaker failure — a pre-transport construction error must surface as-is.
[[nodiscard]] CapabilityBinding poison_capability_binding(const std::string &name,
                                                          std::string error_message) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = RetryConfig{.max_retries = 0};
    binding.circuit_breaker = CircuitBreakerConfig{.enabled = false};
    binding.handler = [error_message = std::move(error_message)](
                          const std::vector<Value> &) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = error_message,
            .attempts = 1,
        };
    };
    return binding;
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

    if (config.auth.has_value() && !config.secret_manager) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "HTTP capability auth failed: secret manager is required",
            .attempts = 1,
        };
    }

    if (config.auth.has_value()) {
        auto resolved = ahfl::secret::resolve_auth(*config.auth, *config.secret_manager);
        if (!resolved.success) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "HTTP capability auth failed: " + resolved.error_message,
                .attempts = 1,
            };
        }
        for (auto &[key, value] : resolved.headers) {
            request.headers.insert_or_assign(std::move(key), std::move(value));
        }
        request.tls_client_certificate_path = std::move(resolved.tls_client_certificate_path);
        request.tls_client_key_path = std::move(resolved.tls_client_key_path);
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

    auto result = value_from_wire_response_body(
        response.body, config.response_format, config.response_wire_binding);
    return result;
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

    if (config.auth.has_value() && !config.secret_manager) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "gRPC capability auth failed: secret manager is required",
            .attempts = 1,
        };
    }

    if (config.auth.has_value()) {
        auto resolved = ahfl::secret::resolve_auth(*config.auth, *config.secret_manager);
        if (!resolved.success) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "gRPC capability auth failed: " + resolved.error_message,
                .attempts = 1,
            };
        }
        for (auto &[key, value] : resolved.headers) {
            request.metadata.emplace_back(std::move(key), std::move(value));
        }
        request.tls_client_certificate_path = std::move(resolved.tls_client_certificate_path);
        request.tls_client_key_path = std::move(resolved.tls_client_key_path);
    }

    auto response = transport.execute_grpc_json_transcoding(request);

    // If trailers carry an explicit grpc-status, it takes precedence over the
    // HTTP-derived status code. Some intermediaries put grpc-status in the
    // initial metadata for empty responses, so metadata is the fallback source.
    if (!response.trailers.empty()) {
        const auto trailer_status = parse_grpc_status_from_headers(response.trailers);
        if (trailer_status != GrpcStatusCode::Ok) {
            response.status_code = trailer_status;
            auto trailer_message = parse_grpc_message_from_headers(response.trailers);
            if (!trailer_message.empty()) {
                response.error_message = std::move(trailer_message);
            }
        }
    }
    if (response.status_code == GrpcStatusCode::Ok && !response.response_metadata.empty()) {
        const auto metadata_status = parse_grpc_status_from_headers(response.response_metadata);
        if (metadata_status != GrpcStatusCode::Ok) {
            response.status_code = metadata_status;
            auto metadata_message = parse_grpc_message_from_headers(response.response_metadata);
            if (!metadata_message.empty()) {
                response.error_message = std::move(metadata_message);
            }
        }
    }

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

    auto result = value_from_wire_response_body(
        response.body, config.response_format, config.response_wire_binding);
    return result;
}

} // namespace

CapabilityBinding make_http_capability(const std::string &name, HTTPCapabilityConfig config) {
    return make_http_capability(name, std::move(config), default_capability_transport_adapter());
}

CapabilityBinding make_http_capability(const std::string &name,
                                       HTTPCapabilityConfig config,
                                       CapabilityTransportAdapterPtr transport) {
    // RFC 0026 C2b G4a: resolve the response schema authority at construction. On a
    // four-state conflict / non-projectable legacy schema / TextPlain non-String
    // root, mint a poison binding that fails closed BEFORE any transport (no pool
    // lease, no auth, no request), and never becomes RetryExhausted.
    auto admission = prepare_wire_response_schema(
        config.response_format, config.response_schema, config.response_wire_binding);
    if (!admission.has_value()) {
        return poison_capability_binding(name,
                                         "response schema admission failed: " + admission.error());
    }
    config.response_wire_binding = std::move(*admission);
    config.response_schema.reset(); // the resolved binding is the sole authority now

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
    // RFC 0026 C2b G4a: same construction-time admission + poison as HTTP.
    auto admission = prepare_wire_response_schema(
        config.response_format, config.response_schema, config.response_wire_binding);
    if (!admission.has_value()) {
        return poison_capability_binding(name,
                                         "response schema admission failed: " + admission.error());
    }
    config.response_wire_binding = std::move(*admission);
    config.response_schema.reset();

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

WireResponseAdmissionResult prepare_wire_response_schema(
    CapabilityResponseFormat format,
    const std::shared_ptr<const ir::TypeRef> &legacy_schema,
    const std::optional<ir::core::VerifiedWireSchemaBinding> &response_wire_binding) {
    const bool has_legacy = legacy_schema != nullptr;
    const bool has_binding = response_wire_binding.has_value();

    // State 4: both present -> conflict, fail closed (no silent precedence).
    if (has_legacy && has_binding) {
        return std::unexpected(std::string(
            "both a legacy response_schema and a projected response_wire_binding are set"));
    }

    std::optional<ir::core::VerifiedWireSchemaBinding> resolved;
    if (has_binding) {
        // State 2: the projected authority is used verbatim.
        resolved = response_wire_binding;
    } else if (has_legacy) {
        // State 3: migrate the legacy TypeRef through an EMPTY-Program environment.
        // Only declaration-free / closed / fully-parameterized shapes project; any
        // user nominal / open shape / non-projectable type fails closed here.
        ir::Program empty_program;
        auto migration = ir::core::migrate_type_ref_to_wire_binding(*legacy_schema, empty_program);
        if (!migration.ok() || !migration.binding.has_value()) {
            std::string message = "legacy response schema is not projectable";
            for (const auto &d : migration.diagnostics) {
                if (d.severity == ir::core::CoreDiagnosticSeverity::Error) {
                    message += ": " + d.message;
                    break;
                }
            }
            return std::unexpected(std::move(message));
        }
        resolved = std::move(*migration.binding);
    }
    // State 1: neither -> engaged nullopt (caller keeps no-schema behavior).
    if (!resolved.has_value()) {
        return std::optional<ir::core::VerifiedWireSchemaBinding>{std::nullopt};
    }

    // TextPlain gate: a text body can only decode under a String / BoundedString
    // verified root.
    if (format == CapabilityResponseFormat::TextPlain) {
        const auto &table = resolved->table();
        const auto root = resolved->root();
        if (root.value >= table.nodes.size() ||
            !std::holds_alternative<ir::core::CoreWireSchemaString>(
                table.nodes[root.value].shape)) {
            return std::unexpected(
                std::string("TextPlain response requires a String-rooted schema"));
        }
    }
    return resolved;
}

} // namespace ahfl::runtime
