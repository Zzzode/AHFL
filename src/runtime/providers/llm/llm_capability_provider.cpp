// llm_capability_provider.cpp - LLM Capability Provider 组合模块

#include "runtime/providers/llm/llm_capability_provider.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "base/json/json_value.hpp"
#include "runtime/providers/llm/streaming.hpp"
#include "runtime/providers/llm/token_budget.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::llm_provider {

namespace {

using ahfl::json::JsonValue;

/// Internal chat message representation for multi-turn conversations.
struct ChatMessage {
    std::string role;
    std::string content;
    std::vector<ToolCall> tool_calls; // only for assistant messages
    std::string tool_call_id;         // only for tool role messages
};

/// Build a JSON message object from a ChatMessage.
[[nodiscard]] std::unique_ptr<JsonValue> serialize_chat_message(const ChatMessage &msg) {
    auto obj = JsonValue::make_object();
    obj->set("role", JsonValue::make_string(msg.role));

    if (msg.role == "tool") {
        // Tool result messages have content and tool_call_id
        obj->set("content", JsonValue::make_string(msg.content));
        obj->set("tool_call_id", JsonValue::make_string(msg.tool_call_id));
    } else if (msg.role == "assistant" && !msg.tool_calls.empty()) {
        // Assistant message with tool_calls: content may be null or present
        if (!msg.content.empty()) {
            obj->set("content", JsonValue::make_string(msg.content));
        } else {
            obj->set("content", JsonValue::make_null());
        }
        auto tc_array = JsonValue::make_array();
        for (const auto &tc : msg.tool_calls) {
            auto tc_obj = JsonValue::make_object();
            tc_obj->set("id", JsonValue::make_string(tc.id));
            tc_obj->set("type", JsonValue::make_string("function"));
            auto fn_obj = JsonValue::make_object();
            fn_obj->set("name", JsonValue::make_string(tc.name));
            fn_obj->set("arguments", JsonValue::make_string(tc.arguments_json));
            tc_obj->set("function", std::move(fn_obj));
            tc_array->push(std::move(tc_obj));
        }
        obj->set("tool_calls", std::move(tc_array));
    } else {
        // system / user / plain assistant messages
        obj->set("content", JsonValue::make_string(msg.content));
    }

    return obj;
}

struct PromptBudgetResult {
    std::optional<std::string> error;
    LLMTokenBudgetEvent event;
};

[[nodiscard]] LLMProviderConfig effective_budget_config(const LLMProviderConfig &config,
                                                        std::string_view capability_name) {
    LLMProviderConfig effective = config;
    for (const auto &budget : config.capability_token_budgets) {
        if (budget.capability_name != capability_name) {
            continue;
        }
        if (budget.max_tokens.has_value()) {
            effective.max_tokens = *budget.max_tokens;
        }
        if (budget.max_prompt_tokens.has_value()) {
            effective.max_prompt_tokens = *budget.max_prompt_tokens;
        }
        if (budget.max_total_tokens.has_value()) {
            effective.max_total_tokens = *budget.max_total_tokens;
        }
        if (budget.max_total_cost_usd.has_value()) {
            effective.max_total_cost_usd = *budget.max_total_cost_usd;
        }
        if (budget.policy.has_value()) {
            effective.token_budget_policy = *budget.policy;
        }
        effective.capability_token_budgets.clear();
        return effective;
    }
    effective.capability_token_budgets.clear();
    return effective;
}

[[nodiscard]] PromptBudgetResult apply_prompt_budget(const LLMProviderConfig &config,
                                                     std::string_view capability_name,
                                                     std::string_view system_prompt,
                                                     std::string &user_prompt) {
    LLMTokenBudgetEvent event{
        .model = config.model,
        .capability_name = std::string(capability_name),
        .max_total_tokens = static_cast<std::size_t>(std::max(config.max_total_tokens, 0)),
        .max_prompt_tokens = static_cast<std::size_t>(std::max(config.max_prompt_tokens, 0)),
        .max_response_tokens = static_cast<std::size_t>(std::max(config.max_tokens, 0)),
        .system_prompt_tokens = TokenCounter::estimate(system_prompt),
        .user_prompt_tokens_before = TokenCounter::estimate(user_prompt),
        .user_prompt_tokens_after = TokenCounter::estimate(user_prompt),
        .max_total_cost_usd = config.max_total_cost_usd,
        .policy = config.token_budget_policy,
        .secret_free = true,
    };

    if (auto error = validate_config(config); error.has_value()) {
        event.kind = LLMTokenBudgetEventKind::PromptRejected;
        event.message = *error;
        event.diagnostic_code = ahfl::error_codes::runtime::LLMPromptBudgetRejected.full_code();
        return PromptBudgetResult{.error = error, .event = std::move(event)};
    }

    const auto max_total = static_cast<std::size_t>(config.max_total_tokens);
    const auto max_response = static_cast<std::size_t>(config.max_tokens);
    const auto max_prompt = static_cast<std::size_t>(config.max_prompt_tokens);
    const auto effective_prompt = std::min(max_prompt, max_total - max_response);
    event.effective_prompt_tokens = effective_prompt;
    if (event.system_prompt_tokens >= effective_prompt) {
        event.kind = LLMTokenBudgetEventKind::PromptRejected;
        event.message = "LLM prompt budget exhausted by system prompt";
        event.diagnostic_code = ahfl::error_codes::runtime::LLMPromptBudgetRejected.full_code();
        return PromptBudgetResult{.error = event.message, .event = std::move(event)};
    }

    PromptTruncator truncator(TokenBudget{
        .max_total = max_total,
        .max_prompt = effective_prompt,
        .max_response = max_response,
    });
    user_prompt = truncator.truncate(system_prompt, user_prompt);
    event.user_prompt_tokens_after = TokenCounter::estimate(user_prompt);
    event.truncated = event.user_prompt_tokens_after < event.user_prompt_tokens_before;
    event.kind = event.truncated ? LLMTokenBudgetEventKind::PromptTruncated
                                 : LLMTokenBudgetEventKind::PromptAccepted;
    event.message = event.truncated ? "LLM user prompt truncated to fit prompt budget"
                                    : "LLM prompt accepted within budget";
    return PromptBudgetResult{.error = std::nullopt, .event = std::move(event)};
}

void apply_invocation_context(LLMTokenUsageEvent &event,
                              const runtime::CapabilityInvocationContext &context) {
    event.workflow_name = context.workflow_name;
    event.workflow_node_name = context.workflow_node_name;
    event.agent_name = context.agent_name;
    event.state_name = context.state_name;
    event.workflow_node_execution_index = context.workflow_node_execution_index;
    event.has_workflow_node_context = context.has_workflow_node_context;
}

void apply_invocation_context(LLMTokenBudgetEvent &event,
                              const runtime::CapabilityInvocationContext &context) {
    event.workflow_name = context.workflow_name;
    event.workflow_node_name = context.workflow_node_name;
    event.agent_name = context.agent_name;
    event.state_name = context.state_name;
    event.workflow_node_execution_index = context.workflow_node_execution_index;
    event.has_workflow_node_context = context.has_workflow_node_context;
}

[[nodiscard]] std::string workflow_budget_key(const runtime::CapabilityInvocationContext &context) {
    return context.workflow_name.empty() ? std::string{"<direct>"} : context.workflow_name;
}

[[nodiscard]] std::optional<std::string>
node_budget_key(const runtime::CapabilityInvocationContext &context) {
    if (!context.has_workflow_node_context || context.workflow_node_name.empty()) {
        return std::nullopt;
    }
    std::string key = workflow_budget_key(context);
    key.push_back('\0');
    key += context.workflow_node_name;
    return key;
}

[[nodiscard]] std::string http_failure_summary(std::string_view provider_name,
                                               const HttpResponse &response) {
    std::string summary(provider_name);
    summary += "(status=";
    summary += std::to_string(response.status_code);
    if (!response.body.empty()) {
        summary += ", body=";
        summary += response.body.substr(0, 120);
    }
    summary += ")";
    return summary;
}

struct StreamExtraction {
    std::string content;
    bool completed{false};
    std::vector<LLMStreamingEvent> events;
};

[[nodiscard]] StreamExtraction extract_stream_content(const std::string &response_body,
                                                      std::string_view provider_name) {
    SSEParser parser;
    StreamExtraction extraction;
    std::size_t chunk_index = 0;
    std::string line;
    std::istringstream lines(response_body);
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        auto chunk = parser.feed_line(line);
        if (chunk.has_value()) {
            ++chunk_index;
            extraction.content += *chunk;
            extraction.events.push_back(LLMStreamingEvent{
                .kind = LLMStreamingEventKind::Chunk,
                .provider_name = std::string(provider_name),
                .chunk_index = chunk_index,
                .chunk_bytes = chunk->size(),
                .total_content_bytes = extraction.content.size(),
                .completed = false,
                .secret_free = true,
            });
        }
        if (parser.is_done()) {
            break;
        }
    }
    extraction.completed = parser.is_done();
    extraction.events.push_back(LLMStreamingEvent{
        .kind = extraction.completed ? LLMStreamingEventKind::Completed
                                     : LLMStreamingEventKind::Interrupted,
        .provider_name = std::string(provider_name),
        .chunk_index = chunk_index,
        .chunk_bytes = 0,
        .total_content_bytes = extraction.content.size(),
        .completed = extraction.completed,
        .secret_free = true,
    });
    return extraction;
}

