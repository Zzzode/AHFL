#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/checkpoint_record/review.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include "../common/test_support.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

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

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
load_project_plan(const std::filesystem::path &project_descriptor) {
    const auto ir_program = ahfl::test_support::load_project_ir(project_descriptor);
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

    const auto replay = ahfl::replay_view::build_replay_view(plan, session, *journal);
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

[[nodiscard]] ahfl::checkpoint_record::CheckpointRecord make_valid_checkpoint_record() {
    return ahfl::checkpoint_record::CheckpointRecord{
        .format_version = std::string(ahfl::checkpoint_record::kCheckpointRecordFormatVersion),
        .source_execution_plan_format_version =
            std::string(ahfl::handoff::kExecutionPlanFormatVersion),
        .source_runtime_session_format_version =
            std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
        .source_execution_journal_format_version =
            std::string(ahfl::execution_journal::kExecutionJournalFormatVersion),
        .source_replay_view_format_version =
            std::string(ahfl::replay_view::kReplayViewFormatVersion),
        .source_scheduler_snapshot_format_version =
            std::string(ahfl::scheduler_snapshot::kSchedulerSnapshotFormatVersion),
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
        .basis_kind = ahfl::checkpoint_record::CheckpointBasisKind::LocalOnly,
        .checkpoint_friendly_source = true,
        .cursor =
            ahfl::checkpoint_record::CheckpointCursor{
                .persistable_prefix_size = 1,
                .persistable_prefix = {"first"},
                .resume_candidate_node_name = "second",
                .resume_ready = true,
            },
        .resume_blocker = std::nullopt,
    };
}

int validate_checkpoint_record_ok() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_blocked_ok() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::Blocked;
    record.cursor.resume_ready = false;
    record.resume_blocker = ahfl::checkpoint_record::CheckpointResumeBlocker{
        .kind = ahfl::checkpoint_record::CheckpointResumeBlockerKind::WaitingOnSchedulerState,
        .message = "waiting for scheduler frontier to stabilize",
        .node_name = "second",
    };

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_terminal_completed_ok() {
    auto record = make_valid_checkpoint_record();
    record.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed;
    record.snapshot_status =
        ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted;
    record.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted;
    record.cursor.persistable_prefix_size = 2;
    record.cursor.persistable_prefix = {"first", "second"};
    record.cursor.resume_candidate_node_name = std::nullopt;
    record.cursor.resume_ready = false;
    record.resume_blocker = std::nullopt;

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_terminal_failed_ok() {
    auto record = make_valid_checkpoint_record();
    record.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    record.snapshot_status = ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed;
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    record.cursor.resume_candidate_node_name = std::nullopt;
    record.cursor.resume_ready = false;
    record.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "second",
        .message = "second failed",
    };
    record.resume_blocker = ahfl::checkpoint_record::CheckpointResumeBlocker{
        .kind = ahfl::checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure,
        .message = "workflow failed",
        .node_name = "second",
    };

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_rejects_non_prefix_cursor() {
    auto record = make_valid_checkpoint_record();
    record.cursor.persistable_prefix = {"second"};

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (!validation.has_errors()) {
        std::cout << "expected validation failure for non-prefix persistable_prefix\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(validation.diagnostics,
                             "persistable_prefix must be a prefix of execution_order")) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_rejects_resume_ready_with_blocker() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    record.resume_blocker = ahfl::checkpoint_record::CheckpointResumeBlocker{
        .kind = ahfl::checkpoint_record::CheckpointResumeBlockerKind::WaitingOnSchedulerState,
        .message = "unexpected blocker",
        .node_name = "second",
    };

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (!validation.has_errors()) {
        std::cout << "expected validation failure for resume_ready with blocker\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(validation.diagnostics,
                             "cannot contain resume_blocker when cursor resume_ready is true")) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_rejects_terminal_completed_without_full_prefix() {
    auto record = make_valid_checkpoint_record();
    record.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed;
    record.snapshot_status =
        ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted;
    record.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted;
    record.cursor.resume_candidate_node_name = std::nullopt;
    record.cursor.resume_ready = false;

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (!validation.has_errors()) {
        std::cout << "expected validation failure for incomplete terminal completed prefix\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(validation.diagnostics,
                             "TerminalCompleted status requires full persistable_prefix")) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_rejects_terminal_failed_without_failure_summary() {
    auto record = make_valid_checkpoint_record();
    record.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    record.snapshot_status = ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed;
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    record.cursor.resume_candidate_node_name = std::nullopt;
    record.cursor.resume_ready = false;
    record.resume_blocker = ahfl::checkpoint_record::CheckpointResumeBlocker{
        .kind = ahfl::checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure,
        .message = "workflow failed",
        .node_name = "second",
    };

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (!validation.has_errors()) {
        std::cout << "expected validation failure for missing workflow_failure_summary\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(validation.diagnostics,
                             "TerminalFailed status requires workflow_failure_summary")) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_record_rejects_durable_adjacent_without_checkpoint_friendly_source() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::Blocked;
    record.cursor.resume_ready = false;
    record.resume_blocker = ahfl::checkpoint_record::CheckpointResumeBlocker{
        .kind = ahfl::checkpoint_record::CheckpointResumeBlockerKind::NotCheckpointFriendly,
        .message = "snapshot not checkpoint friendly",
        .node_name = std::nullopt,
    };
    record.basis_kind = ahfl::checkpoint_record::CheckpointBasisKind::DurableAdjacent;
    record.checkpoint_friendly_source = false;

    const auto validation = ahfl::checkpoint_record::validate_checkpoint_record(record);
    if (!validation.has_errors()) {
        std::cout << "expected validation failure for durable-adjacent without checkpoint-friendly source\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(validation.diagnostics,
                             "durable-adjacent basis requires checkpoint_friendly_source")) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int build_checkpoint_record_project_workflow_value_flow(
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

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *record.record;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.cursor.persistable_prefix_size != 2 ||
        value.cursor.persistable_prefix.size() != 2 ||
        value.cursor.persistable_prefix[0] != "first" ||
        value.cursor.persistable_prefix[1] != "second" ||
        value.cursor.resume_candidate_node_name.has_value() || value.cursor.resume_ready ||
        value.resume_blocker.has_value() || !value.checkpoint_friendly_source) {
        std::cerr << "unexpected completed checkpoint record bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_record_failed_workflow(const std::filesystem::path &project_descriptor) {
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

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *record.record;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.snapshot_status !=
            ahfl::scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed ||
        !value.workflow_failure_summary.has_value() || value.execution_order.size() != 2 ||
        value.execution_order[0] != "first" || value.execution_order[1] != "second" ||
        value.cursor.persistable_prefix_size != 0 || !value.cursor.persistable_prefix.empty() ||
        value.cursor.resume_candidate_node_name.has_value() || value.cursor.resume_ready ||
        !value.resume_blocker.has_value() ||
        value.resume_blocker->kind !=
            ahfl::checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure ||
        !value.resume_blocker->node_name.has_value() ||
        *value.resume_blocker->node_name != "first") {
        std::cerr << "unexpected failed checkpoint record bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_record_partial_workflow(const std::filesystem::path &project_descriptor) {
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

    const auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *record.record;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.snapshot_status != ahfl::scheduler_snapshot::SchedulerSnapshotStatus::Runnable ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.cursor.persistable_prefix_size != 0 ||
        !value.cursor.persistable_prefix.empty() ||
        !value.cursor.resume_candidate_node_name.has_value() ||
        *value.cursor.resume_candidate_node_name != "first" || !value.cursor.resume_ready ||
        value.resume_blocker.has_value() || !value.checkpoint_friendly_source) {
        std::cerr << "unexpected partial checkpoint record bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_record_rejects_snapshot_workflow_mismatch(
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

    auto snapshot = build_scheduler_snapshot_from_session(*plan, *session);
    if (!snapshot.has_value()) {
        return 1;
    }
    snapshot->workflow_canonical_name = "app::main::WrongWorkflow";

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_errors()) {
        std::cerr << "expected scheduler snapshot workflow mismatch checkpoint record failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            record.diagnostics,
            "checkpoint record bootstrap scheduler snapshot workflow_canonical_name does not match runtime session")) {
        record.diagnostics.render(std::cout);
        std::cerr << "missing scheduler snapshot workflow mismatch checkpoint record diagnostic\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_review_completed(const std::filesystem::path &project_descriptor) {
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

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::checkpoint_record::build_checkpoint_review_summary(*record.record);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.format_version != ahfl::checkpoint_record::kCheckpointReviewSummaryFormatVersion ||
        value.source_checkpoint_record_format_version != record.record->format_version ||
        value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted ||
        value.persistable_prefix_size != 2 || value.persistable_prefix.size() != 2 ||
        value.persistable_prefix[0] != "first" || value.persistable_prefix[1] != "second" ||
        value.resume_candidate_node_name.has_value() || value.resume_ready ||
        value.resume_blocker.has_value() || value.terminal_reason != "workflow_completed" ||
        value.next_action !=
            ahfl::checkpoint_record::CheckpointReviewNextActionKind::WorkflowCompleted ||
        value.resume_preview != "workflow already completed; no resume is required" ||
        value.next_step_recommendation !=
            "archive completed checkpoint basis; no further resume action") {
        std::cerr << "unexpected completed checkpoint review summary\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_review_failed(const std::filesystem::path &project_descriptor) {
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

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::checkpoint_record::build_checkpoint_review_summary(*record.record);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed ||
        !value.workflow_failure_summary.has_value() || value.persistable_prefix_size != 0 ||
        !value.persistable_prefix.empty() || !value.resume_blocker.has_value() ||
        value.resume_blocker->kind !=
            ahfl::checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure ||
        value.terminal_reason != "workflow_failed" ||
        value.next_action !=
            ahfl::checkpoint_record::CheckpointReviewNextActionKind::InvestigateFailure ||
        value.resume_preview !=
            "workflow failed at node 'first'; resume is not available from current checkpoint" ||
        value.next_step_recommendation !=
            "inspect workflow failure before attempting a new run") {
        std::cerr << "unexpected failed checkpoint review summary\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_review_partial(const std::filesystem::path &project_descriptor) {
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

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    const auto record =
        ahfl::checkpoint_record::build_checkpoint_record(*plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::checkpoint_record::build_checkpoint_review_summary(*record.record);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist ||
        value.persistable_prefix_size != 0 || !value.persistable_prefix.empty() ||
        !value.resume_candidate_node_name.has_value() ||
        *value.resume_candidate_node_name != "first" || !value.resume_ready ||
        value.resume_blocker.has_value() ||
        value.terminal_reason.has_value() ||
        value.next_action !=
            ahfl::checkpoint_record::CheckpointReviewNextActionKind::PersistCheckpoint ||
        value.resume_preview !=
            "checkpoint can resume from node 'first' after persisting current prefix" ||
        value.next_step_recommendation !=
            "persist current checkpoint basis before future resume") {
        std::cerr << "unexpected partial checkpoint review summary\n";
        return 1;
    }

    return 0;
}

