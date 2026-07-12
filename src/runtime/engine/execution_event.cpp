#include "ahfl/runtime/execution_event.hpp"

#include <map>
#include <utility>

#include "ahfl/base/support/overloaded.hpp"

namespace ahfl::runtime {

ExecutionEventId ExecutionEventStore::emit(ExecutionEventPayload payload,
                                           std::chrono::nanoseconds monotonic_offset) {
    const ExecutionEventId id{events_.size()};
    events_.push_back(ExecutionEvent{
        .id = id,
        .monotonic_offset = monotonic_offset,
        .payload = std::move(payload),
    });
    return id;
}

const ExecutionEvent *ExecutionEventStore::find(ExecutionEventId id) const noexcept {
    if (!id.valid() || id.index() >= events_.size()) {
        return nullptr;
    }
    return &events_[id.index()];
}

std::span<const ExecutionEvent> ExecutionEventStore::events() const noexcept {
    return events_;
}

std::size_t ExecutionEventStore::size() const noexcept {
    return events_.size();
}

bool ExecutionEventValidationResult::has_issue(
    ExecutionEventValidationIssueKind kind) const noexcept {
    for (const auto &issue : issues) {
        if (issue.kind == kind) {
            return true;
        }
    }
    return false;
}

namespace {

enum class LifecycleKind : std::uint8_t {
    Run,
    Workflow,
    Node,
    Invocation,
};

struct LifecycleKey {
    LifecycleKind kind;
    std::size_t identity;

    [[nodiscard]] friend auto
    operator<=>(const LifecycleKey &, const LifecycleKey &) noexcept = default;
};

struct LifecycleState {
    std::size_t starts{0};
    std::size_t terminals{0};
    ExecutionEventId first_event;
    ExecutionEventId last_event;
};

struct LifecycleObservation {
    enum class Kind {
        None,
        Start,
        Terminal,
    };

    Kind kind{Kind::None};
    LifecycleKey key{LifecycleKind::Run, 0};
};

[[nodiscard]] LifecycleObservation lifecycle_observation(const ExecutionEventPayload &payload) {
    return std::visit(
        ahfl::Overloaded{
            [](const RunStarted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Start,
                    .key = {LifecycleKind::Run, event.run.index()},
                };
            },
            [](const WorkflowStarted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Start,
                    .key = {LifecycleKind::Workflow, event.workflow.index()},
                };
            },
            [](const NodeScheduled &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Start,
                    .key = {LifecycleKind::Node, event.node.index()},
                };
            },
            [](const CapabilityStarted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Start,
                    .key = {LifecycleKind::Invocation, event.invocation.index()},
                };
            },
            [](const RunCompleted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Run, event.run.index()},
                };
            },
            [](const WorkflowCompleted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Workflow, event.workflow.index()},
                };
            },
            [](const WorkflowFailed &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Workflow, event.workflow.index()},
                };
            },
            [](const NodeCompleted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Node, event.node.index()},
                };
            },
            [](const NodeRestored &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Node, event.node.index()},
                };
            },
            [](const NodeFailed &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Node, event.node.index()},
                };
            },
            [](const NodeSkipped &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Node, event.node.index()},
                };
            },
            [](const CapabilityCompleted &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Invocation, event.invocation.index()},
                };
            },
            [](const CapabilityFailed &event) {
                return LifecycleObservation{
                    .kind = LifecycleObservation::Kind::Terminal,
                    .key = {LifecycleKind::Invocation, event.invocation.index()},
                };
            },
            [](const auto &) { return LifecycleObservation{}; },
        },
        payload);
}

} // namespace

ExecutionEventValidationResult
validate_execution_events(std::span<const ExecutionEvent> events) {
    ExecutionEventValidationResult result;
    std::map<LifecycleKey, LifecycleState> lifecycles;
    std::map<InvocationId, std::size_t> invocation_usage_counts;
    std::chrono::nanoseconds previous_offset{0};

    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto &event = events[index];
        if (event.id != ExecutionEventId{index}) {
            result.issues.push_back({
                .kind = ExecutionEventValidationIssueKind::InvalidEventId,
                .event = event.id,
                .identity = index,
            });
        }
        if (index != 0 && event.monotonic_offset < previous_offset) {
            result.issues.push_back({
                .kind = ExecutionEventValidationIssueKind::NonMonotonicOffset,
                .event = event.id,
                .identity = index,
            });
        }
        previous_offset = event.monotonic_offset;

        if (const auto *usage = std::get_if<CapabilityUsageRecorded>(&event.payload)) {
            const auto lifecycle =
                lifecycles.find(LifecycleKey{LifecycleKind::Invocation,
                                             usage->invocation.index()});
            if (lifecycle == lifecycles.end() || lifecycle->second.starts == 0 ||
                lifecycle->second.terminals != 0) {
                result.issues.push_back({
                    .kind = ExecutionEventValidationIssueKind::UsageOutsideInvocation,
                    .event = event.id,
                    .identity = usage->invocation.index(),
                });
            }
            auto &count = invocation_usage_counts[usage->invocation];
            ++count;
            if (count > 1) {
                result.issues.push_back({
                    .kind = ExecutionEventValidationIssueKind::DuplicateUsage,
                    .event = event.id,
                    .identity = usage->invocation.index(),
                });
            }
        }

        const auto observation = lifecycle_observation(event.payload);
        if (observation.kind == LifecycleObservation::Kind::None) {
            continue;
        }
        auto &state = lifecycles[observation.key];
        if (!state.first_event.valid()) {
            state.first_event = event.id;
        }
        state.last_event = event.id;

        if (observation.kind == LifecycleObservation::Kind::Start) {
            ++state.starts;
            if (state.starts > 1) {
                result.issues.push_back({
                    .kind = ExecutionEventValidationIssueKind::DuplicateStart,
                    .event = event.id,
                    .identity = observation.key.identity,
                });
            }
            continue;
        }

        ++state.terminals;
        if (state.starts == 0) {
            result.issues.push_back({
                .kind = ExecutionEventValidationIssueKind::TerminalWithoutStart,
                .event = event.id,
                .identity = observation.key.identity,
            });
        } else if (state.terminals > 1) {
            result.issues.push_back({
                .kind = ExecutionEventValidationIssueKind::DuplicateTerminal,
                .event = event.id,
                .identity = observation.key.identity,
            });
        }
    }

    for (const auto &[key, state] : lifecycles) {
        if (state.starts != 0 && state.terminals == 0) {
            result.issues.push_back({
                .kind = ExecutionEventValidationIssueKind::MissingTerminal,
                .event = state.first_event,
                .identity = key.identity,
            });
        }
    }

    return result;
}

} // namespace ahfl::runtime