[[nodiscard]] std::unique_ptr<ResponseCache> make_response_cache(const LLMProviderConfig &config) {
    if (!config.response_cache_enabled) {
        return nullptr;
    }
    return std::make_unique<ResponseCache>(
        static_cast<std::size_t>(config.response_cache_max_entries),
        std::chrono::seconds{config.response_cache_ttl_seconds});
}

[[nodiscard]] std::optional<std::size_t> non_negative_size_field(const JsonValue &object,
                                                                 std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_int();
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

struct ParsedResponseCacheSnapshot {
    bool valid{false};
    std::vector<ResponseCacheSnapshotEntry> entries;
};

[[nodiscard]] ParsedResponseCacheSnapshot parse_response_cache_snapshot(std::string_view content) {
    auto parsed = ahfl::json::parse_json(content);
    if (!parsed.has_value() || !*parsed || !(**parsed).is_object()) {
        return {};
    }
    const auto &root = **parsed;
    const auto *schema = root.get("schema");
    const auto *key_version = root.get("key_version");
    if (schema == nullptr || key_version == nullptr || !schema->as_string().has_value() ||
        !key_version->as_string().has_value() ||
        *schema->as_string() != kResponseCacheSnapshotSchema ||
        *key_version->as_string() != kResponseCacheKeyVersion) {
        return {};
    }
    const auto *entry_array = root.get("entries");
    if (entry_array == nullptr || !entry_array->is_array()) {
        return {};
    }
    ParsedResponseCacheSnapshot snapshot{.valid = true};
    for (const auto &item : entry_array->array_items) {
        if (item == nullptr || !item->is_object()) {
            continue;
        }
        const auto *key = item->get("key_fingerprint");
        const auto *response = item->get("response");
        const auto *inserted = item->get("inserted_unix_seconds");
        if (key == nullptr || response == nullptr || inserted == nullptr ||
            !key->as_string().has_value() || !response->as_string().has_value() ||
            !inserted->as_int().has_value()) {
            continue;
        }
        snapshot.entries.push_back(ResponseCacheSnapshotEntry{
            .key_fingerprint = std::string(*key->as_string()),
            .response = std::string(*response->as_string()),
            .inserted_unix_seconds = *inserted->as_int(),
        });
    }
    return snapshot;
}

[[nodiscard]] std::string
serialize_response_cache_snapshot(const std::vector<ResponseCacheSnapshotEntry> &entries) {
    auto root = JsonValue::make_object();
    root->set("schema", JsonValue::make_string(std::string(kResponseCacheSnapshotSchema)));
    root->set("key_version", JsonValue::make_string(std::string(kResponseCacheKeyVersion)));
    auto entry_array = JsonValue::make_array();
    for (const auto &entry : entries) {
        auto item = JsonValue::make_object();
        item->set("key_fingerprint", JsonValue::make_string(entry.key_fingerprint));
        item->set("response", JsonValue::make_string(entry.response));
        item->set("inserted_unix_seconds", JsonValue::make_int(entry.inserted_unix_seconds));
        entry_array->push(std::move(item));
    }
    root->set("entries", std::move(entry_array));
    return ahfl::json::serialize_json(*root);
}

[[nodiscard]] std::optional<std::string>
read_response_cache_snapshot_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return std::nullopt;
    }
    return buffer.str();
}

