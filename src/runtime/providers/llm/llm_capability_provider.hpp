#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/providers/llm/http_client.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"
#include "runtime/providers/llm/prompt_builder.hpp"
#include "runtime/providers/llm/response_cache.hpp"
#include "runtime/providers/llm/response_parser.hpp"
#include "runtime/providers/llm/tool_calling.hpp"

namespace ahfl::llm_provider {

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
    std::string model;
    std::string cache_key_version;
    std::string key_fingerprint;
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
    std::string provider_name;
    std::string model;
    std::string workflow_name;
    std::string workflow_node_name;
    std::string agent_name;
    std::string state_name;
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
    PromptTruncated,
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
    std::string provider_name;
    std::string model;
    std::string capability_name;
    std::string workflow_name;
    std::string workflow_node_name;
    std::string agent_name;
    std::string state_name;
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
    std::string message;
    std::string diagnostic_code;
    bool secret_free{true};
};

// LLM Capability Provider: 将 AHFL capability 调用路由到 LLM API
class LLMCapabilityProvider {
  public:
    /// A tool executor that can run tools and return results.
    using ToolExecutor = std::function<ToolCallResult(const ToolCall &)>;

    LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config);
    LLMCapabilityProvider(const ir::Program &program,
                          LLMProviderConfig config,
                          HttpClient::ChatCompletionsTransport transport);

    // 调用 capability，通过 LLM 获取结果
    [[nodiscard]] runtime::CapabilityCallResult invoke(const std::string &capability_name,
                                                       const std::vector<evaluator::Value> &args);
    [[nodiscard]] runtime::CapabilityCallResult
    invoke_with_context(const runtime::CapabilityInvocationContext &context,
                        const std::string &capability_name,
                        const std::vector<evaluator::Value> &args);
    [[nodiscard]] runtime::ContextualCapabilityInvoker as_contextual_invoker();

    // 注册所有 capability 到 registry
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
    std::optional<HttpClient::ChatCompletionsTransport> transport_;
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
    mutable std::unordered_map<std::string, CumulativeBudgetTotals> workflow_budget_totals_;
    mutable std::unordered_map<std::string, CumulativeBudgetTotals> node_budget_totals_;

    // Tool calling state
    std::vector<ToolDefinition> tools_;
    ToolExecutor tool_executor_;

    // 构建发送给 LLM 的请求 JSON (simple two-message format)
    [[nodiscard]] std::string build_request_json(const LLMProviderConfig &config,
                                                 const std::string &system_prompt,
                                                 const std::string &user_prompt) const;

    // 从 LLM 响应体中提取 content 文本 (backward compat wrapper)
    [[nodiscard]] std::string extract_content_from_response(const std::string &response_body) const;

    // Extract structured response including tool_calls from LLM response body
    [[nodiscard]] LLMResponseContent extract_response(const std::string &response_body) const;

    // Build a full multi-turn request JSON with tool definitions
    [[nodiscard]] std::string
    build_full_request_json(const std::vector<std::pair<std::string, std::string>> &messages) const;

    struct ProviderCandidate {
        std::string name;
        LLMProviderConfig config;
    };
    struct ProviderHttpResult {
        HttpResponse response;
        std::size_t attempts{0};
        std::string provider_name;
    };

    [[nodiscard]] std::vector<ProviderCandidate> provider_candidates() const;
    [[nodiscard]] HttpResponse send_chat_completion(const LLMProviderConfig &config,
                                                    const std::string &request_json) const;
    [[nodiscard]] std::optional<ProviderHttpResult> chat_with_provider_fallback(
        std::string_view capability_name,
        const runtime::CapabilityInvocationContext &context,
        const std::function<std::string(const LLMProviderConfig &)> &request_factory,
        std::string &error_message,
        std::size_t &attempts) const;
    void record_response_cache_event(LLMResponseCacheAuditEventKind kind,
                                     const std::string &system_prompt,
                                     const std::string &user_prompt,
                                     std::size_t response_bytes);
    void record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind kind,
                                              std::size_t entry_count);
    void load_persistent_response_cache();
    void persist_response_cache_snapshot();
    void record_provider_health_event(LLMProviderHealthEvent event) const;
    void record_token_usage_event(std::string_view provider_name,
                                  std::string_view capability_name,
                                  const runtime::CapabilityInvocationContext &context,
                                  const LLMProviderConfig &config,
                                  const std::string &response_body) const;
    void record_token_budget_event(LLMTokenBudgetEvent event) const;
};

} // namespace ahfl::llm_provider