int build_checkpoint_review_rejects_invalid_record() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    record.cursor.resume_candidate_node_name = std::nullopt;

    const auto summary =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid checkpoint review record input failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            summary.diagnostics,
            "checkpoint record ReadyToPersist status requires resume_candidate_node_name")) {
        summary.diagnostics.render(std::cout);
        std::cerr << "missing invalid checkpoint review input diagnostic\n";
        return 1;
    }

    return 0;
}

int validate_checkpoint_review_ok() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto built =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::checkpoint_record::validate_checkpoint_review_summary(*built.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_checkpoint_review_rejects_unsupported_format_version() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto built =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.format_version = "ahfl.checkpoint-review.v999";

    const auto validation =
        ahfl::checkpoint_record::validate_checkpoint_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected checkpoint review format_version failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            validation.diagnostics,
            "checkpoint review summary format_version must be 'ahfl.checkpoint-review.v1'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing checkpoint review format_version diagnostic\n";
        return 1;
    }

    return 0;
}

int validate_checkpoint_review_rejects_unsupported_source_record_format_version() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto built =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_checkpoint_record_format_version = "ahfl.checkpoint-record.v999";

    const auto validation =
        ahfl::checkpoint_record::validate_checkpoint_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected checkpoint review source record format failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            validation.diagnostics,
            "checkpoint review summary source_checkpoint_record_format_version must be 'ahfl.checkpoint-record.v1'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing checkpoint review source record diagnostic\n";
        return 1;
    }

    return 0;
}

