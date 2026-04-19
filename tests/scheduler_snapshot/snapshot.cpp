#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::optional<ahfl::ir::Program>
load_project_ir(const std::filesystem::path &project_descriptor) {
    const ahfl::Frontend frontend;

    const auto descriptor_result = frontend.load_project_descriptor(project_descriptor);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        descriptor_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto project_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = descriptor_result.descriptor->entry_files,
        .search_roots = descriptor_result.descriptor->search_roots,
    });
    if (project_result.has_errors()) {
        project_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(project_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(project_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(project_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return ahfl::lower_program_ir(project_result.graph, resolve_result, type_check_result);
}

[[nodiscard]] ahfl::handoff::PackageMetadata
make_project_workflow_value_flow_metadata() {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string(ahfl::handoff::kFormatVersion),
        .name = "workflow-value-flow",
        .version = "0.2.0",
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Workflow,
        .canonical_name = "app::main::ValueFlowWorkflow",
    };
    metadata.export_targets = {
        *metadata.entry_target,
        ahfl::handoff::ExecutableRef{
            .kind = ahfl::handoff::ExecutableKind::Agent,
            .canonical_name = "lib::agents::AliasAgent",
        },
    };
    metadata.capability_binding_keys.emplace("lib::agents::Echo", "runtime.echo");
    return metadata;
}

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
load_project_plan(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return std::nullopt;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_runtime_session_with_mock(const ahfl::handoff::ExecutionPlan &plan,
                                std::string input_fixture,
                                std::string result_fixture,
                                std::string run_id) {
    const auto session = ahfl::runtime_session::build_runtime_session(
        plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = std::move(input_fixture),
            .run_id = std::move(run_id),
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = std::move(result_fixture),
                        .invocation_label = std::string("echo-1"),
                    },
                },
        });
    if (session.has_errors() || !session.session.has_value()) {
        session.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *session.session;
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_valid_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.basic", "fixture.echo.ok", "run-001");
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_failed_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.failed", "fixture.echo.fail", "run-failed-001");
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_partial_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.partial", "fixture.echo.pending", "run-partial-001");
}

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_valid_execution_journal(const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = ahfl::execution_journal::build_execution_journal(session);
    if (journal.has_errors() || !journal.journal.has_value()) {
        journal.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *journal.journal;
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView>
build_replay_view_from_session(const ahfl::handoff::ExecutionPlan &plan,
                               const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = build_valid_execution_journal(session);
    if (!journal.has_value()) {
        return std::nullopt;
    }

    const auto replay = ahfl::replay_view::build_replay_view(plan, session, *journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *replay.replay;
}

[[nodiscard]] std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot>
build_scheduler_snapshot_from_session(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = build_valid_execution_journal(session);
    if (!journal.has_value()) {
        return std::nullopt;
    }

    const auto replay =
        ahfl::replay_view::build_replay_view(plan, session, *journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(plan, session, *journal, *replay.replay);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        snapshot.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *snapshot.snapshot;
}

[[nodiscard]] ahfl::scheduler_snapshot::SchedulerSnapshot make_valid_scheduler_snapshot() {
    return ahfl::scheduler_snapshot::SchedulerSnapshot{
        .format_version = std::string(ahfl::scheduler_snapshot::kSchedulerSnapshotFormatVersion),
        .source_execution_plan_format_version =
            std::string(ahfl::handoff::kExecutionPlanFormatVersion),
        .source_runtime_session_format_version =
            std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
        .source_execution_journal_format_version =
            std::string(ahfl::execution_journal::kExecutionJournalFormatVersion),
        .source_replay_view_format_version =
            std::string(ahfl::replay_view::kReplayViewFormatVersion),
        .source_package_identity =
            ahfl::handoff::PackageIdentity{
                .format_version = std::string(ahfl::handoff::kFormatVersion),
                .name = "workflow-value-flow",
                .version = "0.2.0",
            },
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .session_id = "run-partial-001",
        .run_id = "run-partial-001",
        .input_fixture = "fixture.request.partial",
        .workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Partial,
        .snapshot_status = ahfl::scheduler_snapshot::SchedulerSnapshotStatus::Runnable,
        .workflow_failure_summary = std::nullopt,
        .execution_order = {"first", "second"},
        .ready_nodes =
            {
                ahfl::scheduler_snapshot::SchedulerReadyNode{
                    .node_name = "second",
                    .target = "lib::agents::AliasAgent",
                    .execution_index = 1,
                    .planned_dependencies = {"first"},
                    .satisfied_dependencies = {"first"},
                    .input_summary = {},
                    .capability_bindings =
                        {
                            ahfl::handoff::CapabilityBindingReference{
                                .capability_name = "lib::agents::Echo",
                                .binding_key = "runtime.echo",
                            },
                        },
                },
            },
        .blocked_nodes = {},
        .cursor =
            ahfl::scheduler_snapshot::SchedulerCursor{
                .completed_prefix_size = 1,
                .completed_prefix = {"first"},
                .next_candidate_node_name = "second",
                .checkpoint_friendly = true,
            },
    };
}

[[nodiscard]] ahfl::scheduler_snapshot::SchedulerSnapshot make_valid_failed_scheduler_snapshot() {
    return ahfl::scheduler_snapshot::SchedulerSnapshot{
        .format_version = std::string(ahfl::scheduler_snapshot::kSchedulerSnapshotFormatVersion),
        .source_execution_plan_format_version =
            std::string(ahfl::handoff::kExecutionPlanFormatVersion),
        .source_runtime_session_format_version =
            std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
        .source_execution_journal_format_version =
            std::string(ahfl::execution_journal::kExecutionJournalFormatVersion),
        .source_replay_view_format_version =
            std::string(ahfl::replay_view::kReplayViewFormatVersion),
        .source_package_identity =
            ahfl::handoff::PackageIdentity{
                .format_version = std::string(ahfl::handoff::kFormatVersion),
                .name = "workflow-value-flow",
                .version = "0.2.0",
            },
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .session_id = "run-failed-001",
        .run_id = "run-failed-001",
        .input_fixture = "fixture.request.failed",
        .workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed,
        .snapshot_status = ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed,
        .workflow_failure_summary =
            ahfl::runtime_session::RuntimeFailureSummary{
                .kind = ahfl::runtime_session::RuntimeFailureKind::WorkflowFailed,
                .node_name = std::string("first"),
                .message = "workflow failed after node execution",
            },
        .execution_order = {"first", "second"},
        .ready_nodes = {},
        .blocked_nodes =
            {
                ahfl::scheduler_snapshot::SchedulerBlockedNode{
                    .node_name = "second",
                    .target = "lib::agents::AliasAgent",
                    .execution_index = 1,
                    .planned_dependencies = {"first"},
                    .missing_dependencies = {},
                    .blocked_reason =
                        ahfl::scheduler_snapshot::SchedulerBlockedReasonKind::WorkflowTerminalFailure,
                    .may_become_ready = false,
                    .blocking_failure_summary =
                        ahfl::runtime_session::RuntimeFailureSummary{
                            .kind = ahfl::runtime_session::RuntimeFailureKind::WorkflowFailed,
                            .node_name = std::string("first"),
                            .message = "workflow failed after node execution",
                        },
                },
            },
        .cursor =
            ahfl::scheduler_snapshot::SchedulerCursor{
                .completed_prefix_size = 0,
                .completed_prefix = {},
                .next_candidate_node_name = std::nullopt,
                .checkpoint_friendly = true,
            },
    };
}

int run_validate_scheduler_snapshot_ok() {
    const auto snapshot = make_valid_scheduler_snapshot();
    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_failed_ok() {
    const auto snapshot = make_valid_failed_scheduler_snapshot();
    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_runnable_without_ready_nodes() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.ready_nodes.clear();

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected runnable scheduler snapshot failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot runnable status requires at least one ready node")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing runnable scheduler snapshot diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_next_candidate_not_ready() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.blocked_nodes = {
        ahfl::scheduler_snapshot::SchedulerBlockedNode{
            .node_name = "first",
            .target = "lib::agents::AliasAgent",
            .execution_index = 0,
            .planned_dependencies = {},
            .missing_dependencies = {},
            .blocked_reason =
                ahfl::scheduler_snapshot::SchedulerBlockedReasonKind::UpstreamPartial,
            .may_become_ready = false,
            .blocking_failure_summary = std::nullopt,
        },
    };
    snapshot.cursor.next_candidate_node_name = "first";

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected runnable next candidate failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot runnable status next candidate must reference a ready node")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing runnable next candidate diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_non_prefix_cursor() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.cursor.completed_prefix = {"second"};
    snapshot.cursor.completed_prefix_size = 1;

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected non-prefix cursor scheduler snapshot failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot cursor completed_prefix must be a prefix of execution_order")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing non-prefix cursor diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_ready_dependency_outside_planned_set() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.ready_nodes.front().satisfied_dependencies = {"missing-dependency"};

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected ready dependency subset failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot ready node 'second' contains satisfied dependency "
            "'missing-dependency' not declared in planned_dependencies")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing ready dependency subset diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_unknown_ready_node() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.ready_nodes.front().node_name = "missing";

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected unknown ready node scheduler snapshot failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot ready node 'missing' does not exist in execution_order")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing unknown ready node diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_blocked_terminal_failure_without_summary() {
    auto snapshot = make_valid_failed_scheduler_snapshot();
    snapshot.blocked_nodes.front().blocking_failure_summary = std::nullopt;

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked terminal failure summary failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot blocked node 'second' workflow terminal failure "
            "requires blocking_failure_summary")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing blocked terminal failure summary diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_terminal_completed_without_full_prefix() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed;
    snapshot.snapshot_status =
        ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted;
    snapshot.ready_nodes.clear();
    snapshot.blocked_nodes.clear();
    snapshot.cursor.next_candidate_node_name = std::nullopt;

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected terminal completed prefix failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot terminal completed status requires fully completed prefix")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing terminal completed prefix diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_unsupported_format_version() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.format_version = "ahfl.scheduler-snapshot.v999";

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler snapshot format_version failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot format_version must be 'ahfl.scheduler-snapshot.v1'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler snapshot format_version diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_snapshot_rejects_unsupported_source_replay_view_format_version() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.source_replay_view_format_version = "ahfl.replay-view.v999";

    const auto validation = ahfl::scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler snapshot source replay view format failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler snapshot source_replay_view_format_version must be 'ahfl.replay-view.v2'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler snapshot source replay view diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_snapshot_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(*plan, *session, *journal, *replay);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        snapshot.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *snapshot.snapshot;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || !value.ready_nodes.empty() ||
        !value.blocked_nodes.empty() || value.cursor.completed_prefix_size != 2 ||
        value.cursor.completed_prefix.size() != 2 ||
        value.cursor.completed_prefix[0] != "first" ||
        value.cursor.completed_prefix[1] != "second" ||
        value.cursor.next_candidate_node_name.has_value() || !value.cursor.checkpoint_friendly) {
        std::cerr << "unexpected completed scheduler snapshot bootstrap result\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_snapshot_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_failed_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(*plan, *session, *journal, *replay);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        snapshot.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *snapshot.snapshot;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed ||
        !value.workflow_failure_summary.has_value() || value.execution_order.size() != 2 ||
        value.execution_order[0] != "first" || value.execution_order[1] != "second" ||
        !value.ready_nodes.empty() || value.blocked_nodes.size() != 1 ||
        value.blocked_nodes.front().node_name != "second" ||
        value.blocked_nodes.front().blocked_reason !=
            ahfl::scheduler_snapshot::SchedulerBlockedReasonKind::WorkflowTerminalFailure ||
        value.blocked_nodes.front().may_become_ready ||
        !value.blocked_nodes.front().blocking_failure_summary.has_value() ||
        value.cursor.completed_prefix_size != 0 || !value.cursor.completed_prefix.empty() ||
        value.cursor.next_candidate_node_name.has_value()) {
        std::cerr << "unexpected failed scheduler snapshot bootstrap result\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_snapshot_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_partial_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(*plan, *session, *journal, *replay);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        snapshot.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *snapshot.snapshot;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::Runnable ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.ready_nodes.size() != 1 ||
        value.ready_nodes.front().node_name != "first" ||
        value.ready_nodes.front().execution_index != 0 ||
        !value.ready_nodes.front().planned_dependencies.empty() ||
        value.blocked_nodes.size() != 1 ||
        value.blocked_nodes.front().node_name != "second" ||
        value.blocked_nodes.front().blocked_reason !=
            ahfl::scheduler_snapshot::SchedulerBlockedReasonKind::WaitingOnDependencies ||
        !value.blocked_nodes.front().may_become_ready ||
        value.blocked_nodes.front().missing_dependencies.size() != 1 ||
        value.blocked_nodes.front().missing_dependencies.front() != "first" ||
        value.cursor.completed_prefix_size != 0 || !value.cursor.completed_prefix.empty() ||
        !value.cursor.next_candidate_node_name.has_value() ||
        *value.cursor.next_candidate_node_name != "first") {
        std::cerr << "unexpected partial scheduler snapshot bootstrap result\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_snapshot_rejects_replay_workflow_mismatch(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }
    replay->workflow_canonical_name = "app::main::WrongWorkflow";

    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(*plan, *session, *journal, *replay);
    if (!snapshot.has_errors()) {
        std::cerr << "expected replay workflow mismatch scheduler snapshot failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            snapshot.diagnostics,
            "scheduler snapshot bootstrap replay view workflow_canonical_name does not match runtime session")) {
        snapshot.diagnostics.render(std::cout);
        std::cerr << "missing replay workflow mismatch scheduler snapshot diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_decision_summary_completed(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.format_version != ahfl::scheduler_snapshot::kSchedulerDecisionSummaryFormatVersion ||
        value.source_scheduler_snapshot_format_version != snapshot->format_version ||
        value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted ||
        value.completed_prefix_size != 2 || value.completed_prefix.size() != 2 ||
        value.completed_prefix[0] != "first" || value.completed_prefix[1] != "second" ||
        value.next_candidate_node_name.has_value() || !value.ready_nodes.empty() ||
        !value.blocked_nodes.empty() || value.terminal_reason != "workflow_completed" ||
        value.next_action != ahfl::scheduler_snapshot::SchedulerNextActionKind::WorkflowCompleted ||
        value.next_step_recommendation !=
            "workflow is complete; no further scheduler action") {
        std::cerr << "unexpected completed scheduler decision summary\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_decision_summary_failed(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_failed_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed ||
        !value.workflow_failure_summary.has_value() || value.completed_prefix_size != 0 ||
        !value.completed_prefix.empty() || !value.ready_nodes.empty() ||
        value.blocked_nodes.size() != 1 ||
        value.blocked_nodes.front().node_name != "second" ||
        value.blocked_nodes.front().blocked_reason !=
            ahfl::scheduler_snapshot::SchedulerBlockedReasonKind::WorkflowTerminalFailure ||
        value.blocked_nodes.front().may_become_ready ||
        value.terminal_reason != "workflow_failed" ||
        value.next_action != ahfl::scheduler_snapshot::SchedulerNextActionKind::InvestigateFailure ||
        value.next_step_recommendation !=
            "inspect workflow failure at node 'first'; do not continue scheduling") {
        std::cerr << "unexpected failed scheduler decision summary\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_decision_summary_partial(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_partial_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.snapshot_status != ahfl::scheduler_snapshot::SchedulerSnapshotStatus::Runnable ||
        value.completed_prefix_size != 0 || !value.completed_prefix.empty() ||
        !value.next_candidate_node_name.has_value() ||
        *value.next_candidate_node_name != "first" || value.ready_nodes.size() != 1 ||
        value.ready_nodes.front().node_name != "first" ||
        value.blocked_nodes.size() != 1 ||
        value.blocked_nodes.front().node_name != "second" ||
        value.blocked_nodes.front().missing_dependencies.size() != 1 ||
        value.blocked_nodes.front().missing_dependencies.front() != "first" ||
        value.terminal_reason.has_value() ||
        value.next_action != ahfl::scheduler_snapshot::SchedulerNextActionKind::RunReadyNode ||
        value.next_step_recommendation != "run ready node 'first'") {
        std::cerr << "unexpected partial scheduler decision summary\n";
        return 1;
    }

    return 0;
}

int run_build_scheduler_decision_summary_rejects_invalid_snapshot() {
    auto snapshot = make_valid_scheduler_snapshot();
    snapshot.ready_nodes.clear();

    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(snapshot);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid scheduler decision summary input failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            summary.diagnostics,
            "scheduler snapshot runnable status requires at least one ready node")) {
        summary.diagnostics.render(std::cout);
        std::cerr << "missing invalid scheduler summary input diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_decision_summary_ok() {
    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(
        make_valid_scheduler_snapshot());
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::scheduler_snapshot::validate_scheduler_decision_summary(*summary.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_validate_scheduler_decision_summary_rejects_unsupported_format_version() {
    const auto built = ahfl::scheduler_snapshot::build_scheduler_decision_summary(
        make_valid_scheduler_snapshot());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.format_version = "ahfl.scheduler-review.v999";

    const auto validation =
        ahfl::scheduler_snapshot::validate_scheduler_decision_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler decision summary format_version failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler decision summary format_version must be 'ahfl.scheduler-review.v1'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler decision summary format_version diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_decision_summary_rejects_unsupported_source_snapshot_format_version() {
    const auto built = ahfl::scheduler_snapshot::build_scheduler_decision_summary(
        make_valid_scheduler_snapshot());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v999";

    const auto validation =
        ahfl::scheduler_snapshot::validate_scheduler_decision_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler decision summary source snapshot format failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler decision summary source_scheduler_snapshot_format_version must be 'ahfl.scheduler-snapshot.v1'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler decision summary source snapshot diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_decision_summary_rejects_prefix_size_mismatch() {
    const auto built = ahfl::scheduler_snapshot::build_scheduler_decision_summary(
        make_valid_scheduler_snapshot());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.completed_prefix_size = summary.completed_prefix.size() + 1;

    const auto validation =
        ahfl::scheduler_snapshot::validate_scheduler_decision_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler decision summary prefix size failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler decision summary completed_prefix_size must match completed_prefix length")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler decision summary prefix size diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_scheduler_decision_summary_rejects_runnable_terminal_reason() {
    const auto built = ahfl::scheduler_snapshot::build_scheduler_decision_summary(
        make_valid_scheduler_snapshot());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.terminal_reason = "workflow_completed";

    const auto validation =
        ahfl::scheduler_snapshot::validate_scheduler_decision_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected scheduler decision summary runnable terminal_reason failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "scheduler decision summary runnable snapshot_status must not declare terminal_reason")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing scheduler decision summary runnable terminal_reason diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: ahfl_scheduler_snapshot_tests <command> [project_descriptor]\n";
        return 1;
    }

    const std::string_view command{argv[1]};
    if (command == "validate-scheduler-snapshot-ok") {
        return run_validate_scheduler_snapshot_ok();
    }
    if (command == "validate-scheduler-snapshot-failed-ok") {
        return run_validate_scheduler_snapshot_failed_ok();
    }
    if (command == "validate-scheduler-snapshot-rejects-runnable-without-ready-nodes") {
        return run_validate_scheduler_snapshot_rejects_runnable_without_ready_nodes();
    }
    if (command == "validate-scheduler-snapshot-rejects-next-candidate-not-ready") {
        return run_validate_scheduler_snapshot_rejects_next_candidate_not_ready();
    }
    if (command == "validate-scheduler-snapshot-rejects-non-prefix-cursor") {
        return run_validate_scheduler_snapshot_rejects_non_prefix_cursor();
    }
    if (command == "validate-scheduler-snapshot-rejects-ready-dependency-outside-planned-set") {
        return run_validate_scheduler_snapshot_rejects_ready_dependency_outside_planned_set();
    }
    if (command == "validate-scheduler-snapshot-rejects-unknown-ready-node") {
        return run_validate_scheduler_snapshot_rejects_unknown_ready_node();
    }
    if (command == "validate-scheduler-snapshot-rejects-blocked-terminal-failure-without-summary") {
        return run_validate_scheduler_snapshot_rejects_blocked_terminal_failure_without_summary();
    }
    if (command == "validate-scheduler-snapshot-rejects-terminal-completed-without-full-prefix") {
        return run_validate_scheduler_snapshot_rejects_terminal_completed_without_full_prefix();
    }
    if (command == "validate-scheduler-snapshot-rejects-unsupported-format-version") {
        return run_validate_scheduler_snapshot_rejects_unsupported_format_version();
    }
    if (command ==
        "validate-scheduler-snapshot-rejects-unsupported-source-replay-view-format-version") {
        return run_validate_scheduler_snapshot_rejects_unsupported_source_replay_view_format_version();
    }
    if (command == "build-scheduler-decision-summary-rejects-invalid-snapshot") {
        return run_build_scheduler_decision_summary_rejects_invalid_snapshot();
    }
    if (command == "validate-scheduler-decision-summary-ok") {
        return run_validate_scheduler_decision_summary_ok();
    }
    if (command == "validate-scheduler-decision-summary-rejects-unsupported-format-version") {
        return run_validate_scheduler_decision_summary_rejects_unsupported_format_version();
    }
    if (command ==
        "validate-scheduler-decision-summary-rejects-unsupported-source-snapshot-format-version") {
        return run_validate_scheduler_decision_summary_rejects_unsupported_source_snapshot_format_version();
    }
    if (command == "validate-scheduler-decision-summary-rejects-prefix-size-mismatch") {
        return run_validate_scheduler_decision_summary_rejects_prefix_size_mismatch();
    }
    if (command == "validate-scheduler-decision-summary-rejects-runnable-terminal-reason") {
        return run_validate_scheduler_decision_summary_rejects_runnable_terminal_reason();
    }

    if (argc < 3) {
        std::cerr << "missing project descriptor for scheduler snapshot bootstrap command\n";
        return 1;
    }

    const std::filesystem::path project_descriptor{argv[2]};
    if (command == "build-scheduler-snapshot-project-workflow-value-flow") {
        return run_build_scheduler_snapshot_project_workflow_value_flow(project_descriptor);
    }
    if (command == "build-scheduler-snapshot-failed-workflow") {
        return run_build_scheduler_snapshot_failed_workflow(project_descriptor);
    }
    if (command == "build-scheduler-snapshot-partial-workflow") {
        return run_build_scheduler_snapshot_partial_workflow(project_descriptor);
    }
    if (command == "build-scheduler-snapshot-rejects-replay-workflow-mismatch") {
        return run_build_scheduler_snapshot_rejects_replay_workflow_mismatch(
            project_descriptor);
    }
    if (command == "build-scheduler-decision-summary-completed") {
        return run_build_scheduler_decision_summary_completed(project_descriptor);
    }
    if (command == "build-scheduler-decision-summary-failed") {
        return run_build_scheduler_decision_summary_failed(project_descriptor);
    }
    if (command == "build-scheduler-decision-summary-partial") {
        return run_build_scheduler_decision_summary_partial(project_descriptor);
    }

    std::cerr << "unknown ahfl_scheduler_snapshot_tests command: " << command << '\n';
    return 1;
}
