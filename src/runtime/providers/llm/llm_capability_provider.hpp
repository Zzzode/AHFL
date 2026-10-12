#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/value/value.hpp"
#include "runtime/providers/llm/http_client.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"
#include "runtime/providers/llm/prompt_builder.hpp"
#include "runtime/providers/llm/response_cache.hpp"
#include "runtime/providers/llm/response_parser.hpp"
#include "runtime/providers/llm/tool_calling.hpp"
#include "runtime/providers/llm/llm_session_store.hpp"
#include "runtime/providers/llm/model_capacity.hpp"

namespace ahfl::llm_provider {

class SSEParser;

/// Structured content extracted from an LLM response.
struct LLMResponseContent {
    std::string content;
    std::vector<ToolCall> tool_calls;

    [[nodiscard]] bool has_tool_calls() const {
        return !tool_calls.empty();
    }
};

enum class LLMResponseCacheAuditEventKind {
    Miss,
    Hit,
    Write,
    Invalidated,
    SnapshotLoaded,
    SnapshotPersisted,
    SnapshotLoadFailed,
    SnapshotPersistFailed,
};

struct LLMResponseCacheAuditEvent {
    LLMResponseCacheAuditEventKind kind{LLMResponseCacheAuditEventKind::Miss};
    std::string model{};
    std::string key_digest{};
    std::size_t system_prompt_bytes{0};
    std::size_t user_prompt_bytes{0};
    std::size_t response_bytes{0};
    std::size_t cache_size_after{0};
    std::size_t snapshot_entry_count{0};
    bool persistent{false};
    bool secret_free{true};
};

enum class LLMProviderHealthEventKind {
    ProviderDegraded,
    FallbackSelected,
    FallbackExhausted,
};

struct LLMProviderHealthEvent {
    LLMProviderHealthEventKind kind{LLMProviderHealthEventKind::ProviderDegraded};
    std::string provider_name;
    std::string selected_provider_name;
    int status_code{0};
    std::size_t attempts_after{0};
    std::string message;
    bool secret_free{true};
};

enum class LLMStreamingEventKind {
    Chunk,
    Completed,
    Interrupted,
};

struct LLMStreamingEvent {
    LLMStreamingEventKind kind{LLMStreamingEventKind::Chunk};
    std::string provider_name;
    std::size_t chunk_index{0};
    std::size_t chunk_bytes{0};
    std::size_t total_content_bytes{0};
    bool completed{false};
    bool secret_free{true};
};

struct LLMTokenUsageEvent {
    std::string provider_name{};
    std::string model{};
    std::string workflow_name{};
    std::string workflow_node_name{};
    std::string agent_name{};
    std::string state_name{};
    std::size_t workflow_node_execution_index{0};
    bool has_workflow_node_context{false};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double prompt_cost_usd{0.0};
    double completion_cost_usd{0.0};
    double total_cost_usd{0.0};
    bool cost_estimated{false};
    bool secret_free{true};
};

enum class LLMTokenBudgetEventKind {
    PromptAccepted,
    PromptRejected,
    UsageWithinBudget,
    UsageExceededBudget,
    CostWithinBudget,
    CostExceededBudget,
    WorkflowUsageWithinBudget,
    WorkflowUsageExceededBudget,
    NodeUsageWithinBudget,
    NodeUsageExceededBudget,
    WorkflowCostWithinBudget,
    WorkflowCostExceededBudget,
    NodeCostWithinBudget,
    NodeCostExceededBudget,
};

struct LLMTokenBudgetEvent {
    LLMTokenBudgetEventKind kind{LLMTokenBudgetEventKind::PromptAccepted};
    std::string provider_name{};
    std::string model{};
    std::string capability_name{};
    std::string workflow_name{};
    std::string workflow_node_name{};
    std::string agent_name{};
    std::string state_name{};
    std::size_t workflow_node_execution_index{0};
    bool has_workflow_node_context{false};
    std::size_t max_total_tokens{0};
    std::size_t max_prompt_tokens{0};
    std::size_t max_response_tokens{0};
    std::size_t max_workflow_total_tokens{0};
    std::size_t max_node_total_tokens{0};
    std::size_t effective_prompt_tokens{0};
    std::size_t system_prompt_tokens{0};
    std::size_t user_prompt_tokens_before{0};
    std::size_t user_prompt_tokens_after{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    std::size_t cumulative_workflow_tokens{0};
    std::size_t cumulative_node_tokens{0};
    double max_total_cost_usd{0.0};
    double max_workflow_total_cost_usd{0.0};
    double max_node_total_cost_usd{0.0};
    double total_cost_usd{0.0};
    double cumulative_workflow_cost_usd{0.0};
    double cumulative_node_cost_usd{0.0};
    bool truncated{false};
    std::string policy{"fail"};
    std::string message{};
    std::string diagnostic_code{};
    bool secret_free{true};
};

// LLM Capability Provider: routes AHFL capability calls to the LLM API
class LLMCapabilityProvider {
  public:
    /// A tool executor that can run tools and return results.
    struct ToolExecutionContext {
        std::uint64_t session_id{0};
        std::uint64_t turn_id{0};
        std::uint64_t tool_ordinal{0};
        std::uint64_t definition_id{0};
        runtime::CapabilityInvocationContext outer_context{};
        support::Sha256Digest binding_digest{};
        std::uint64_t idempotency_key{0};
    };
    using ToolExecutor =
        std::function<ToolCallResult(const ToolCall &, const ToolExecutionContext &)>;

    LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config);
    LLMCapabilityProvider(const ir::Program &program,
                          LLMProviderConfig config,
                          HttpClient::RequestTransport transport);

    // Invoke a capability, obtaining results via the LLM
    [[nodiscard]] runtime::CapabilityCallResult invoke(const std::string &capability_name,
                                                       const std::vector<runtime::Value> &args);
    [[nodiscard]] runtime::CapabilityCallResult
    invoke_with_context(const runtime::CapabilityInvocationContext &context,
                        const std::string &capability_name,
                        const std::vector<runtime::Value> &args);
    [[nodiscard]] runtime::ContextualCapabilityInvoker as_contextual_invoker();

    void set_session_store(std::shared_ptr<LLMSessionStore> store);
    [[nodiscard]] std::expected<bool, std::string>
    can_resume(const runtime::CapabilityInvocationContext &context,
               const std::string &name,
               const std::vector<runtime::Value> &args);
    [[nodiscard]] std::expected<std::optional<runtime::CapabilityCallResult>, std::string>
    replay_completed(const runtime::CapabilityInvocationContext &context,
                     const std::string &name,
                     const std::vector<runtime::Value> &args);
    [[nodiscard]] runtime::CapabilityCallResult
    resume_pending(const runtime::CapabilityInvocationContext &context,
                   const std::string &name,
                   const std::vector<runtime::Value> &args);

    // Register all capabilities with the registry
    void register_all(runtime::CapabilityRegistry &registry);

    /// Set available tools and their executor for function calling.
    void set_tools(std::vector<ToolDefinition> tools, ToolExecutor executor);

    [[nodiscard]] const std::vector<LLMResponseCacheAuditEvent> &
    response_cache_audit_events() const;
    void clear_response_cache_audit_events();
    [[nodiscard]] const std::vector<LLMProviderHealthEvent> &provider_health_events() const;
    void clear_provider_health_events();
    [[nodiscard]] const std::vector<LLMStreamingEvent> &streaming_events() const;
    void clear_streaming_events();
    [[nodiscard]] const std::vector<LLMTokenUsageEvent> &token_usage_events() const;
    void clear_token_usage_events();
    [[nodiscard]] const std::vector<LLMTokenBudgetEvent> &token_budget_events() const;
    void clear_token_budget_events();

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;
    LLMProviderConfig config_;
    std::optional<HttpClient::RequestTransport> transport_;
    PromptBuilder prompt_builder_;
    ResponseParser response_parser_;
    std::unique_ptr<ResponseCache> response_cache_;
    std::vector<LLMResponseCacheAuditEvent> response_cache_audit_events_;
    mutable std::vector<LLMProviderHealthEvent> provider_health_events_;
    std::vector<LLMStreamingEvent> streaming_events_;
    mutable std::vector<LLMTokenUsageEvent> token_usage_events_;
    mutable std::vector<LLMTokenBudgetEvent> token_budget_events_;
    struct CumulativeBudgetTotals {
        std::size_t total_tokens{0};
        double total_cost_usd{0.0};
    };
    struct WorkflowBudgetKey {
        runtime::RunId run;
        runtime::WorkflowId workflow;

        [[nodiscard]] friend bool operator==(const WorkflowBudgetKey &,
                                             const WorkflowBudgetKey &) noexcept = default;
    };
    struct WorkflowBudgetKeyHash {
        [[nodiscard]] std::size_t operator()(const WorkflowBudgetKey &key) const noexcept {
            return (key.run.index() * 1315423911U) ^ key.workflow.index();
        }
    };
    struct NodeBudgetKey {
        runtime::RunId run;
        runtime::WorkflowId workflow;
        runtime::WorkflowNodeId node;

