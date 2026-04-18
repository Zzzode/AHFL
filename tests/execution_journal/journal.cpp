#include "ahfl/execution_journal/journal.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] ahfl::execution_journal::ExecutionJournal make_valid_journal() {
    return ahfl::execution_journal::ExecutionJournal{
        .format_version = std::string(ahfl::execution_journal::kExecutionJournalFormatVersion),
        .source_runtime_session_format_version =
            std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
        .source_execution_plan_format_version =
            std::string(ahfl::handoff::kExecutionPlanFormatVersion),
        .source_package_identity =
            ahfl::handoff::PackageIdentity{
                .format_version = std::string(ahfl::handoff::kFormatVersion),
                .name = "workflow-value-flow",
                .version = "0.2.0",
            },
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .session_id = "run-001",
        .run_id = "run-001",
        .events =
            {
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::SessionStarted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::nullopt,
                    .execution_index = std::nullopt,
                    .satisfied_dependencies = {},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeBecameReady,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("first"),
                    .execution_index = 0,
                    .satisfied_dependencies = {},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("first"),
                    .execution_index = 0,
                    .satisfied_dependencies = {},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeCompleted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("first"),
                    .execution_index = 0,
                    .satisfied_dependencies = {},
                    .used_mock_selectors = {"binding:runtime.echo"},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeBecameReady,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("second"),
                    .execution_index = 1,
                    .satisfied_dependencies = {"first"},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("second"),
                    .execution_index = 1,
                    .satisfied_dependencies = {"first"},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeCompleted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::string("second"),
                    .execution_index = 1,
                    .satisfied_dependencies = {"first"},
                    .used_mock_selectors = {},
                },
                ahfl::execution_journal::ExecutionJournalEvent{
                    .kind = ahfl::execution_journal::ExecutionJournalEventKind::WorkflowCompleted,
                    .workflow_canonical_name = "app::main::ValueFlowWorkflow",
                    .node_name = std::nullopt,
                    .execution_index = std::nullopt,
                    .satisfied_dependencies = {},
                    .used_mock_selectors = {},
                },
            },
    };
}

[[nodiscard]] ahfl::runtime_session::RuntimeSession make_valid_runtime_session() {
    return ahfl::runtime_session::RuntimeSession{
        .format_version = std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
        .source_execution_plan_format_version =
            std::string(ahfl::handoff::kExecutionPlanFormatVersion),
        .source_package_identity =
            ahfl::handoff::PackageIdentity{
                .format_version = std::string(ahfl::handoff::kFormatVersion),
                .name = "workflow-value-flow",
                .version = "0.2.0",
            },
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .session_id = "run-001",
        .run_id = "run-001",
        .workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed,
        .input_fixture = "fixture.request.basic",
        .options =
            ahfl::runtime_session::RuntimeSessionOptions{
                .sequential_mode = true,
            },
        .execution_order = {"first", "second"},
        .nodes =
            {
                ahfl::runtime_session::RuntimeSessionNode{
                    .node_name = "first",
                    .target = "lib::agents::AliasAgent",
                    .status = ahfl::runtime_session::NodeSessionStatus::Completed,
                    .execution_index = 0,
                    .satisfied_dependencies = {},
                    .lifecycle =
                        ahfl::handoff::WorkflowNodeLifecycleSummary{
                            .start_condition =
                                ahfl::handoff::WorkflowNodeStartConditionKind::Immediate,
                            .completion_condition =
                                ahfl::handoff::WorkflowNodeCompletionConditionKind::
                                    TargetReachedFinalState,
                            .completion_latched = true,
                            .target_initial_state = "Idle",
                            .target_final_states = {"Done"},
                        },
                    .input_summary = {},
                    .capability_bindings =
                        {
                            ahfl::handoff::CapabilityBindingReference{
                                .capability_name = "lib::agents::Echo",
                                .binding_key = "runtime.echo",
                            },
                        },
                    .used_mocks =
                        {
                            ahfl::runtime_session::RuntimeSessionMockUsage{
                                .selector = "binding:runtime.echo",
                                .capability_name = std::nullopt,
                                .binding_key = "runtime.echo",
                                .result_fixture = "fixture.echo.ok",
                                .invocation_label = "echo-1",
                            },
                        },
                },
                ahfl::runtime_session::RuntimeSessionNode{
                    .node_name = "second",
                    .target = "lib::agents::AliasAgent",
                    .status = ahfl::runtime_session::NodeSessionStatus::Completed,
                    .execution_index = 1,
                    .satisfied_dependencies = {"first"},
                    .lifecycle =
                        ahfl::handoff::WorkflowNodeLifecycleSummary{
                            .start_condition =
                                ahfl::handoff::WorkflowNodeStartConditionKind::
                                    AfterDependenciesCompleted,
                            .completion_condition =
                                ahfl::handoff::WorkflowNodeCompletionConditionKind::
                                    TargetReachedFinalState,
                            .completion_latched = true,
                            .target_initial_state = "Idle",
                            .target_final_states = {"Done"},
                        },
                    .input_summary = {},
                    .capability_bindings = {},
                    .used_mocks = {},
                },
            },
        .return_summary = {},
    };
}