[[nodiscard]] bool write_response_cache_snapshot_file(const std::filesystem::path &path,
                                                      std::string_view content) {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

} // namespace

LLMCapabilityProvider::LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config)
    : program_(program), index_(program_), config_(std::move(config)), prompt_builder_(program_),
      response_parser_(program_), response_cache_(make_response_cache(config_)) {
    load_persistent_response_cache();
}

LLMCapabilityProvider::LLMCapabilityProvider(const ir::Program &program,
                                             LLMProviderConfig config,
                                             HttpClient::ChatCompletionsTransport transport)
    : program_(program), index_(program_), config_(std::move(config)),
      transport_(std::move(transport)), prompt_builder_(program_), response_parser_(program_),
      response_cache_(make_response_cache(config_)) {
    load_persistent_response_cache();
}

void LLMCapabilityProvider::set_tools(std::vector<ToolDefinition> tools, ToolExecutor executor) {
    tools_ = std::move(tools);
    tool_executor_ = std::move(executor);
}

const std::vector<LLMResponseCacheAuditEvent> &
LLMCapabilityProvider::response_cache_audit_events() const {
    return response_cache_audit_events_;
}

void LLMCapabilityProvider::clear_response_cache_audit_events() {
    response_cache_audit_events_.clear();
}

const std::vector<LLMProviderHealthEvent> &LLMCapabilityProvider::provider_health_events() const {
    return provider_health_events_;
}

void LLMCapabilityProvider::clear_provider_health_events() {
    provider_health_events_.clear();
}

const std::vector<LLMStreamingEvent> &LLMCapabilityProvider::streaming_events() const {
    return streaming_events_;
}

void LLMCapabilityProvider::clear_streaming_events() {
    streaming_events_.clear();
}

const std::vector<LLMTokenUsageEvent> &LLMCapabilityProvider::token_usage_events() const {
    return token_usage_events_;
}

void LLMCapabilityProvider::clear_token_usage_events() {
    token_usage_events_.clear();
}

const std::vector<LLMTokenBudgetEvent> &LLMCapabilityProvider::token_budget_events() const {
    return token_budget_events_;
}

void LLMCapabilityProvider::clear_token_budget_events() {
    token_budget_events_.clear();
}

void LLMCapabilityProvider::record_response_cache_event(LLMResponseCacheAuditEventKind kind,
                                                        const std::string &system_prompt,
                                                        const std::string &user_prompt,
                                                        std::size_t response_bytes) {
    if (response_cache_ == nullptr) {
        return;
    }
    response_cache_audit_events_.push_back(LLMResponseCacheAuditEvent{
        .kind = kind,
        .model = config_.model,
        .cache_key_version = std::string(kResponseCacheKeyVersion),
        .key_fingerprint =
            response_cache_key_fingerprint(config_.model, system_prompt, user_prompt),
        .system_prompt_bytes = system_prompt.size(),
        .user_prompt_bytes = user_prompt.size(),
        .response_bytes = response_bytes,
        .cache_size_after = response_cache_->size(),
        .persistent = !config_.response_cache_path.empty(),
        .secret_free = true,
    });
}

void LLMCapabilityProvider::record_response_cache_snapshot_event(
    LLMResponseCacheAuditEventKind kind, std::size_t entry_count) {
    if (response_cache_ == nullptr) {
        return;
    }
    response_cache_audit_events_.push_back(LLMResponseCacheAuditEvent{
        .kind = kind,
        .model = config_.model,
        .cache_key_version = std::string(kResponseCacheKeyVersion),
        .cache_size_after = response_cache_->size(),
        .snapshot_entry_count = entry_count,
        .persistent = !config_.response_cache_path.empty(),
        .secret_free = true,
    });
}

void LLMCapabilityProvider::load_persistent_response_cache() {
    if (response_cache_ == nullptr || config_.response_cache_path.empty()) {
        return;
    }
    const std::filesystem::path path{config_.response_cache_path};
    if (!std::filesystem::exists(path)) {
        return;
    }
    auto content = read_response_cache_snapshot_file(path);
    if (!content.has_value()) {
        record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind::SnapshotLoadFailed, 0);
        return;
    }
    auto snapshot = parse_response_cache_snapshot(*content);
    if (!snapshot.valid) {
        record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind::SnapshotLoadFailed, 0);
        return;
    }
    const auto loaded = response_cache_->load_snapshot(snapshot.entries);
    record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind::SnapshotLoaded, loaded);
}

void LLMCapabilityProvider::persist_response_cache_snapshot() {
    if (response_cache_ == nullptr || config_.response_cache_path.empty()) {
        return;
    }
    auto entries = response_cache_->snapshot();
    const auto content = serialize_response_cache_snapshot(entries);
    if (write_response_cache_snapshot_file(std::filesystem::path{config_.response_cache_path},
                                           content)) {
        record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind::SnapshotPersisted,
                                             entries.size());
        return;
    }
    record_response_cache_snapshot_event(LLMResponseCacheAuditEventKind::SnapshotPersistFailed,
                                         entries.size());
}

void LLMCapabilityProvider::record_provider_health_event(LLMProviderHealthEvent event) const {
    provider_health_events_.push_back(std::move(event));
}

void LLMCapabilityProvider::record_token_budget_event(LLMTokenBudgetEvent event) const {
    token_budget_events_.push_back(std::move(event));
}

