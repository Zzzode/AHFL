#include "runtime/engine/capability_bridge.hpp"

#include "runtime/engine/connection_pool.hpp"
#include "runtime/engine/grpc_transport.hpp"
#include "runtime/engine/http_transport.hpp"
#include "runtime/engine/wire_value.hpp"

#include <memory>
#include <thread>
#include <utility>

namespace ahfl::runtime {

// ============================================================================
// CircuitBreakerState
// ============================================================================

CircuitBreakerState::State CircuitBreakerState::current_state() {
    std::lock_guard lock(mutex_);
    if (state_ == State::Closed) {
        return State::Closed;
    }
    // Check if recovery window has elapsed
    const auto now = std::chrono::steady_clock::now();
    if (now - opened_at_ >= config_.recovery_window) {
        state_ = State::HalfOpen;
        return State::HalfOpen;
    }
    return State::Open;
}

void CircuitBreakerState::record_success() {
    std::lock_guard lock(mutex_);
    failure_count_ = 0;
    state_ = State::Closed;
}

void CircuitBreakerState::record_failure() {
    std::lock_guard lock(mutex_);
    ++failure_count_;
    if (failure_count_ >= config_.failure_threshold) {
        state_ = State::Open;
        opened_at_ = std::chrono::steady_clock::now();
    }
}

// ============================================================================
// CapabilityRegistry
// ============================================================================

void CapabilityRegistry::register_capability(CapabilityBinding binding) {
    auto name = binding.name;
    bindings_.emplace(std::move(name), std::move(binding));
}

void CapabilityRegistry::register_function(
    const std::string &name, std::function<Value(const std::vector<Value> &args)> fn) {
    CapabilityBinding binding;
    binding.name = name;
    binding.handler = [f = std::move(fn)](const std::vector<Value> &args) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = f(args),
            .error_message = {},
            .attempts = 1,
        };
    };
    bindings_.emplace(name, std::move(binding));
}

void CapabilityRegistry::register_mock(const std::string &name, Value mock_result) {
    CapabilityBinding binding;
    binding.name = name;
    binding.handler = [result = std::make_shared<Value>(std::move(mock_result))](
                          const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = evaluator::clone_value(*result),
            .error_message = {},
            .attempts = 1,
        };
    };
    bindings_.emplace(name, std::move(binding));
}

CapabilityCallResult CapabilityRegistry::invoke(const std::string &name,
                                                const std::vector<Value> &args) {
    auto it = bindings_.find(name);
    if (it == bindings_.end()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "capability not found: " + name,
            .attempts = 0,
        };
    }
    return invoke_with_retry(it->second, args);
}

bool CapabilityRegistry::has(const std::string &name) const {
    return bindings_.find(name) != bindings_.end();
}

std::vector<std::string> CapabilityRegistry::registered_names() const {
    std::vector<std::string> names;
    names.reserve(bindings_.size());
    for (const auto &[key, _] : bindings_) {
        names.push_back(key);
    }
    return names;
}

CapabilityInvoker CapabilityRegistry::as_invoker() {
    auto *self = this;
    return [self](const std::string &name, const std::vector<Value> &args) -> CapabilityCallResult {
        return self->invoke(name, args);
    };
}

CapabilityCallResult CapabilityRegistry::invoke_with_retry(CapabilityBinding &binding,
                                                           const std::vector<Value> &args) {
    // Circuit breaker check
    if (binding.circuit_breaker.enabled && binding.circuit_state) {
        const auto cb_state = binding.circuit_state->current_state();
        if (cb_state == CircuitBreakerState::State::Open) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::CircuitOpen,
                .value = std::nullopt,
                .error_message = "circuit breaker is open for: " + binding.name,
                .attempts = 0,
            };
        }
    }

    const std::size_t max_attempts = binding.retry.max_retries + 1;
    CapabilityCallResult last_result{};

    for (std::size_t attempt = 1; attempt <= max_attempts; ++attempt) {
        last_result = binding.handler(args);
        last_result.attempts = attempt;

        if (last_result.status == CapabilityCallStatus::Success) {
            if (binding.circuit_breaker.enabled && binding.circuit_state) {
                binding.circuit_state->record_success();
            }
            return last_result;
        }

        // Exponential backoff before next attempt
        if (attempt < max_attempts) {
            auto delay = binding.retry.initial_delay;
            for (std::size_t i = 1; i < attempt; ++i) {
                delay = std::chrono::milliseconds(static_cast<long long>(
                    static_cast<double>(delay.count()) * binding.retry.backoff_multiplier));
            }
            if (delay.count() > 0) {
                std::this_thread::sleep_for(delay);
            }
        }
    }

    // Record failure for circuit breaker
    if (binding.circuit_breaker.enabled && binding.circuit_state) {
        binding.circuit_state->record_failure();
    }

    // 只有当实际进行了重试（max_retries > 0）时，才标记为 RetryExhausted
    if (binding.retry.max_retries > 0) {
        last_result.status = CapabilityCallStatus::RetryExhausted;
    }
    return last_result;
}

// ============================================================================
// HTTP Capability (real implementation)
// ============================================================================

namespace {

/// Global connection pool instance (concurrency limiter).
ConnectionPool &global_connection_pool() {
    static ConnectionPool pool;
    return pool;
}

/// Extract host from URL (simplified: between "://" and next "/" or end).
[[nodiscard]] std::string extract_host(const std::string &url) {
    auto scheme_end = url.find("://");
    std::size_t host_start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
    auto host_end = url.find('/', host_start);
    if (host_end == std::string::npos) {
        host_end = url.size();
    }
    return url.substr(host_start, host_end - host_start);
}

} // namespace