int run_validate_execution_journal_ok() {
    const auto journal = make_valid_journal();
    const auto validation = ahfl::execution_journal::validate_execution_journal(journal);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_validate_execution_journal_rejects_out_of_order_node_phase() {
    auto journal = make_valid_journal();
    journal.events[1].kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted;

    const auto validation = ahfl::execution_journal::validate_execution_journal(journal);
    if (!validation.has_errors()) {
        std::cerr << "expected out-of-order node phase validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(validation.diagnostics,
                             "execution journal validation node 'first' starts before 'node_became_ready'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing out-of-order node phase diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_execution_journal_rejects_non_monotonic_execution_index() {
    auto journal = make_valid_journal();
    journal.events[1].execution_index = 1;
    journal.events[2].execution_index = 1;
    journal.events[3].execution_index = 1;
    journal.events[4].execution_index = 0;
    journal.events[5].execution_index = 0;
    journal.events[6].execution_index = 0;

    const auto validation = ahfl::execution_journal::validate_execution_journal(journal);
    if (!validation.has_errors()) {
        std::cerr << "expected non-monotonic execution_index validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(validation.diagnostics,
                             "execution journal validation event 'node_became_ready' decreases execution_index ordering")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing non-monotonic execution_index diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_execution_journal_rejects_events_after_workflow_completed() {
    auto journal = make_valid_journal();
    journal.events.push_back(ahfl::execution_journal::ExecutionJournalEvent{
        .kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted,
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .node_name = std::string("late"),
        .execution_index = 2,
        .satisfied_dependencies = {},
        .used_mock_selectors = {},
    });

    const auto validation = ahfl::execution_journal::validate_execution_journal(journal);
    if (!validation.has_errors()) {
        std::cerr << "expected events-after-workflow-completed validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(validation.diagnostics,
                             "execution journal validation contains events after 'workflow_completed'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing events-after-workflow-completed diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_execution_journal_from_runtime_session_ok() {
    const auto result =
        ahfl::execution_journal::build_execution_journal(make_valid_runtime_session());
    if (result.has_errors() || !result.journal.has_value()) {
        result.diagnostics.render(std::cout);
        return 1;
    }

    const auto &journal = *result.journal;
    if (journal.format_version != ahfl::execution_journal::kExecutionJournalFormatVersion ||
        journal.source_runtime_session_format_version !=
            ahfl::runtime_session::kRuntimeSessionFormatVersion ||
        journal.source_execution_plan_format_version != ahfl::handoff::kExecutionPlanFormatVersion ||
        journal.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        journal.session_id != "run-001" || !journal.run_id.has_value() ||
        *journal.run_id != "run-001" || journal.events.size() != 8 ||
        journal.events[0].kind !=
            ahfl::execution_journal::ExecutionJournalEventKind::SessionStarted ||
        journal.events[1].kind !=
            ahfl::execution_journal::ExecutionJournalEventKind::NodeBecameReady ||
        journal.events[2].kind !=
            ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted ||
        journal.events[3].kind !=
            ahfl::execution_journal::ExecutionJournalEventKind::NodeCompleted ||
        journal.events[3].used_mock_selectors.size() != 1 ||
        journal.events[3].used_mock_selectors.front() != "binding:runtime.echo" ||
        journal.events[4].node_name != "second" ||
        journal.events[4].satisfied_dependencies.size() != 1 ||
        journal.events[4].satisfied_dependencies.front() != "first" ||
        journal.events.back().kind !=
            ahfl::execution_journal::ExecutionJournalEventKind::WorkflowCompleted) {
        std::cerr << "unexpected execution journal bootstrap result\n";
        return 1;
    }

    return 0;
}

int run_build_execution_journal_rejects_invalid_runtime_session() {
    auto session = make_valid_runtime_session();
    session.nodes[1].status = ahfl::runtime_session::NodeSessionStatus::Blocked;

    const auto result = ahfl::execution_journal::build_execution_journal(session);
    if (!result.has_errors()) {
        std::cerr << "expected invalid runtime session bootstrap failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            result.diagnostics,
            "runtime session validation node 'second' is not completed while workflow status is Completed")) {
        result.diagnostics.render(std::cout);
        std::cerr << "missing invalid runtime session bootstrap diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: execution_journal_tests <case>\n";
        return 2;
    }

    const std::string test_case = argv[1];

    if (test_case == "validate-execution-journal-ok") {
        return run_validate_execution_journal_ok();
    }

    if (test_case == "validate-execution-journal-rejects-out-of-order-node-phase") {
        return run_validate_execution_journal_rejects_out_of_order_node_phase();
    }

    if (test_case == "validate-execution-journal-rejects-non-monotonic-execution-index") {
        return run_validate_execution_journal_rejects_non_monotonic_execution_index();
    }

    if (test_case == "validate-execution-journal-rejects-events-after-workflow-completed") {
        return run_validate_execution_journal_rejects_events_after_workflow_completed();
    }

    if (test_case == "build-execution-journal-from-runtime-session-ok") {
        return run_build_execution_journal_from_runtime_session_ok();
    }

    if (test_case == "build-execution-journal-rejects-invalid-runtime-session") {
        return run_build_execution_journal_rejects_invalid_runtime_session();
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