void LLMCapabilityProvider::record_token_usage_event(
    std::string_view provider_name,
    std::string_view capability_name,
    const runtime::CapabilityInvocationContext &context,
    const LLMProviderConfig &config,
    const std::string &response_body) const {
    auto parsed = ahfl::json::parse_json(response_body);
    if (!parsed.has_value() || !*parsed) {
        return;
    }

    const auto *usage = (*parsed)->get("usage");
    if (usage == nullptr || !usage->is_object()) {
        return;
    }

    const auto prompt_tokens = non_negative_size_field(*usage, "prompt_tokens").value_or(0);
    const auto completion_tokens = non_negative_size_field(*usage, "completion_tokens").value_or(0);
    const auto total_tokens =
        non_negative_size_field(*usage, "total_tokens").value_or(prompt_tokens + completion_tokens);
    const auto usage_exceeded_budget =
        total_tokens > static_cast<std::size_t>(config.max_total_tokens);
    const auto prompt_cost =
        static_cast<double>(prompt_tokens) * config.prompt_token_cost_per_million / 1'000'000.0;
    const auto completion_cost = static_cast<double>(completion_tokens) *
                                 config.completion_token_cost_per_million / 1'000'000.0;
    const auto total_cost = prompt_cost + completion_cost;
    const auto has_cost_budget = config.max_total_cost_usd > 0.0;
    const auto cost_exceeded_budget = has_cost_budget && total_cost > config.max_total_cost_usd;

    LLMTokenUsageEvent usage_event{
        .provider_name = std::string(provider_name),
        .model = config.model,
        .prompt_tokens = prompt_tokens,
        .completion_tokens = completion_tokens,
        .total_tokens = total_tokens,
        .prompt_cost_usd = prompt_cost,
        .completion_cost_usd = completion_cost,
        .total_cost_usd = total_cost,
        .cost_estimated = config.prompt_token_cost_per_million > 0.0 ||
                          config.completion_token_cost_per_million > 0.0,
        .secret_free = true,
    };
    apply_invocation_context(usage_event, context);
    token_usage_events_.push_back(std::move(usage_event));

    LLMTokenBudgetEvent usage_budget_event{
        .kind = usage_exceeded_budget ? LLMTokenBudgetEventKind::UsageExceededBudget
                                      : LLMTokenBudgetEventKind::UsageWithinBudget,
        .provider_name = std::string(provider_name),
        .model = config.model,
        .capability_name = std::string(capability_name),
        .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
        .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
        .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
        .prompt_tokens = prompt_tokens,
        .completion_tokens = completion_tokens,
        .total_tokens = total_tokens,
        .max_total_cost_usd = config.max_total_cost_usd,
        .total_cost_usd = total_cost,
        .policy = config.token_budget_policy,
        .message = usage_exceeded_budget
                       ? "LLM provider reported token usage above max_total_tokens"
                       : "LLM provider reported token usage within max_total_tokens",
        .diagnostic_code = usage_exceeded_budget
                               ? ahfl::error_codes::runtime::LLMTokenBudgetExceeded.full_code()
                               : std::string{},
        .secret_free = true,
    };
    apply_invocation_context(usage_budget_event, context);
    record_token_budget_event(std::move(usage_budget_event));

    if (has_cost_budget) {
        LLMTokenBudgetEvent cost_budget_event{
            .kind = cost_exceeded_budget ? LLMTokenBudgetEventKind::CostExceededBudget
                                         : LLMTokenBudgetEventKind::CostWithinBudget,
            .provider_name = std::string(provider_name),
            .model = config.model,
            .capability_name = std::string(capability_name),
            .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
            .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
            .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
            .prompt_tokens = prompt_tokens,
            .completion_tokens = completion_tokens,
            .total_tokens = total_tokens,
            .max_total_cost_usd = config.max_total_cost_usd,
            .total_cost_usd = total_cost,
            .policy = config.token_budget_policy,
            .message = cost_exceeded_budget
                           ? "LLM provider estimated cost above max_total_cost_usd"
                           : "LLM provider estimated cost within max_total_cost_usd",
            .diagnostic_code = cost_exceeded_budget
                                   ? ahfl::error_codes::runtime::LLMCostBudgetExceeded.full_code()
                                   : std::string{},
            .secret_free = true,
        };
        apply_invocation_context(cost_budget_event, context);
        record_token_budget_event(std::move(cost_budget_event));
    }

    const auto has_workflow_token_budget = config.max_workflow_total_tokens > 0;
    const auto has_workflow_cost_budget = config.max_workflow_total_cost_usd > 0.0;
    if (has_workflow_token_budget || has_workflow_cost_budget) {
        auto &totals = workflow_budget_totals_[workflow_budget_key(context)];
        totals.total_tokens += total_tokens;
        totals.total_cost_usd += total_cost;
        if (has_workflow_token_budget) {
            const auto max_workflow_tokens =
                static_cast<std::size_t>(config.max_workflow_total_tokens);
            const auto exceeded = totals.total_tokens > max_workflow_tokens;
            LLMTokenBudgetEvent workflow_token_event{
                .kind = exceeded ? LLMTokenBudgetEventKind::WorkflowUsageExceededBudget
                                 : LLMTokenBudgetEventKind::WorkflowUsageWithinBudget,
                .provider_name = std::string(provider_name),
                .model = config.model,
                .capability_name = std::string(capability_name),
                .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
                .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
                .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
                .max_workflow_total_tokens = max_workflow_tokens,
                .prompt_tokens = prompt_tokens,
                .completion_tokens = completion_tokens,
                .total_tokens = total_tokens,
                .cumulative_workflow_tokens = totals.total_tokens,
                .max_total_cost_usd = config.max_total_cost_usd,
                .policy = config.token_budget_policy,
                .message = exceeded ? "LLM workflow cumulative token usage exceeded budget"
                                    : "LLM workflow cumulative token usage within budget",
                .diagnostic_code =
                    exceeded ? ahfl::error_codes::runtime::LLMTokenBudgetExceeded.full_code()
                             : std::string{},
                .secret_free = true,
            };
            apply_invocation_context(workflow_token_event, context);
            record_token_budget_event(std::move(workflow_token_event));
        }
        if (has_workflow_cost_budget) {
            const auto exceeded = totals.total_cost_usd > config.max_workflow_total_cost_usd;
            LLMTokenBudgetEvent workflow_cost_event{
                .kind = exceeded ? LLMTokenBudgetEventKind::WorkflowCostExceededBudget
                                 : LLMTokenBudgetEventKind::WorkflowCostWithinBudget,
                .provider_name = std::string(provider_name),
                .model = config.model,
                .capability_name = std::string(capability_name),
                .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
                .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
                .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
                .prompt_tokens = prompt_tokens,
                .completion_tokens = completion_tokens,
                .total_tokens = total_tokens,
                .cumulative_workflow_tokens = totals.total_tokens,
                .max_total_cost_usd = config.max_total_cost_usd,
                .max_workflow_total_cost_usd = config.max_workflow_total_cost_usd,
                .total_cost_usd = total_cost,
                .cumulative_workflow_cost_usd = totals.total_cost_usd,
                .policy = config.token_budget_policy,
                .message = exceeded ? "LLM workflow cumulative cost exceeded budget"
                                    : "LLM workflow cumulative cost within budget",
                .diagnostic_code =
                    exceeded ? ahfl::error_codes::runtime::LLMCostBudgetExceeded.full_code()
                             : std::string{},
                .secret_free = true,
            };
            apply_invocation_context(workflow_cost_event, context);
            record_token_budget_event(std::move(workflow_cost_event));
        }
    }

    const auto maybe_node_key = node_budget_key(context);
    const auto has_node_token_budget =
        maybe_node_key.has_value() && config.max_node_total_tokens > 0;
    const auto has_node_cost_budget =
        maybe_node_key.has_value() && config.max_node_total_cost_usd > 0.0;
    if (has_node_token_budget || has_node_cost_budget) {
        auto &totals = node_budget_totals_[*maybe_node_key];
        totals.total_tokens += total_tokens;
        totals.total_cost_usd += total_cost;
        if (has_node_token_budget) {
            const auto max_node_tokens = static_cast<std::size_t>(config.max_node_total_tokens);
            const auto exceeded = totals.total_tokens > max_node_tokens;
            LLMTokenBudgetEvent node_token_event{
                .kind = exceeded ? LLMTokenBudgetEventKind::NodeUsageExceededBudget
                                 : LLMTokenBudgetEventKind::NodeUsageWithinBudget,
                .provider_name = std::string(provider_name),
                .model = config.model,
                .capability_name = std::string(capability_name),
                .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
                .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
                .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
                .max_node_total_tokens = max_node_tokens,
                .prompt_tokens = prompt_tokens,
                .completion_tokens = completion_tokens,
                .total_tokens = total_tokens,
                .cumulative_node_tokens = totals.total_tokens,
                .max_total_cost_usd = config.max_total_cost_usd,
                .policy = config.token_budget_policy,
                .message = exceeded ? "LLM node cumulative token usage exceeded budget"
                                    : "LLM node cumulative token usage within budget",
                .diagnostic_code =
                    exceeded ? ahfl::error_codes::runtime::LLMTokenBudgetExceeded.full_code()
                             : std::string{},
                .secret_free = true,
            };
            apply_invocation_context(node_token_event, context);
            record_token_budget_event(std::move(node_token_event));
        }
        if (has_node_cost_budget) {
            const auto exceeded = totals.total_cost_usd > config.max_node_total_cost_usd;
            LLMTokenBudgetEvent node_cost_event{
                .kind = exceeded ? LLMTokenBudgetEventKind::NodeCostExceededBudget
                                 : LLMTokenBudgetEventKind::NodeCostWithinBudget,
                .provider_name = std::string(provider_name),
                .model = config.model,
                .capability_name = std::string(capability_name),
                .max_total_tokens = static_cast<std::size_t>(config.max_total_tokens),
                .max_prompt_tokens = static_cast<std::size_t>(config.max_prompt_tokens),
                .max_response_tokens = static_cast<std::size_t>(config.max_tokens),
                .prompt_tokens = prompt_tokens,
                .completion_tokens = completion_tokens,
                .total_tokens = total_tokens,
                .cumulative_node_tokens = totals.total_tokens,
                .max_total_cost_usd = config.max_total_cost_usd,
                .max_node_total_cost_usd = config.max_node_total_cost_usd,
                .total_cost_usd = total_cost,
                .cumulative_node_cost_usd = totals.total_cost_usd,
                .policy = config.token_budget_policy,
                .message = exceeded ? "LLM node cumulative cost exceeded budget"
                                    : "LLM node cumulative cost within budget",
                .diagnostic_code =
                    exceeded ? ahfl::error_codes::runtime::LLMCostBudgetExceeded.full_code()
                             : std::string{},
                .secret_free = true,
            };
            apply_invocation_context(node_cost_event, context);
            record_token_budget_event(std::move(node_cost_event));
        }
    }
}

