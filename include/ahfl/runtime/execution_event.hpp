#pragma once

#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::runtime {

template <typename Tag> class RuntimeIndex {
  public:
    constexpr RuntimeIndex() noexcept = default;
    explicit constexpr RuntimeIndex(std::size_t index) noexcept : index_(index) {}

    [[nodiscard]] constexpr std::size_t index() const noexcept {
        return index_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index_ != kInvalidIndex;
    }

    [[nodiscard]] friend constexpr bool
    operator==(RuntimeIndex lhs, RuntimeIndex rhs) noexcept = default;
    [[nodiscard]] friend constexpr auto
    operator<=>(RuntimeIndex lhs, RuntimeIndex rhs) noexcept = default;

  private:
    static constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();
    std::size_t index_{kInvalidIndex};
};

struct RunIdTag;
struct WorkflowIdTag;
struct WorkflowNodeIdTag;
struct AgentIdTag;
struct CapabilityIdTag;
struct ProviderIdTag;
struct AgentStateIdTag;
struct InvocationIdTag;
struct RuntimeValueIdTag;
struct DiagnosticIdTag;
struct ExecutionEventIdTag;
struct CheckpointIdTag;

using RunId = RuntimeIndex<RunIdTag>;
using WorkflowId = RuntimeIndex<WorkflowIdTag>;
using WorkflowNodeId = RuntimeIndex<WorkflowNodeIdTag>;
using AgentId = RuntimeIndex<AgentIdTag>;
using CapabilityId = RuntimeIndex<CapabilityIdTag>;
using ProviderId = RuntimeIndex<ProviderIdTag>;
using AgentStateId = RuntimeIndex<AgentStateIdTag>;
using InvocationId = RuntimeIndex<InvocationIdTag>;
using RuntimeValueId = RuntimeIndex<RuntimeValueIdTag>;
using DiagnosticId = RuntimeIndex<DiagnosticIdTag>;
using ExecutionEventId = RuntimeIndex<ExecutionEventIdTag>;
using CheckpointId = RuntimeIndex<CheckpointIdTag>;

enum class RunTerminalStatus {
    Completed,
    Failed,
    Cancelled,
    Interrupted,
};

enum class CapabilityFailureKind {
    Error,
    Timeout,
    RetryExhausted,
    BudgetRejected,
    Cancelled,
    Interrupted,
};

enum class ProviderDegradationReason {
    Error,
    Timeout,
    RetryExhausted,
    BudgetRejected,
};

enum class NodeFailureKind {
    AgentFailed,
    EvaluationFailed,
    CapabilityFailed,
    BudgetRejected,
    Cancelled,
    Interrupted,
};

enum class WorkflowFailureKind {
    NodeFailed,
    DependencyFailed,
    EvaluationFailed,
    BudgetRejected,
    Cancelled,
    Interrupted,
};

struct RunStarted {
    RunId run;
};

struct RunResumed {
    RunId run;
    CheckpointId checkpoint;
};

struct WorkflowStarted {
    RunId run;
    WorkflowId workflow;
};

struct NodeScheduled {
    WorkflowId workflow;
    WorkflowNodeId node;
    std::vector<WorkflowNodeId> dependencies;
    std::size_t execution_slot{0};
};

struct NodeStarted {
    WorkflowNodeId node;
    AgentId agent;
};

struct AgentStateEntered {
    WorkflowNodeId node;
    AgentId agent;
    AgentStateId state;
};

struct CapabilityStarted {
    InvocationId invocation;
    WorkflowNodeId node;
    CapabilityId capability;
    ProviderId provider;
    std::size_t attempt{1};
};

struct CapabilityCompleted {
    InvocationId invocation;
    std::optional<RuntimeValueId> output;
    std::size_t attempts{1};
    bool cache_hit{false};
};

struct CapabilityPolicyNotice {
    std::string diagnostic_code;
    std::string message;
};

struct CapabilityUsageRecorded {
    InvocationId invocation;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double total_cost_usd{0.0};
    bool cost_estimated{false};
    std::vector<CapabilityPolicyNotice> notices;
};

struct CapabilityFailed {
    InvocationId invocation;
    CapabilityFailureKind kind{CapabilityFailureKind::Error};
    std::optional<DiagnosticId> diagnostic;
    std::size_t attempts{1};
    bool retryable{false};
};

