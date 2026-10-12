#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/providers/llm/tool_calling.hpp"

namespace ahfl::llm_provider {

struct SessionMessage {
    std::string role;
    std::string content;
    std::vector<ToolCall> tool_calls{};
    std::string tool_call_id{};
};

enum class ToolReceiptState {
    Prepared,
    InFlight,
    Completed
};
struct SessionToolReceipt {
    std::uint64_t turn{0};
    std::uint64_t ordinal{0};
    std::uint64_t definition{0};
    ToolReceiptState state{ToolReceiptState::Prepared};
    ToolCallResult result{};
    ToolCall call{};
    std::string schema_digest{};
    std::uint64_t idempotency_key{0};
};

struct SessionTerminalFailure {
    runtime::CapabilityCallStatus status{runtime::CapabilityCallStatus::Error};
    std::optional<runtime::CapabilityFailureKind> failure_kind{};
    std::string error_text{};
    std::string diagnostic_code{};
    bool usage_present{false};
};

struct LLMSession {
    std::uint64_t id{0};
    std::string binding;
    std::uint64_t workflow{0};
    std::uint64_t node{0};
    std::uint64_t capability{0};
    std::uint64_t ordinal{0};
    bool prepared{true};
    bool completed{false};
    std::vector<SessionMessage> history{};
    std::size_t active_begin{2};
    std::string summary{};
    std::size_t summary_through{2};
    std::size_t round{0};
    std::vector<SessionToolReceipt> receipts{};
    std::string completed_content{};
    std::string completed_wire{};
    std::string completed_wire_digest{};
    std::string request_digest{};
    std::string response_wire{};
    bool response_pending{false};
    bool summary_request{false};
    std::size_t summary_prefix_end{2};
    std::size_t summary_before_tokens{0};
    std::optional<SessionTerminalFailure> terminal_failure{};
    bool request_in_flight{false};
    std::size_t reserved_tokens{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double total_cost_usd{0};
    bool cost_estimated{false};
    std::size_t attempts{0};
    std::vector<runtime::CapabilityPolicyNotice> usage_notices{};
    bool cache_hit{false};
    bool provider_degraded{false};
    std::string degraded_provider_name{};
    std::string selected_provider_name{};
};

/// Authenticated local artifacts. HMAC provides integrity, not confidentiality.
/// The CLI owns the namespace lease; one writer may access a store at a time.
class LLMSessionStore {
  public:
    [[nodiscard]] static std::expected<std::shared_ptr<LLMSessionStore>, std::string>
    open(const std::filesystem::path &root, support::Sha256Digest run_binding = {});
    [[nodiscard]] std::expected<std::optional<LLMSession>, std::string>
    load(std::uint64_t id, std::string_view expected_binding) const;
    [[nodiscard]] std::expected<std::optional<std::uint64_t>, std::string>
    resolve_id(std::string_view binding, bool create) const;
    [[nodiscard]] std::expected<void, std::string> save(const LLMSession &session) const;
    [[nodiscard]] std::expected<std::vector<LLMSession>, std::string> sessions() const;
    [[nodiscard]] support::Sha256Digest namespace_digest() const {
        return support::sha256(key_);
    }
    [[nodiscard]] const std::filesystem::path &root() const {
        return root_;
    }

  private:
    std::filesystem::path root_;
    support::Sha256Digest key_{};
    support::Sha256Digest run_binding_{};
};

/// A byte-complete, UTF-8-boundary-safe view of immutable history. Numeric history
/// and chunk indices are also exposed by the provider's read-only history tool.
[[nodiscard]] std::expected<std::string, std::string> session_history_chunk(
    const LLMSession &session, std::uint64_t history_id, std::uint64_t chunk_index);
[[nodiscard]] std::string session_message_json(const SessionMessage &message);
} // namespace ahfl::llm_provider