int validate_checkpoint_review_rejects_prefix_size_mismatch() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto built =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.persistable_prefix_size = summary.persistable_prefix.size() + 1;

    const auto validation =
        ahfl::checkpoint_record::validate_checkpoint_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected checkpoint review prefix size failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            validation.diagnostics,
            "checkpoint review summary persistable_prefix_size must match persistable_prefix length")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing checkpoint review prefix size diagnostic\n";
        return 1;
    }

    return 0;
}

int validate_checkpoint_review_rejects_ready_terminal_reason() {
    auto record = make_valid_checkpoint_record();
    record.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    const auto built =
        ahfl::checkpoint_record::build_checkpoint_review_summary(record);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.terminal_reason = "workflow_completed";

    const auto validation =
        ahfl::checkpoint_record::validate_checkpoint_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected checkpoint review ready terminal_reason failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            validation.diagnostics,
            "checkpoint review summary ReadyToPersist checkpoint_status must not declare terminal_reason")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing checkpoint review ready terminal_reason diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "expected command\n";
        return 1;
    }

    const std::string_view command = argv[1];
    if (command == "validate-checkpoint-record-ok") {
        return validate_checkpoint_record_ok();
    }
    if (command == "validate-checkpoint-record-blocked-ok") {
        return validate_checkpoint_record_blocked_ok();
    }
    if (command == "validate-checkpoint-record-terminal-completed-ok") {
        return validate_checkpoint_record_terminal_completed_ok();
    }
    if (command == "validate-checkpoint-record-terminal-failed-ok") {
        return validate_checkpoint_record_terminal_failed_ok();
    }
    if (command == "validate-checkpoint-record-rejects-non-prefix-cursor") {
        return validate_checkpoint_record_rejects_non_prefix_cursor();
    }
    if (command == "validate-checkpoint-record-rejects-resume-ready-with-blocker") {
        return validate_checkpoint_record_rejects_resume_ready_with_blocker();
    }
    if (command == "validate-checkpoint-record-rejects-terminal-completed-without-full-prefix") {
        return validate_checkpoint_record_rejects_terminal_completed_without_full_prefix();
    }
    if (command ==
        "validate-checkpoint-record-rejects-terminal-failed-without-failure-summary") {
        return validate_checkpoint_record_rejects_terminal_failed_without_failure_summary();
    }
    if (command ==
        "validate-checkpoint-record-rejects-durable-adjacent-without-checkpoint-friendly-source") {
        return validate_checkpoint_record_rejects_durable_adjacent_without_checkpoint_friendly_source();
    }
    if (command == "build-checkpoint-review-rejects-invalid-record") {
        return build_checkpoint_review_rejects_invalid_record();
    }
    if (command == "validate-checkpoint-review-ok") {
        return validate_checkpoint_review_ok();
    }
    if (command == "validate-checkpoint-review-rejects-unsupported-format-version") {
        return validate_checkpoint_review_rejects_unsupported_format_version();
    }
    if (command ==
        "validate-checkpoint-review-rejects-unsupported-source-record-format-version") {
        return validate_checkpoint_review_rejects_unsupported_source_record_format_version();
    }
    if (command == "validate-checkpoint-review-rejects-prefix-size-mismatch") {
        return validate_checkpoint_review_rejects_prefix_size_mismatch();
    }
    if (command == "validate-checkpoint-review-rejects-ready-terminal-reason") {
        return validate_checkpoint_review_rejects_ready_terminal_reason();
    }
    if (command == "build-checkpoint-record-project-workflow-value-flow") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_record_project_workflow_value_flow(argv[2]);
    }
    if (command == "build-checkpoint-record-failed-workflow") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_record_failed_workflow(argv[2]);
    }
    if (command == "build-checkpoint-record-partial-workflow") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_record_partial_workflow(argv[2]);
    }
    if (command == "build-checkpoint-record-rejects-snapshot-workflow-mismatch") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_record_rejects_snapshot_workflow_mismatch(argv[2]);
    }
    if (command == "build-checkpoint-review-completed") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_review_completed(argv[2]);
    }
    if (command == "build-checkpoint-review-failed") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_review_failed(argv[2]);
    }
    if (command == "build-checkpoint-review-partial") {
        if (argc < 3) {
            std::cout << "expected project descriptor path\n";
            return 1;
        }
        return build_checkpoint_review_partial(argv[2]);
    }

    std::cout << "unknown command: " << command << '\n';
    return 1;
}
