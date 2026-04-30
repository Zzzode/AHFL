#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::execution_journal {

namespace {

[[nodiscard]] std::string event_kind_name(ExecutionJournalEventKind kind) {
    switch (kind) {
    case ExecutionJournalEventKind::SessionStarted:
        return "session_started";
    case ExecutionJournalEventKind::NodeBecameReady:
        return "node_became_ready";
    case ExecutionJournalEventKind::NodeStarted:
        return "node_started";
    case ExecutionJournalEventKind::NodeCompleted:
        return "node_completed";
    case ExecutionJournalEventKind::MockMissing:
        return "mock_missing";
    case ExecutionJournalEventKind::NodeFailed:
        return "node_failed";
    case ExecutionJournalEventKind::WorkflowCompleted:
        return "workflow_completed";
    case ExecutionJournalEventKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "<unknown>";
}

enum class NodeEventPhase {
    None,
    BecameReady,
    Started,
    Completed,
    Failed,
};

[[nodiscard]] std::string node_event_key(const ExecutionJournalEvent &event) {
    if (!event.node_name.has_value() || !event.execution_index.has_value()) {
        return {};
    }

    return *event.node_name + "#" + std::to_string(*event.execution_index);
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "execution journal validation");
}

} // namespace

ExecutionJournalValidationResult validate_execution_journal(const ExecutionJournal &journal) {
    ExecutionJournalValidationResult result{
        .diagnostics = {},
    };

    if (journal.format_version != kExecutionJournalFormatVersion) {
        result.diagnostics.error()
            .message("execution journal validation encountered unsupported format_version '" +
                     journal.format_version + "'")
            .emit();
    }

    if (journal.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        result.diagnostics.error()
            .message("execution journal validation encountered unsupported "
                     "source_runtime_session_format_version '" +
                     journal.source_runtime_session_format_version + "'")
            .emit();
    }

    if (journal.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error()
            .message("execution journal validation encountered unsupported "
                     "source_execution_plan_format_version '" +
                     journal.source_execution_plan_format_version + "'")
            .emit();
    }

    if (journal.workflow_canonical_name.empty()) {
        result.diagnostics.error()
            .message("execution journal validation contains empty workflow_canonical_name")
            .emit();
    }

    if (journal.session_id.empty()) {
        result.diagnostics.error()
            .message("execution journal validation contains empty session_id")
            .emit();
    }

    if (journal.events.empty()) {
        result.diagnostics.error()
            .message("execution journal validation contains no events")
            .emit();
        return result;
    }

    if (journal.events.front().kind != ExecutionJournalEventKind::SessionStarted) {
        result.diagnostics.error()
            .message("execution journal validation must begin with 'session_started'")
            .emit();
    }

    std::unordered_map<std::string, NodeEventPhase> node_phases;
    std::unordered_set<std::string> completed_node_keys;
    std::unordered_set<std::string> failed_node_keys;
    std::optional<std::size_t> previous_execution_index;
    std::optional<ExecutionJournalEventKind> terminal_workflow_kind;
    std::string terminal_workflow_name;

    for (const auto &event : journal.events) {
        const auto kind_name = event_kind_name(event.kind);

        if (event.workflow_canonical_name.empty()) {
            result.diagnostics.error()
                .message("execution journal validation event '" + kind_name +
                         "' contains empty workflow_canonical_name")
                .emit();
        } else if (event.workflow_canonical_name != journal.workflow_canonical_name) {
            result.diagnostics.error()
                .message("execution journal validation event '" + kind_name +
                         "' workflow_canonical_name does not match journal")
                .emit();
        }

        std::unordered_set<std::string> dependency_names;
        for (const auto &dependency : event.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' contains empty satisfied dependency")
                    .emit();
                continue;
            }

            if (!dependency_names.insert(dependency).second) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' contains duplicate satisfied dependency '" + dependency + "'")
                    .emit();
            }
        }

        std::unordered_set<std::string> mock_selectors;
        for (const auto &selector : event.used_mock_selectors) {
            if (selector.empty()) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' contains empty used mock selector")
                    .emit();
                continue;
            }

            if (!mock_selectors.insert(selector).second) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' contains duplicate used mock selector '" + selector + "'")
                    .emit();
            }
        }

        if (event.failure_summary.has_value()) {
            validate_failure_summary(
                *event.failure_summary, "event '" + kind_name + "'", result.diagnostics);
        }

        if (terminal_workflow_kind.has_value()) {
            result.diagnostics.error()
                .message("execution journal validation contains events after '" +
                         terminal_workflow_name + "'")
                .emit();
            break;
        }

        switch (event.kind) {
        case ExecutionJournalEventKind::SessionStarted:
            if (event.node_name.has_value()) {
                result.diagnostics.error()
                    .message(
                        "execution journal validation 'session_started' must not contain node_name")
                    .emit();
            }
            if (event.execution_index.has_value()) {
                result.diagnostics.error()
                    .message("execution journal validation 'session_started' must not contain "
                             "execution_index")
                    .emit();
            }
            if (event.failure_summary.has_value()) {
                result.diagnostics.error()
                    .message("execution journal validation 'session_started' must not contain "
                             "failure_summary")
                    .emit();
            }
            break;
        case ExecutionJournalEventKind::WorkflowCompleted:
        case ExecutionJournalEventKind::WorkflowFailed:
            if (event.node_name.has_value()) {
                result.diagnostics.error()
                    .message("execution journal validation '" + kind_name +
                             "' must not contain node_name")
                    .emit();
            }
            if (event.execution_index.has_value()) {
                result.diagnostics.error()
                    .message("execution journal validation '" + kind_name +
                             "' must not contain execution_index")
                    .emit();
            }
            if (event.kind == ExecutionJournalEventKind::WorkflowCompleted) {
                if (event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation 'workflow_completed' must not "
                                 "contain failure_summary")
                        .emit();
                }
            } else {
                if (!event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation 'workflow_failed' requires "
                                 "failure_summary")
                        .emit();
                } else if (event.failure_summary->kind !=
                           runtime_session::RuntimeFailureKind::WorkflowFailed) {
                    result.diagnostics.error()
                        .message("execution journal validation 'workflow_failed' failure_summary "
                                 "kind must be WorkflowFailed")
                        .emit();
                }
            }
            terminal_workflow_kind = event.kind;
            terminal_workflow_name = kind_name;
            break;
        case ExecutionJournalEventKind::NodeBecameReady:
        case ExecutionJournalEventKind::NodeStarted:
        case ExecutionJournalEventKind::NodeCompleted:
        case ExecutionJournalEventKind::MockMissing:
        case ExecutionJournalEventKind::NodeFailed: {
            if (!event.node_name.has_value() || event.node_name->empty()) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' requires non-empty node_name")
                    .emit();
                break;
            }
            if (!event.execution_index.has_value()) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' requires execution_index")
                    .emit();
                break;
            }

            if (previous_execution_index.has_value() &&
                *event.execution_index < *previous_execution_index) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' decreases execution_index ordering")
                    .emit();
            }
            previous_execution_index = event.execution_index;

            const auto key = node_event_key(event);
            auto &phase = node_phases[key];
            if (event.failure_summary.has_value() && event.failure_summary->node_name.has_value() &&
                *event.failure_summary->node_name != *event.node_name) {
                result.diagnostics.error()
                    .message("execution journal validation event '" + kind_name +
                             "' failure_summary node_name does not match node_name")
                    .emit();
            }
            switch (event.kind) {
            case ExecutionJournalEventKind::NodeBecameReady:
                if (phase != NodeEventPhase::None) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' duplicates 'node_became_ready' phase")
                        .emit();
                }
                if (event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation event '" + kind_name +
                                 "' must not contain failure_summary")
                        .emit();
                }
                phase = NodeEventPhase::BecameReady;
                break;
            case ExecutionJournalEventKind::NodeStarted:
                if (phase != NodeEventPhase::BecameReady) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' starts before 'node_became_ready'")
                        .emit();
                }
                if (event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation event '" + kind_name +
                                 "' must not contain failure_summary")
                        .emit();
                }
                phase = NodeEventPhase::Started;
                break;
            case ExecutionJournalEventKind::NodeCompleted:
                if (phase != NodeEventPhase::Started) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' completes before 'node_started'")
                        .emit();
                }
                if (!completed_node_keys.insert(key).second) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' duplicates 'node_completed' phase")
                        .emit();
                }
                if (event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation event '" + kind_name +
                                 "' must not contain failure_summary")
                        .emit();
                }
                phase = NodeEventPhase::Completed;
                break;
            case ExecutionJournalEventKind::MockMissing:
            case ExecutionJournalEventKind::NodeFailed:
                if (phase != NodeEventPhase::Started) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' fails before 'node_started'")
                        .emit();
                }
                if (!event.failure_summary.has_value()) {
                    result.diagnostics.error()
                        .message("execution journal validation event '" + kind_name +
                                 "' requires failure_summary")
                        .emit();
                } else {
                    const auto expected_failure_kind =
                        event.kind == ExecutionJournalEventKind::MockMissing
                            ? runtime_session::RuntimeFailureKind::MockMissing
                            : runtime_session::RuntimeFailureKind::NodeFailed;
                    if (event.failure_summary->kind != expected_failure_kind) {
                        result.diagnostics.error()
                            .message("execution journal validation event '" + kind_name +
                                     "' failure_summary kind does not match event kind")
                            .emit();
                    }
                }
                if (!failed_node_keys.insert(key).second) {
                    result.diagnostics.error()
                        .message("execution journal validation node '" + *event.node_name +
                                 "' duplicates failure terminal phase")
                        .emit();
                }
                phase = NodeEventPhase::Failed;
                break;
            case ExecutionJournalEventKind::SessionStarted:
            case ExecutionJournalEventKind::WorkflowFailed:
            case ExecutionJournalEventKind::WorkflowCompleted:
                break;
            }
            break;
        }
        }
    }

    for (const auto &[key, phase] : node_phases) {
        if (phase != NodeEventPhase::Completed && phase != NodeEventPhase::Failed) {
            result.diagnostics.error()
                .message("execution journal validation node event sequence '" + key +
                         "' does not reach terminal node phase")
                .emit();
        }
    }

    if (terminal_workflow_kind == ExecutionJournalEventKind::WorkflowCompleted &&
        !failed_node_keys.empty()) {
        result.diagnostics.error()
            .message("execution journal validation 'workflow_completed' must not follow failed "
                     "node events")
            .emit();
    }

    if (terminal_workflow_kind == ExecutionJournalEventKind::WorkflowFailed &&
        failed_node_keys.empty()) {
        result.diagnostics.error()
            .message("execution journal validation 'workflow_failed' must follow at least one "
                     "failed node event")
            .emit();
    }

    if (!terminal_workflow_kind.has_value() && !failed_node_keys.empty()) {
        result.diagnostics.error()
            .message("execution journal validation contains failed node events but does not end "
                     "with 'workflow_failed'")
            .emit();
    }

    return result;
}