std::string LLMCapabilityProvider::build_request_json(const LLMProviderConfig &config,
                                                      const std::string &system_prompt,
                                                      const std::string &user_prompt) const {
    auto root = JsonValue::make_object();
    root->set("model", JsonValue::make_string(config.model));

    auto messages = JsonValue::make_array();
    auto system = JsonValue::make_object();
    system->set("role", JsonValue::make_string("system"));
    system->set("content", JsonValue::make_string(system_prompt));
    messages->push(std::move(system));

    auto user = JsonValue::make_object();
    user->set("role", JsonValue::make_string("user"));
    user->set("content", JsonValue::make_string(user_prompt));
    messages->push(std::move(user));
    root->set("messages", std::move(messages));

    root->set("temperature", JsonValue::make_float(config.temperature));
    root->set("max_tokens", JsonValue::make_int(config.max_tokens));
    if (config.stream) {
        root->set("stream", JsonValue::make_bool(true));
    }

    if (config.json_mode) {
        auto format = JsonValue::make_object();
        format->set("type", JsonValue::make_string("json_object"));
        root->set("response_format", std::move(format));
    }

    return ahfl::json::serialize_json(*root);
}

std::string LLMCapabilityProvider::build_full_request_json(
    const std::vector<std::pair<std::string, std::string>> & /*messages*/) const {
    // This overload exists for the interface but the tool-calling loop uses
    // the ChatMessage-based internal builder below. Kept for ABI compat.
    return {};
}