        [[nodiscard]] friend bool operator==(const NodeBudgetKey &,
                                             const NodeBudgetKey &) noexcept = default;
    };
    struct NodeBudgetKeyHash {
        [[nodiscard]] std::size_t operator()(const NodeBudgetKey &key) const noexcept {
            return ((key.run.index() * 1315423911U) ^
                    (key.workflow.index() * 2654435761U)) ^
                   key.node.index();
        }
    };
    mutable std::unordered_map<WorkflowBudgetKey, CumulativeBudgetTotals, WorkflowBudgetKeyHash>
        workflow_budget_totals_;
    mutable std::unordered_map<NodeBudgetKey, CumulativeBudgetTotals, NodeBudgetKeyHash>
        node_budget_totals_;

    std::shared_ptr<LLMSessionStore> session_store_;
    mutable ModelCapacityResolver capacity_resolver_;
    mutable LLMSession *active_session_{nullptr};
    [[nodiscard]] std::expected<EffectiveRequestBudget, std::string>
    request_budget(const LLMProviderConfig &config, ProviderId id = 0) const;
    [[nodiscard]] std::string session_binding(const runtime::CapabilityInvocationContext &,
                                              const std::string &,
                                              const std::vector<runtime::Value> &) const;
    [[nodiscard]] runtime::CapabilityCallResult
    run_session(const runtime::CapabilityInvocationContext &,
                const std::string &,
                const std::vector<runtime::Value> &,
                bool resume);
    [[nodiscard]] std::expected<void, std::string> persist_session() const;

    [[nodiscard]] std::expected<void, std::string>
    reserve_session_request(const LLMProviderConfig &,
                            const runtime::CapabilityInvocationContext &,
                            const std::string &,
                            std::size_t) const;
    [[nodiscard]] std::expected<void, std::string>
    acknowledge_session_response(const LLMProviderConfig &,
                                 const runtime::CapabilityInvocationContext &,
                                 const HttpResponse &,
                                 std::size_t) const;

    // Tool calling state
    std::vector<ToolDefinition> tools_;
    ToolExecutor tool_executor_;

    // Build the request JSON sent to the LLM (simple two-message format)
    [[nodiscard]] std::string build_request_json(const LLMProviderConfig &config,
                                                 const std::string &system_prompt,
                                                 const std::string &user_prompt,
                                                 const EffectiveRequestBudget &budget) const;

    // Extract structured response including tool_calls from LLM response body
    [[nodiscard]] LLMResponseContent extract_response(const std::string &response_body) const;

    [[nodiscard]] runtime::CapabilityCallResult
    invoke_with_context_impl(const runtime::CapabilityInvocationContext &context,
                             const std::string &capability_name,
                             const std::vector<runtime::Value> &args);

    struct ProviderCandidate {
        std::string name;
        LLMProviderConfig config;
    };
    struct ProviderHttpResult {
        HttpResponse response;
        std::size_t attempts{0};
        std::string provider_name;
        std::optional<runtime::CapabilityUsage> usage;
    };
    struct UsagePolicyOutcome {
        runtime::CapabilityUsage usage;
        std::optional<runtime::CapabilityCallResult> rejection;
    };

    [[nodiscard]] std::vector<ProviderCandidate> provider_candidates() const;
    [[nodiscard]] HttpResponse send_chat_completion(const LLMProviderConfig &config,
                                                    const std::string &request_json) const;
    [[nodiscard]] HttpResponse send_chat_completion_streaming(
        const LLMProviderConfig &config, const std::string &request_json,
        const std::function<void(std::string_view chunk)> &on_chunk,
        SSEParser &parser) const;
    [[nodiscard]] std::expected<ProviderHttpResult, runtime::CapabilityCallResult>
    chat_with_provider_fallback(
        std::string_view capability_name,
        const runtime::CapabilityInvocationContext &context,
        const std::function<std::string(const LLMProviderConfig &, const EffectiveRequestBudget &)> &request_factory,
        const std::function<void(std::string_view chunk)> *on_stream_chunk = nullptr,
        SSEParser *stream_parser = nullptr) const;
    void record_response_cache_event(LLMResponseCacheAuditEventKind kind,
                                     const std::string &system_prompt,
                                     const std::string &user_prompt,
                                     std::size_t response_bytes);
    void record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind kind,
                                              std::size_t entry_count);
    void load_persistent_response_cache();
    void persist_response_cache_snapshot();
    void record_provider_health_event(LLMProviderHealthEvent event) const;
    [[nodiscard]] std::optional<UsagePolicyOutcome>
    record_token_usage_event(std::string_view provider_name,
                             std::string_view capability_name,
                             const runtime::CapabilityInvocationContext &context,
                             const LLMProviderConfig &config,
                             const std::string &response_body,
                             std::size_t attempts) const;
    void record_token_budget_event(LLMTokenBudgetEvent event) const;
};

} // namespace ahfl::llm_provider
