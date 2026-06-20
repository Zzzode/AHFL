#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/types.hpp"
#include "runtime/engine/capability_transport_adapter.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/providers/secret/auth_provider.hpp"

namespace ahfl::runtime {

using evaluator::Value;

struct CapabilityInvocationContext {
    std::string workflow_name;
    std::string workflow_node_name;
    std::string agent_name;
    std::string state_name;
    std::size_t workflow_node_execution_index{0};
    bool has_workflow_node_context{false};
};

// Capability call status
enum class CapabilityCallStatus {
    Success,
    Error,
    Timeout,
    RetryExhausted,
    CircuitOpen,
};

// Capability call result
struct CapabilityCallResult {
    CapabilityCallStatus status{CapabilityCallStatus::Error};
    std::optional<Value> value;
    std::string error_message;
    std::size_t attempts{1};
};

using CapabilityInvoker =
    std::function<CapabilityCallResult(const std::string &name, const std::vector<Value> &args)>;
using ContextualCapabilityInvoker =
    std::function<CapabilityCallResult(const CapabilityInvocationContext &context,
                                       const std::string &name,
                                       const std::vector<Value> &args)>;

enum class CapabilityResponseFormat {
    Json,
    TextPlain,
};

// Retry configuration
struct RetryConfig {
    std::size_t max_retries{0};
    std::chrono::milliseconds initial_delay{100};
    double backoff_multiplier{2.0};
};

// Timeout configuration
struct TimeoutConfig {
    std::chrono::milliseconds deadline{30000};
};

// Circuit breaker configuration
struct CircuitBreakerConfig {
    std::size_t failure_threshold{5};
    std::chrono::seconds recovery_window{30};
    bool enabled{false};
};

// Circuit breaker state (thread-safe, shared with handler lambdas via shared_ptr)
class CircuitBreakerState {
  public:
    enum class State {
        Closed,
        Open,
        HalfOpen
    };

    explicit CircuitBreakerState(CircuitBreakerConfig config) : config_(config) {}

    [[nodiscard]] State current_state();
    void record_success();
    void record_failure();

  private:
    CircuitBreakerConfig config_;
    std::mutex mutex_;
    State state_{State::Closed};
    std::size_t failure_count_{0};
    std::chrono::steady_clock::time_point opened_at_{};
};

// Binding definition for a single capability
struct CapabilityBinding {
    std::string name;
    std::function<CapabilityCallResult(const std::vector<Value> &args)> handler;
    RetryConfig retry;
    TimeoutConfig timeout;
    CircuitBreakerConfig circuit_breaker;
    std::shared_ptr<CircuitBreakerState> circuit_state;
};

// Capability registry
class CapabilityRegistry {
  public:
    void register_capability(CapabilityBinding binding);
    void register_function(const std::string &name,
                           std::function<Value(const std::vector<Value> &args)> fn);
    void register_mock(const std::string &name, Value mock_result);

    [[nodiscard]] CapabilityCallResult invoke(const std::string &name,
                                              const std::vector<Value> &args);
    [[nodiscard]] CapabilityCallResult
    invoke_with_context(const CapabilityInvocationContext &context,
                        const std::string &name,
                        const std::vector<Value> &args);
    [[nodiscard]] bool has(const std::string &name) const;
    [[nodiscard]] std::vector<std::string> registered_names() const;
    [[nodiscard]] CapabilityInvoker as_invoker();
    [[nodiscard]] ContextualCapabilityInvoker as_contextual_invoker();

  private:
    std::unordered_map<std::string, CapabilityBinding> bindings_;

    [[nodiscard]] CapabilityCallResult invoke_with_retry(CapabilityBinding &binding,
                                                         const std::vector<Value> &args);
};

// HTTP capability factory
struct HTTPCapabilityConfig {
    std::string url;
    std::string method{"POST"};
    std::unordered_map<std::string, std::string> headers;
    CapabilityResponseFormat response_format{CapabilityResponseFormat::Json};
    RetryConfig retry;
    TimeoutConfig timeout;
    CircuitBreakerConfig circuit_breaker;
    std::optional<ahfl::secret::AuthConfig> auth;
    std::shared_ptr<ahfl::secret::SecretManager> secret_manager;
    std::shared_ptr<const ir::TypeRef> response_schema;
};
[[nodiscard]] CapabilityBinding make_http_capability(const std::string &name,
                                                     HTTPCapabilityConfig config);
[[nodiscard]] CapabilityBinding make_http_capability(const std::string &name,
                                                     HTTPCapabilityConfig config,
                                                     CapabilityTransportAdapterPtr transport);

// gRPC-shaped Capability factory backed by HTTP/2 JSON transcoding.
struct GrpcJsonTranscodingCapabilityConfig {
    std::string endpoint;
    std::string service;
    std::string method;
    CapabilityResponseFormat response_format{CapabilityResponseFormat::Json};
    RetryConfig retry;
    TimeoutConfig timeout;
    CircuitBreakerConfig circuit_breaker;
    std::optional<ahfl::secret::AuthConfig> auth;
    std::shared_ptr<ahfl::secret::SecretManager> secret_manager;
    std::shared_ptr<const ir::TypeRef> response_schema;
};
[[nodiscard]] CapabilityBinding
make_grpc_json_transcoding_capability(const std::string &name,
                                      GrpcJsonTranscodingCapabilityConfig config);
[[nodiscard]] CapabilityBinding
make_grpc_json_transcoding_capability(const std::string &name,
                                      GrpcJsonTranscodingCapabilityConfig config,
                                      CapabilityTransportAdapterPtr transport);

} // namespace ahfl::runtime