std::vector<LLMCapabilityProvider::ProviderCandidate>
LLMCapabilityProvider::provider_candidates() const {
    ProviderRegistry registry;
    registry.add_provider(ProviderEntry{
        .name = "primary",
        .endpoint = config_.endpoint,
        .api_key = config_.api_key,
        .api_key_secret = config_.api_key_secret,
        .oauth2_token_secret = config_.oauth2_token_secret,
        .mtls_client_cert_path = config_.mtls_client_cert_path,
        .mtls_client_key_path = config_.mtls_client_key_path,
        .mtls_ca_cert_path = config_.mtls_ca_cert_path,
        .mtls_client_cert_secret = config_.mtls_client_cert_secret,
        .mtls_client_key_secret = config_.mtls_client_key_secret,
        .mtls_ca_cert_secret = config_.mtls_ca_cert_secret,
        .mtls_verify_tls = config_.mtls_verify_tls,
        .mtls_verify_tls_set = true,
        .auth_scheme = config_.auth_scheme,
        .auth_header = config_.auth_header,
        .model = config_.model,
        .priority = std::numeric_limits<int>::max(),
        .status = ProviderStatus::Available,
    });
    for (const auto &fallback : config_.fallback_providers) {
        registry.add_provider(fallback);
    }

    std::vector<ProviderCandidate> candidates;
    for (const auto &entry : registry.selection_order()) {
        LLMProviderConfig candidate = config_;
        candidate.endpoint = entry.endpoint;
        candidate.api_key = entry.api_key;
        candidate.api_key_secret = entry.api_key_secret;
        candidate.oauth2_token_secret = entry.oauth2_token_secret;
        if (!entry.mtls_client_cert_path.empty()) {
            candidate.mtls_client_cert_path = entry.mtls_client_cert_path;
        }
        if (!entry.mtls_client_key_path.empty()) {
            candidate.mtls_client_key_path = entry.mtls_client_key_path;
        }
        if (!entry.mtls_ca_cert_path.empty()) {
            candidate.mtls_ca_cert_path = entry.mtls_ca_cert_path;
        }
        if (!entry.mtls_client_cert_secret.empty()) {
            candidate.mtls_client_cert_secret = entry.mtls_client_cert_secret;
        }
        if (!entry.mtls_client_key_secret.empty()) {
            candidate.mtls_client_key_secret = entry.mtls_client_key_secret;
        }
        if (!entry.mtls_ca_cert_secret.empty()) {
            candidate.mtls_ca_cert_secret = entry.mtls_ca_cert_secret;
        }
        if (entry.mtls_verify_tls_set) {
            candidate.mtls_verify_tls = entry.mtls_verify_tls;
        }
        if (!entry.auth_scheme.empty()) {
            candidate.auth_scheme = entry.auth_scheme;
        }
        if (!entry.auth_header.empty()) {
            candidate.auth_header = entry.auth_header;
        }
        candidate.model = entry.model;
        candidate.fallback_providers.clear();
        candidates.push_back(ProviderCandidate{
            .name = entry.name,
            .config = std::move(candidate),
        });
    }
    return candidates;
}

HttpResponse LLMCapabilityProvider::send_chat_completion(const LLMProviderConfig &config,
                                                         const std::string &request_json) const {
    const auto auth_config = HttpAuthConfig{
        .scheme = config.auth_scheme,
        .header = config.auth_header,
        .mtls_client_cert_path = config.mtls_client_cert_path,
        .mtls_client_key_path = config.mtls_client_key_path,
        .mtls_ca_cert_path = config.mtls_ca_cert_path,
        .mtls_verify_tls = config.mtls_verify_tls,
    };
    if (transport_.has_value()) {
        HttpClient client(
            config.endpoint, config.api_key, config.timeout_seconds, *transport_, auth_config);
        return client.chat_completions(request_json);
    }
    HttpClient client(config.endpoint, config.api_key, config.timeout_seconds, auth_config);
    return client.chat_completions(request_json);
}

std::optional<LLMCapabilityProvider::ProviderHttpResult>
LLMCapabilityProvider::chat_with_provider_fallback(
    std::string_view capability_name,
    const runtime::CapabilityInvocationContext &context,
    const std::function<std::string(const LLMProviderConfig &)> &request_factory,
    std::string &error_message,
    std::size_t &attempts) const {
    std::vector<std::string> failures;
    std::vector<std::string> degraded_providers;
    std::size_t total_attempts = 0;

    for (const auto &candidate : provider_candidates()) {
        const auto candidate_config = effective_budget_config(candidate.config, capability_name);
        const auto request_json = request_factory(candidate_config);
        HttpResponse response;
        for (int attempt = 1; attempt <= candidate_config.max_retries + 1; ++attempt) {
            ++total_attempts;
            response = send_chat_completion(candidate_config, request_json);
            if (response.success()) {
                record_token_usage_event(
                    candidate.name, capability_name, context, candidate_config, response.body);
                if (!failures.empty()) {
                    record_provider_health_event(LLMProviderHealthEvent{
                        .kind = LLMProviderHealthEventKind::FallbackSelected,
                        .provider_name =
                            degraded_providers.empty() ? std::string{} : degraded_providers.front(),
                        .selected_provider_name = candidate.name,
                        .status_code = response.status_code,
                        .attempts_after = total_attempts,
                        .message = "LLM provider fallback selected '" + candidate.name + "'",
                        .secret_free = true,
                    });
                }
                attempts = total_attempts;
                return ProviderHttpResult{
                    .response = std::move(response),
                    .attempts = total_attempts,
                    .provider_name = candidate.name,
                };
            }
        }

        const auto summary = http_failure_summary(candidate.name, response);
        record_provider_health_event(LLMProviderHealthEvent{
            .kind = LLMProviderHealthEventKind::ProviderDegraded,
            .provider_name = candidate.name,
            .selected_provider_name = {},
            .status_code = response.status_code,
            .attempts_after = total_attempts,
            .message = summary,
            .secret_free = true,
        });
        degraded_providers.push_back(candidate.name);
        failures.push_back(summary);
    }

    std::ostringstream message;
    message << "LLM provider fallback exhausted";
    if (!failures.empty()) {
        message << ": ";
        for (std::size_t index = 0; index < failures.size(); ++index) {
            if (index > 0) {
                message << "; ";
            }
            message << failures[index];
        }
    }
    error_message = message.str();
    record_provider_health_event(LLMProviderHealthEvent{
        .kind = LLMProviderHealthEventKind::FallbackExhausted,
        .provider_name = {},
        .selected_provider_name = {},
        .status_code = 0,
        .attempts_after = total_attempts,
        .message = error_message,
        .secret_free = true,
    });
    attempts = total_attempts;
    return std::nullopt;
}