CapabilityBinding make_http_capability(const std::string &name, HTTPCapabilityConfig config) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.circuit_breaker = config.circuit_breaker;

    if (config.circuit_breaker.enabled) {
        binding.circuit_state = std::make_shared<CircuitBreakerState>(config.circuit_breaker);
    }

    auto shared_config = std::make_shared<HTTPCapabilityConfig>(std::move(config));

    binding.handler = [shared_config](const std::vector<Value> &args) -> CapabilityCallResult {
        const auto host = extract_host(shared_config->url);
        auto &pool = global_connection_pool();

        if (!pool.can_connect(host)) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "connection pool exhausted for host: " + host,
                .attempts = 1,
            };
        }

        pool.record_start(host);

        HttpTransport transport;

        HttpRequest request;
        request.url = shared_config->url;
        request.method = shared_config->method;
        request.headers = shared_config->headers;
        request.body = serialize_args_for_wire_json(args);
        request.timeout_seconds = static_cast<int>(shared_config->timeout.deadline.count() / 1000);

        // Ensure Content-Type is set
        if (request.headers.find("Content-Type") == request.headers.end()) {
            request.headers["Content-Type"] = "application/json";
        }

        const auto response = transport.execute(request);

        // Always release the pool slot after the HTTP call completes.
        pool.record_end(host);

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
                .error_message =
                    "HTTP " + std::to_string(response.status_code) + ": " + response.body,
                .attempts = 1,
            };
        }

        // Parse response body as Value
        auto parsed = parse_value_from_wire_json(response.body);
        if (!parsed.has_value()) {
            // If body is empty or not valid JSON, return None
            if (response.body.empty()) {
                return CapabilityCallResult{
                    .status = CapabilityCallStatus::Success,
                    .value = evaluator::make_none(),
                    .error_message = {},
                    .attempts = 1,
                };
            }
            // Return raw body as string value
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = Value{evaluator::StringValue{response.body}},
                .error_message = {},
                .attempts = 1,
            };
        }

        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = std::move(*parsed),
            .error_message = {},
            .attempts = 1,
        };
    };

    return binding;
}

// ============================================================================
// gRPC Capability (JSON Transcoding mode via HTTP/2)
// ============================================================================

namespace {

/// Parse endpoint string into host and port.
/// Accepts formats: "host:port", "http://host:port", "https://host:port"
struct ParsedEndpoint {
    std::string host;
    uint16_t port{50051};
    bool use_tls{false};
};

[[nodiscard]] ParsedEndpoint parse_grpc_endpoint(const std::string &endpoint) {
    ParsedEndpoint result;
    std::string working = endpoint;

    // Strip scheme if present
    if (working.rfind("https://", 0) == 0) {
        result.use_tls = true;
        working = working.substr(8);
    } else if (working.rfind("http://", 0) == 0) {
        result.use_tls = false;
        working = working.substr(7);
    }

    // Split host:port
    auto colon_pos = working.rfind(':');
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

} // namespace

CapabilityBinding make_grpc_capability(const std::string &name, GRPCCapabilityConfig config) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.circuit_breaker = config.circuit_breaker;

    if (config.circuit_breaker.enabled) {
        binding.circuit_state = std::make_shared<CircuitBreakerState>(config.circuit_breaker);
    }

    auto shared_config = std::make_shared<GRPCCapabilityConfig>(std::move(config));

    binding.handler = [shared_config](const std::vector<Value> &args) -> CapabilityCallResult {
        // Parse endpoint into structured form
        auto parsed = parse_grpc_endpoint(shared_config->endpoint);

        const auto host_key = parsed.host + ":" + std::to_string(parsed.port);
        auto &pool = global_connection_pool();

        if (!pool.can_connect(host_key)) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "connection pool exhausted for gRPC host: " + host_key,
                .attempts = 1,
            };
        }

        pool.record_start(host_key);

        // Build gRPC request
        GrpcEndpoint endpoint;
        endpoint.host = parsed.host;
        endpoint.port = parsed.port;
        endpoint.service_name = shared_config->service;
        endpoint.method_name = shared_config->method;
        endpoint.use_tls = parsed.use_tls;

        GrpcRequest grpc_request;
        grpc_request.endpoint = std::move(endpoint);
        grpc_request.serialized_body = serialize_args_for_grpc(args);
        grpc_request.timeout =
            std::chrono::duration_cast<std::chrono::seconds>(shared_config->timeout.deadline);

        // Add metadata from config (future extension point)
        // grpc_request.metadata = ...

        const auto response = execute_grpc(grpc_request);

        pool.record_end(host_key);

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

        // Parse response body as Value
        auto parsed_value = parse_value_from_wire_json(response.body);
        if (!parsed_value.has_value()) {
            if (response.body.empty()) {
                return CapabilityCallResult{
                    .status = CapabilityCallStatus::Success,
                    .value = evaluator::make_none(),
                    .error_message = {},
                    .attempts = 1,
                };
            }
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = Value{evaluator::StringValue{response.body}},
                .error_message = {},
                .attempts = 1,
            };
        }

        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = std::move(*parsed_value),
            .error_message = {},
            .attempts = 1,
        };
    };

    return binding;
}

} // namespace ahfl::runtime