struct CapabilityRetryScheduled {
    InvocationId previous_invocation;
    InvocationId next_invocation;
    std::size_t next_attempt{1};
};

struct ProviderDegraded {
    InvocationId invocation;
    ProviderId provider;
    ProviderId fallback_provider;
    ProviderDegradationReason reason{ProviderDegradationReason::Error};
};

struct NodeCompleted {
    WorkflowNodeId node;
    std::optional<RuntimeValueId> output;
};

struct NodeRestored {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<RuntimeValueId> output;
    CheckpointId checkpoint;
};

struct NodeFailed {
    WorkflowNodeId node;
    DiagnosticId diagnostic;
    NodeFailureKind kind{NodeFailureKind::AgentFailed};
};

struct NodeSkipped {
    WorkflowNodeId node;
    std::vector<WorkflowNodeId> blocking_dependencies;
};

struct WorkflowCompleted {
    WorkflowId workflow;
    std::optional<RuntimeValueId> output;
};

struct WorkflowFailed {
    WorkflowId workflow;
    DiagnosticId diagnostic;
    WorkflowFailureKind kind{WorkflowFailureKind::NodeFailed};
};

struct CheckpointSaved {
    RunId run;
    CheckpointId checkpoint;
};

struct RunCancellationRequested {
    RunId run;
};

struct RunInterrupted {
    RunId run;
};

struct RunCompleted {
    RunId run;
    RunTerminalStatus status{RunTerminalStatus::Completed};
};

using ExecutionEventPayload = std::variant<RunStarted,
                                           RunResumed,
                                           WorkflowStarted,
                                           NodeScheduled,
                                           NodeStarted,
                                           AgentStateEntered,
                                           CapabilityStarted,
                                           CapabilityUsageRecorded,
                                           CapabilityCompleted,
                                           CapabilityFailed,
                                           CapabilityRetryScheduled,
                                           ProviderDegraded,
                                           NodeCompleted,
                                           NodeRestored,
                                           NodeFailed,
                                           NodeSkipped,
                                           WorkflowCompleted,
                                           WorkflowFailed,
                                           CheckpointSaved,
                                           RunCancellationRequested,
                                           RunInterrupted,
                                           RunCompleted>;

struct ExecutionEvent {
    ExecutionEventId id;
    std::chrono::nanoseconds monotonic_offset{0};
    ExecutionEventPayload payload;
};

class ExecutionEventSink {
  public:
    virtual ~ExecutionEventSink() = default;

    [[nodiscard]] virtual ExecutionEventId
    emit(ExecutionEventPayload payload, std::chrono::nanoseconds monotonic_offset) = 0;
};

class ExecutionEventStore final : public ExecutionEventSink {
  public:
    [[nodiscard]] ExecutionEventId
    emit(ExecutionEventPayload payload, std::chrono::nanoseconds monotonic_offset) override;

    template <typename Payload>
        requires std::constructible_from<ExecutionEventPayload, Payload>
    [[nodiscard]] ExecutionEventId
    append(Payload payload, std::chrono::nanoseconds monotonic_offset) {
        return emit(ExecutionEventPayload{std::move(payload)}, monotonic_offset);
    }

    [[nodiscard]] const ExecutionEvent *find(ExecutionEventId id) const noexcept;
    [[nodiscard]] std::span<const ExecutionEvent> events() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<ExecutionEvent> events_;
};

enum class ExecutionEventValidationIssueKind {
    InvalidEventId,
    NonMonotonicOffset,
    DuplicateStart,
    MissingTerminal,
    DuplicateTerminal,
    TerminalWithoutStart,
    UsageOutsideInvocation,
    DuplicateUsage,
};

struct ExecutionEventValidationIssue {
    ExecutionEventValidationIssueKind kind;
    ExecutionEventId event;
    std::size_t identity{0};
};

struct ExecutionEventValidationResult {
    std::vector<ExecutionEventValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }

    [[nodiscard]] bool has_issue(ExecutionEventValidationIssueKind kind) const noexcept;
};

[[nodiscard]] ExecutionEventValidationResult
validate_execution_events(std::span<const ExecutionEvent> events);

} // namespace ahfl::runtime