LLMResponseContent LLMCapabilityProvider::extract_response(const std::string &response_body) const {
    LLMResponseContent result;
    auto parsed = ahfl::json::parse_json(response_body);
    if (!parsed.has_value() || !*parsed) {
        return result;
    }

    const auto *choices = (*parsed)->get("choices");
    if (choices == nullptr || !choices->is_array() || choices->array_items.empty()) {
        return result;
    }
    const auto *choice = choices->array_items.front().get();
    const auto *message = choice != nullptr ? choice->get("message") : nullptr;
    if (message == nullptr) {
        return result;
    }

    // Extract content
    const auto *content = message->get("content");
    if (content != nullptr) {
        if (auto value = content->as_string(); value.has_value()) {
            result.content = std::string(*value);
        }
    }

    // Extract tool_calls
    const auto *tool_calls_node = message->get("tool_calls");
    if (tool_calls_node != nullptr && tool_calls_node->is_array()) {
        for (const auto &item : tool_calls_node->array_items) {
            if (item == nullptr || !item->is_object()) {
                continue;
            }
            ToolCall call;
            // Extract id
            if (const auto *id_node = item->get("id"); id_node != nullptr) {
                if (auto id_str = id_node->as_string(); id_str.has_value()) {
                    call.id = std::string(*id_str);
                }
            }
            // Extract function.name and function.arguments
            const auto *function = item->get("function");
            if (function != nullptr && function->is_object()) {
                if (const auto *name_node = function->get("name"); name_node != nullptr) {
                    if (auto name_str = name_node->as_string(); name_str.has_value()) {
                        call.name = std::string(*name_str);
                    }
                }
                if (const auto *args_node = function->get("arguments"); args_node != nullptr) {
                    if (auto args_str = args_node->as_string(); args_str.has_value()) {
                        call.arguments_json = std::string(*args_str);
                    } else if (!args_node->is_null()) {
                        call.arguments_json = ahfl::json::serialize_json(*args_node);
                    }
                }
            }
            if (!call.name.empty()) {
                result.tool_calls.push_back(std::move(call));
            }
        }
    }

    return result;
}

std::string
LLMCapabilityProvider::extract_content_from_response(const std::string &response_body) const {
    return extract_response(response_body).content;
}

runtime::CapabilityCallResult
LLMCapabilityProvider::invoke(const std::string &capability_name,
                              const std::vector<evaluator::Value> &args) {
    return invoke_with_context(runtime::CapabilityInvocationContext{}, capability_name, args);
}