ExecutionJournalResult build_execution_journal(const runtime_session::RuntimeSession &session) {
    ExecutionJournalResult result{
        .journal = std::nullopt,
        .diagnostics = {},
    };

    const auto session_validation = runtime_session::validate_runtime_session(session);
    result.diagnostics.append(session_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ExecutionJournal journal{
        .format_version = std::string(kExecutionJournalFormatVersion),
        .source_runtime_session_format_version = session.format_version,
        .source_execution_plan_format_version = session.source_execution_plan_format_version,
        .source_package_identity = session.source_package_identity,
        .workflow_canonical_name = session.workflow_canonical_name,
        .session_id = session.session_id,
        .run_id = session.run_id,
        .events = {},
    };
    journal.events.reserve(2 + session.execution_order.size() * 3);
    journal.events.push_back(ExecutionJournalEvent{
        .kind = ExecutionJournalEventKind::SessionStarted,
        .workflow_canonical_name = session.workflow_canonical_name,
        .node_name = std::nullopt,
        .execution_index = std::nullopt,
        .failure_summary = std::nullopt,
        .satisfied_dependencies = {},
        .used_mock_selectors = {},
    });

    std::unordered_map<std::string, const runtime_session::RuntimeSessionNode *> nodes_by_name;
    for (const auto &node : session.nodes) {
        nodes_by_name.emplace(node.node_name, &node);
    }

    for (const auto &node_name : session.execution_order) {
        const auto found = nodes_by_name.find(node_name);
        if (found == nodes_by_name.end()) {
            result.diagnostics.error()
                .message("execution journal bootstrap execution_order node '" + node_name +
                         "' does not exist in runtime session")
                .emit();
            return result;
        }

        const auto &node = *found->second;
        journal.events.push_back(ExecutionJournalEvent{
            .kind = ExecutionJournalEventKind::NodeBecameReady,
            .workflow_canonical_name = session.workflow_canonical_name,
            .node_name = node.node_name,
            .execution_index = node.execution_index,
            .failure_summary = std::nullopt,
            .satisfied_dependencies = node.satisfied_dependencies,
            .used_mock_selectors = {},
        });
        journal.events.push_back(ExecutionJournalEvent{
            .kind = ExecutionJournalEventKind::NodeStarted,
            .workflow_canonical_name = session.workflow_canonical_name,
            .node_name = node.node_name,
            .execution_index = node.execution_index,
            .failure_summary = std::nullopt,
            .satisfied_dependencies = node.satisfied_dependencies,
            .used_mock_selectors = {},
        });

        std::vector<std::string> used_mock_selectors;
        used_mock_selectors.reserve(node.used_mocks.size());
        for (const auto &mock : node.used_mocks) {
            used_mock_selectors.push_back(mock.selector);
        }

        if (node.status == runtime_session::NodeSessionStatus::Completed) {
            journal.events.push_back(ExecutionJournalEvent{
                .kind = ExecutionJournalEventKind::NodeCompleted,
                .workflow_canonical_name = session.workflow_canonical_name,
                .node_name = node.node_name,
                .execution_index = node.execution_index,
                .failure_summary = std::nullopt,
                .satisfied_dependencies = node.satisfied_dependencies,
                .used_mock_selectors = std::move(used_mock_selectors),
            });
            continue;
        }

        if (node.status == runtime_session::NodeSessionStatus::Failed) {
            const auto event_kind = node.failure_summary.has_value() &&
                                            node.failure_summary->kind ==
                                                runtime_session::RuntimeFailureKind::MockMissing
                                        ? ExecutionJournalEventKind::MockMissing
                                        : ExecutionJournalEventKind::NodeFailed;
            journal.events.push_back(ExecutionJournalEvent{
                .kind = event_kind,
                .workflow_canonical_name = session.workflow_canonical_name,
                .node_name = node.node_name,
                .execution_index = node.execution_index,
                .failure_summary = node.failure_summary,
                .satisfied_dependencies = node.satisfied_dependencies,
                .used_mock_selectors = std::move(used_mock_selectors),
            });
            continue;
        }

        result.diagnostics.error()
            .message("execution journal bootstrap execution_order node '" + node_name +
                     "' is not in executable terminal status")
            .emit();
        return result;
    }

    switch (session.workflow_status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        journal.events.push_back(ExecutionJournalEvent{
            .kind = ExecutionJournalEventKind::WorkflowCompleted,
            .workflow_canonical_name = session.workflow_canonical_name,
            .node_name = std::nullopt,
            .execution_index = std::nullopt,
            .failure_summary = std::nullopt,
            .satisfied_dependencies = {},
            .used_mock_selectors = {},
        });
        break;
    case runtime_session::WorkflowSessionStatus::Failed:
        journal.events.push_back(ExecutionJournalEvent{
            .kind = ExecutionJournalEventKind::WorkflowFailed,
            .workflow_canonical_name = session.workflow_canonical_name,
            .node_name = std::nullopt,
            .execution_index = std::nullopt,
            .failure_summary = session.failure_summary,
            .satisfied_dependencies = {},
            .used_mock_selectors = {},
        });
        break;
    case runtime_session::WorkflowSessionStatus::Partial:
        break;
    }

    const auto journal_validation = validate_execution_journal(journal);
    result.diagnostics.append(journal_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.journal = std::move(journal);
    return result;
}

} // namespace ahfl::execution_journal
