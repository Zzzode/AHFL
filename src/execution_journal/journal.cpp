#include "ahfl/execution_journal/journal.hpp"

#include <string>
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
    case ExecutionJournalEventKind::WorkflowCompleted:
        return "workflow_completed";
    }

    return "<unknown>";
}

enum class NodeEventPhase {
    None,
    BecameReady,
    Started,
    Completed,
};

[[nodiscard]] std::string node_event_key(const ExecutionJournalEvent &event) {
    if (!event.node_name.has_value() || !event.execution_index.has_value()) {
        return {};
    }

    return *event.node_name + "#" + std::to_string(*event.execution_index);
}

} // namespace

ExecutionJournalValidationResult validate_execution_journal(const ExecutionJournal &journal) {
    ExecutionJournalValidationResult result{
        .diagnostics = {},
    };

    if (journal.format_version != kExecutionJournalFormatVersion) {
        result.diagnostics.error(
            "execution journal validation encountered unsupported format_version '" +
            journal.format_version + "'");
    }

    if (journal.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        result.diagnostics.error(
            "execution journal validation encountered unsupported source_runtime_session_format_version '" +
            journal.source_runtime_session_format_version + "'");
    }

    if (journal.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error(
            "execution journal validation encountered unsupported source_execution_plan_format_version '" +
            journal.source_execution_plan_format_version + "'");
    }

    if (journal.workflow_canonical_name.empty()) {
        result.diagnostics.error(
            "execution journal validation contains empty workflow_canonical_name");
    }

    if (journal.session_id.empty()) {
        result.diagnostics.error("execution journal validation contains empty session_id");
    }

    if (journal.events.empty()) {
        result.diagnostics.error("execution journal validation contains no events");
        return result;
    }

    if (journal.events.front().kind != ExecutionJournalEventKind::SessionStarted) {
        result.diagnostics.error(
            "execution journal validation must begin with 'session_started'");
    }

    if (journal.events.back().kind != ExecutionJournalEventKind::WorkflowCompleted) {
        result.diagnostics.error(
            "execution journal validation must end with 'workflow_completed'");
    }

    std::unordered_map<std::string, NodeEventPhase> node_phases;
    std::unordered_set<std::string> completed_node_keys;
    std::optional<std::size_t> previous_execution_index;
    bool saw_workflow_completed = false;

    for (const auto &event : journal.events) {
        const auto kind_name = event_kind_name(event.kind);

        if (event.workflow_canonical_name.empty()) {
            result.diagnostics.error("execution journal validation event '" + kind_name +
                                     "' contains empty workflow_canonical_name");
        } else if (event.workflow_canonical_name != journal.workflow_canonical_name) {
            result.diagnostics.error("execution journal validation event '" + kind_name +
                                     "' workflow_canonical_name does not match journal");
        }

        std::unordered_set<std::string> dependency_names;
        for (const auto &dependency : event.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' contains empty satisfied dependency");
                continue;
            }

            if (!dependency_names.insert(dependency).second) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' contains duplicate satisfied dependency '" +
                                         dependency + "'");
            }
        }

        std::unordered_set<std::string> mock_selectors;
        for (const auto &selector : event.used_mock_selectors) {
            if (selector.empty()) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' contains empty used mock selector");
                continue;
            }

            if (!mock_selectors.insert(selector).second) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' contains duplicate used mock selector '" +
                                         selector + "'");
            }
        }

        if (saw_workflow_completed) {
            result.diagnostics.error(
                "execution journal validation contains events after 'workflow_completed'");
            break;
        }

        switch (event.kind) {
        case ExecutionJournalEventKind::SessionStarted:
            if (event.node_name.has_value()) {
                result.diagnostics.error(
                    "execution journal validation 'session_started' must not contain node_name");
            }
            if (event.execution_index.has_value()) {
                result.diagnostics.error(
                    "execution journal validation 'session_started' must not contain execution_index");
            }
            break;
        case ExecutionJournalEventKind::WorkflowCompleted:
            if (event.node_name.has_value()) {
                result.diagnostics.error(
                    "execution journal validation 'workflow_completed' must not contain node_name");
            }
            if (event.execution_index.has_value()) {
                result.diagnostics.error(
                    "execution journal validation 'workflow_completed' must not contain execution_index");
            }
            saw_workflow_completed = true;
            break;
        case ExecutionJournalEventKind::NodeBecameReady:
        case ExecutionJournalEventKind::NodeStarted:
        case ExecutionJournalEventKind::NodeCompleted: {
            if (!event.node_name.has_value() || event.node_name->empty()) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' requires non-empty node_name");
                break;
            }
            if (!event.execution_index.has_value()) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' requires execution_index");
                break;
            }

            if (previous_execution_index.has_value() &&
                *event.execution_index < *previous_execution_index) {
                result.diagnostics.error("execution journal validation event '" + kind_name +
                                         "' decreases execution_index ordering");
            }
            previous_execution_index = event.execution_index;

            const auto key = node_event_key(event);
            auto &phase = node_phases[key];
            switch (event.kind) {
            case ExecutionJournalEventKind::NodeBecameReady:
                if (phase != NodeEventPhase::None) {
                    result.diagnostics.error("execution journal validation node '" +
                                             *event.node_name +
                                             "' duplicates 'node_became_ready' phase");
                }
                phase = NodeEventPhase::BecameReady;
                break;
            case ExecutionJournalEventKind::NodeStarted:
                if (phase != NodeEventPhase::BecameReady) {
                    result.diagnostics.error("execution journal validation node '" +
                                             *event.node_name +
                                             "' starts before 'node_became_ready'");
                }
                phase = NodeEventPhase::Started;
                break;
            case ExecutionJournalEventKind::NodeCompleted:
                if (phase != NodeEventPhase::Started) {
                    result.diagnostics.error("execution journal validation node '" +
                                             *event.node_name +
                                             "' completes before 'node_started'");
                }
                if (!completed_node_keys.insert(key).second) {
                    result.diagnostics.error("execution journal validation node '" +
                                             *event.node_name +
                                             "' duplicates 'node_completed' phase");
                }
                phase = NodeEventPhase::Completed;
                break;
            case ExecutionJournalEventKind::SessionStarted:
            case ExecutionJournalEventKind::WorkflowCompleted:
                break;
            }
            break;
        }
        }
    }

    for (const auto &[key, phase] : node_phases) {
        if (phase != NodeEventPhase::Completed) {
            result.diagnostics.error("execution journal validation node event sequence '" + key +
                                     "' does not reach 'node_completed'");
        }
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
            result.diagnostics.error("execution journal bootstrap execution_order node '" +
                                     node_name + "' does not exist in runtime session");
            return result;
        }

        const auto &node = *found->second;
        journal.events.push_back(
            ExecutionJournalEvent{
                .kind = ExecutionJournalEventKind::NodeBecameReady,
                .workflow_canonical_name = session.workflow_canonical_name,
                .node_name = node.node_name,
                .execution_index = node.execution_index,
                .satisfied_dependencies = node.satisfied_dependencies,
                .used_mock_selectors = {},
            });
        journal.events.push_back(
            ExecutionJournalEvent{
                .kind = ExecutionJournalEventKind::NodeStarted,
                .workflow_canonical_name = session.workflow_canonical_name,
                .node_name = node.node_name,
                .execution_index = node.execution_index,
                .satisfied_dependencies = node.satisfied_dependencies,
                .used_mock_selectors = {},
            });

        std::vector<std::string> used_mock_selectors;
        used_mock_selectors.reserve(node.used_mocks.size());
        for (const auto &mock : node.used_mocks) {
            used_mock_selectors.push_back(mock.selector);
        }
        journal.events.push_back(
            ExecutionJournalEvent{
                .kind = ExecutionJournalEventKind::NodeCompleted,
                .workflow_canonical_name = session.workflow_canonical_name,
                .node_name = node.node_name,
                .execution_index = node.execution_index,
                .satisfied_dependencies = node.satisfied_dependencies,
                .used_mock_selectors = std::move(used_mock_selectors),
            });
    }

    journal.events.push_back(ExecutionJournalEvent{
        .kind = ExecutionJournalEventKind::WorkflowCompleted,
        .workflow_canonical_name = session.workflow_canonical_name,
        .node_name = std::nullopt,
        .execution_index = std::nullopt,
        .satisfied_dependencies = {},
        .used_mock_selectors = {},
    });

    const auto journal_validation = validate_execution_journal(journal);
    result.diagnostics.append(journal_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.journal = std::move(journal);
    return result;
}

} // namespace ahfl::execution_journal