runtime::CapabilityCallResult
LLMCapabilityProvider::invoke_with_context(const runtime::CapabilityInvocationContext &context,
                                           const std::string &capability_name,
                                           const std::vector<evaluator::Value> &args) {
    // 查找 capability 的返回类型
    std::string return_type;
    if (const auto *capability = index_.find_capability(capability_name); capability != nullptr) {
        return_type = std::string(ir::type_canonical_name(capability->return_type_ref, "Any"));
    }

    if (return_type.empty()) {
        return runtime::CapabilityCallResult{
            .status = runtime::CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "capability not found: " + capability_name,
            .attempts = 0,
        };
    }

    // 构建 prompt
    std::string system_prompt = prompt_builder_.build_system_prompt(capability_name);
    std::string user_prompt = prompt_builder_.build_user_prompt(capability_name, args);
    const auto budget_config = effective_budget_config(config_, capability_name);
    auto prompt_budget =
        apply_prompt_budget(budget_config, capability_name, system_prompt, user_prompt);
    apply_invocation_context(prompt_budget.event, context);
    record_token_budget_event(std::move(prompt_budget.event));
    if (prompt_budget.error.has_value()) {
        return runtime::CapabilityCallResult{
            .status = runtime::CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = *prompt_budget.error,
            .attempts = 0,
        };
    }

    // --- Non-tool path: original behavior when no tools are configured ---
    if (tools_.empty()) {
        if (response_cache_ != nullptr) {
            auto cached = response_cache_->get(config_.model, system_prompt, user_prompt);
            if (cached.has_value()) {
                record_response_cache_event(LLMResponseCacheAuditEventKind::Hit,
                                            system_prompt,
                                            user_prompt,
                                            cached->size());
                auto result = response_parser_.parse_with_diagnostics(*cached, return_type);
                if (result.success()) {
                    return runtime::CapabilityCallResult{
                        .status = runtime::CapabilityCallStatus::Success,
                        .value = std::move(*result.value),
                        .error_message = {},
                        .attempts = 0,
                    };
                }
                record_response_cache_event(LLMResponseCacheAuditEventKind::Invalidated,
                                            system_prompt,
                                            user_prompt,
                                            cached->size());
                response_cache_->clear();
            } else {
                record_response_cache_event(
                    LLMResponseCacheAuditEventKind::Miss, system_prompt, user_prompt, 0);
            }
        }

        std::string fallback_error;
        std::size_t attempts = 0;
        auto http_result = chat_with_provider_fallback(
            capability_name,
            context,
            [&](const LLMProviderConfig &provider_config) {
                return build_request_json(provider_config, system_prompt, user_prompt);
            },
            fallback_error,
            attempts);

        if (!http_result.has_value()) {
            return runtime::CapabilityCallResult{
                .status = config_.max_retries > 0 ? runtime::CapabilityCallStatus::RetryExhausted
                                                  : runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = std::move(fallback_error),
                .attempts = attempts,
            };
        }

        std::string content;
        if (config_.stream) {
            auto stream =
                extract_stream_content(http_result->response.body, http_result->provider_name);
            for (auto &event : stream.events) {
                streaming_events_.push_back(std::move(event));
            }
            if (!stream.completed) {
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "incomplete LLM streaming response: missing [DONE]",
                    .attempts = http_result->attempts,
                };
            }
            content = std::move(stream.content);
        } else {
            content = extract_content_from_response(http_result->response.body);
        }
        if (content.empty()) {
            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = config_.stream ? "empty content in LLM streaming response"
                                                : "empty content in LLM response",
                .attempts = http_result->attempts,
            };
        }

        auto result = response_parser_.parse_with_diagnostics(content, return_type);
        if (!result.success()) {
            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "failed to parse LLM response for type '" + return_type +
                                 "': " + result.error_message,
                .attempts = http_result->attempts,
            };
        }

        if (response_cache_ != nullptr) {
            response_cache_->put(config_.model, system_prompt, user_prompt, content);
            record_response_cache_event(
                LLMResponseCacheAuditEventKind::Write, system_prompt, user_prompt, content.size());
            persist_response_cache_snapshot();
        }

        return runtime::CapabilityCallResult{
            .status = runtime::CapabilityCallStatus::Success,
            .value = std::move(*result.value),
            .error_message = {},
            .attempts = http_result->attempts,
        };
    }

    // --- Tool-calling path ---
    // Build conversation as a vector of ChatMessages
    std::vector<ChatMessage> conversation;
    conversation.push_back(ChatMessage{.role = "system", .content = system_prompt});
    conversation.push_back(ChatMessage{.role = "user", .content = user_prompt});

    std::size_t total_attempts = 0;

    for (int round = 0; round < config_.max_tool_rounds; ++round) {
        const auto build_tool_request = [&](const LLMProviderConfig &provider_config) {
            auto root = JsonValue::make_object();
            root->set("model", JsonValue::make_string(provider_config.model));

            auto messages_json = JsonValue::make_array();
            for (const auto &msg : conversation) {
                messages_json->push(serialize_chat_message(msg));
            }
            root->set("messages", std::move(messages_json));

            root->set("temperature", JsonValue::make_float(provider_config.temperature));
            root->set("max_tokens", JsonValue::make_int(provider_config.max_tokens));

            auto tool_array = JsonValue::make_array();
            for (const auto &tool : tools_) {
                auto function = JsonValue::make_object();
                function->set("name", JsonValue::make_string(tool.name));
                function->set("description", JsonValue::make_string(tool.description));

                auto params_parsed = ahfl::json::parse_json(tool.params_schema_json);
                if (params_parsed.has_value() && *params_parsed) {
                    function->set("parameters", std::move(*params_parsed));
                } else {
                    function->set("parameters", JsonValue::make_string(tool.params_schema_json));
                }

                auto tool_object = JsonValue::make_object();
                tool_object->set("type", JsonValue::make_string("function"));
                tool_object->set("function", std::move(function));
                tool_array->push(std::move(tool_object));
            }
            root->set("tools", std::move(tool_array));

            if (provider_config.tool_choice == "auto" || provider_config.tool_choice == "none") {
                root->set("tool_choice", JsonValue::make_string(provider_config.tool_choice));
            } else {
                auto tc_obj = JsonValue::make_object();
                tc_obj->set("type", JsonValue::make_string("function"));
                auto fn_name_obj = JsonValue::make_object();
                fn_name_obj->set("name", JsonValue::make_string(provider_config.tool_choice));
                tc_obj->set("function", std::move(fn_name_obj));
                root->set("tool_choice", std::move(tc_obj));
            }

            return ahfl::json::serialize_json(*root);
        };

        std::string fallback_error;
        std::size_t round_attempts = 0;
        auto http_result = chat_with_provider_fallback(
            capability_name, context, build_tool_request, fallback_error, round_attempts);
        total_attempts += round_attempts;

        if (!http_result.has_value()) {
            return runtime::CapabilityCallResult{
                .status = config_.max_retries > 0 ? runtime::CapabilityCallStatus::RetryExhausted
                                                  : runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = std::move(fallback_error),
                .attempts = total_attempts,
            };
        }

        // Parse structured response
        LLMResponseContent llm_response = extract_response(http_result->response.body);

        if (!llm_response.has_tool_calls()) {
            // No tool calls: final answer
            if (llm_response.content.empty()) {
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "empty content in LLM response",
                    .attempts = static_cast<std::size_t>(total_attempts),
                };
            }

            auto result =
                response_parser_.parse_with_diagnostics(llm_response.content, return_type);
            if (!result.success()) {
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "failed to parse LLM response for type '" + return_type +
                                     "': " + result.error_message,
                    .attempts = static_cast<std::size_t>(total_attempts),
                };
            }

            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Success,
                .value = std::move(*result.value),
                .error_message = {},
                .attempts = static_cast<std::size_t>(total_attempts),
            };
        }

        // Has tool calls: execute them and continue the loop
        // Append the assistant message with tool_calls to conversation
        conversation.push_back(ChatMessage{
            .role = "assistant",
            .content = llm_response.content,
            .tool_calls = llm_response.tool_calls,
        });

        // Execute each tool and append results
        for (const auto &tool_call : llm_response.tool_calls) {
            ToolCallResult tool_result = tool_executor_(tool_call);
            if (!tool_result.success) {
                std::string error_message = tool_result.error_message;
                if (error_message.empty()) {
                    error_message =
                        tool_result.content.empty() ? "tool execution failed" : tool_result.content;
                }
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message =
                        "tool call failed for '" + tool_call.name + "': " + error_message,
                    .attempts = static_cast<std::size_t>(total_attempts),
                };
            }
            conversation.push_back(ChatMessage{
                .role = "tool",
                .content = tool_result.content,
                .tool_calls = {},
                .tool_call_id = tool_result.tool_call_id,
            });
        }
    }

    // Exceeded max_tool_rounds
    return runtime::CapabilityCallResult{
        .status = runtime::CapabilityCallStatus::Error,
        .value = std::nullopt,
        .error_message =
            "exceeded max tool call rounds (" + std::to_string(config_.max_tool_rounds) + ")",
        .attempts = static_cast<std::size_t>(total_attempts),
    };
}

void LLMCapabilityProvider::register_all(runtime::CapabilityRegistry &registry) {
    // 遍历所有 CapabilityDecl，注册到 registry
    for (const auto *capability : index_.capabilities()) {
        std::string cap_name = capability->name;
        runtime::CapabilityBinding binding;
        binding.name = cap_name;
        binding.handler = [this, cap_name](const std::vector<evaluator::Value> &args) {
            return this->invoke(cap_name, args);
        };
        registry.register_capability(std::move(binding));
    }
}

runtime::ContextualCapabilityInvoker LLMCapabilityProvider::as_contextual_invoker() {
    return [this](const runtime::CapabilityInvocationContext &context,
                  const std::string &capability_name,
                  const std::vector<evaluator::Value> &args) -> runtime::CapabilityCallResult {
        return invoke_with_context(context, capability_name, args);
    };
}

} // namespace ahfl::llm_provider
