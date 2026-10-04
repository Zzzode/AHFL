#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/types.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "runtime/engine/capability_transport_adapter.hpp"
#include "runtime/value/value.hpp"
#include "runtime/providers/secret/auth_provider.hpp"

namespace ahfl::runtime {


struct CapabilityInvocationContext {
    std::string workflow_name;
    std::string workflow_node_name;
    std::string agent_name;
    std::string state_name;
    std::size_t workflow_node_execution_index{0};
    bool has_workflow_node_context{false};
    RunId run_id;
    WorkflowId workflow_id;
    WorkflowNodeId workflow_node_id;
    AgentId agent_id;
    AgentStateId agent_state_id{};
    CapabilityId capability_id{};
    InvocationId invocation_id{};
    std::optional<std::size_t> source_capability_symbol_id{};
    // RFC 0022 slice 4 (exactly-once): a stable per-invocation idempotency key,
    // hash(workflow_id, node_id, per-node ordinal, cap_id, arg_hash). Reproducible
    // across resume so a host can dedup a `durable_write` / `financial_write`
    // effect that committed before a crash. 0 when no node context is present.
    std::uint64_t idempotency_key{0};
};

// Capability call status
enum class CapabilityCallStatus {
    Success,
    Error,
    Timeout,
    RetryExhausted,
    CircuitOpen,
    // RFC 0022 (durable resume): the host accepted the call but the result is
    // not yet available; the workflow must suspend at its current node and be
    // resumed later with the result. Distinct from Error so the runtime can
    // persist a resume record rather than terminate the workflow.
    Pending,
};

/// Stable lowercase name for a CapabilityCallStatus, for diagnostics shared by
/// the wasm bridge/opaque import lanes.
[[nodiscard]] std::string_view
capability_call_status_name(CapabilityCallStatus status) noexcept;

/// Classify a capability call status into a CapabilityFailureKind for the
/// CapabilityFailed event. Relocated out of the old evaluator-driven workflow
/// TU's anonymous namespace (WH-5c.8; that TU was deleted in WH-9) so the
/// shared capability event projection helper uses one definition. Pending is
/// not a failure (handled on the suspend path before failure classification);
/// reaching here with Pending means a
/// misrouted result, classified as Error (fail-closed) rather than asserted.
[[nodiscard]] inline CapabilityFailureKind
capability_failure_kind(CapabilityCallStatus status) noexcept {
    switch (status) {
    case CapabilityCallStatus::Success:
    case CapabilityCallStatus::Error:
    case CapabilityCallStatus::CircuitOpen:
        return CapabilityFailureKind::Error;
    case CapabilityCallStatus::Timeout:
        return CapabilityFailureKind::Timeout;
    case CapabilityCallStatus::RetryExhausted:
        return CapabilityFailureKind::RetryExhausted;
    case CapabilityCallStatus::Pending:
        return CapabilityFailureKind::Error;
    }
    return CapabilityFailureKind::Error;
}

struct CapabilityUsage {
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double total_cost_usd{0.0};
    bool cost_estimated{false};
    std::vector<CapabilityPolicyNotice> notices{};
};

// Capability call result
struct CapabilityCallResult {
    CapabilityCallStatus status{CapabilityCallStatus::Error};
    std::optional<Value> value{};
    std::string error_message{};
    std::size_t attempts{1};
    std::optional<CapabilityFailureKind> failure_kind{};
    bool provider_degraded{false};
    std::string degraded_provider_name{};
    std::string selected_provider_name{};
    bool cache_hit{false};
    std::string diagnostic_code{};
    std::optional<CapabilityUsage> usage{};
    // RFC 0022 slice 3: set only when status == Pending. Identify the pending
    // call so the CallEvalFn boundary can build an EvalResult::suspension
    // (index/id-based: cap_id is a SymbolId, ordinal a per-node counter).
    std::size_t pending_cap_id{0};
    std::uint64_t pending_ordinal{0};
};

// Deep-clone a CapabilityCallResult (Rust `Clone::clone` pattern): Value is
// non-copyable (MapValue holds unique_ptr pairs), so the implicit copy is
// deleted. This explicit clone snapshots the optional output value so callers
// can keep an independent copy (the wasm lanes collect a per-call copy for
// event projection). The struct stays an aggregate so designated
// initializers at the ~40 construction sites keep working.
[[nodiscard]] CapabilityCallResult
clone_capability_call_result(const CapabilityCallResult &other);

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
    // RFC 0026 C2b G4a: the projected, verified wire-schema binding for this
    // capability's response. When engaged it is the response decode/validate
    // authority (exact codec path); `response_schema` above is the legacy TypeRef
    // input to the admission gate (prepare_wire_response_schema). The two are
    // mutually exclusive — supplying both is a fail-closed conflict.
    std::optional<ir::core::VerifiedWireSchemaBinding> response_wire_binding;
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
    // RFC 0026 C2b G4a: see HTTPCapabilityConfig::response_wire_binding.
    std::optional<ir::core::VerifiedWireSchemaBinding> response_wire_binding;
};
[[nodiscard]] CapabilityBinding
make_grpc_json_transcoding_capability(const std::string &name,
                                      GrpcJsonTranscodingCapabilityConfig config);
[[nodiscard]] CapabilityBinding
make_grpc_json_transcoding_capability(const std::string &name,
                                      GrpcJsonTranscodingCapabilityConfig config,
                                      CapabilityTransportAdapterPtr transport);

} // namespace ahfl::runtime
