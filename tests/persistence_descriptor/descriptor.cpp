#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/persistence_descriptor/review.hpp"
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

namespace {

struct PersistenceBootstrapFixture {
    ahfl::handoff::ExecutionPlan plan;
    ahfl::runtime_session::RuntimeSession session;
    ahfl::execution_journal::ExecutionJournal journal;
    ahfl::replay_view::ReplayView replay;
    ahfl::scheduler_snapshot::SchedulerSnapshot snapshot;
    ahfl::checkpoint_record::CheckpointRecord record;
};

enum class SessionScenario {
    Completed,
    Failed,
    Partial,
};

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
build_runtime_session_for_scenario(const ahfl::handoff::ExecutionPlan &plan,
                                   SessionScenario scenario) {
    switch (scenario) {
    case SessionScenario::Completed:
        return build_runtime_session_with_mock(
            plan, "fixture.request.basic", "fixture.echo.ok", "run-001");
    case SessionScenario::Failed:
        return build_runtime_session_with_mock(
            plan, "fixture.request.failed", "fixture.echo.fail", "run-failed-001");
    case SessionScenario::Partial:
        return build_runtime_session_with_mock(
            plan, "fixture.request.partial", "fixture.echo.pending", "run-partial-001");
    }

    return std::nullopt;
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
build_replay_view(const ahfl::handoff::ExecutionPlan &plan,
                  const ahfl::runtime_session::RuntimeSession &session,
                  const ahfl::execution_journal::ExecutionJournal &journal) {
    const auto replay = ahfl::replay_view::build_replay_view(plan, session, journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *replay.replay;
}

[[nodiscard]] std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot>
build_scheduler_snapshot(const ahfl::handoff::ExecutionPlan &plan,
                         const ahfl::runtime_session::RuntimeSession &session,
                         const ahfl::execution_journal::ExecutionJournal &journal,
                         const ahfl::replay_view::ReplayView &replay) {
    const auto snapshot =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(plan, session, journal, replay);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        snapshot.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *snapshot.snapshot;
}

[[nodiscard]] std::optional<PersistenceBootstrapFixture>
build_bootstrap_fixture(const std::filesystem::path &project_descriptor,
                        SessionScenario scenario) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto session = build_runtime_session_for_scenario(*plan, scenario);
    if (!session.has_value()) {
        return std::nullopt;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return std::nullopt;
    }

    const auto replay = build_replay_view(*plan, *session, *journal);
    if (!replay.has_value()) {
        return std::nullopt;
    }

    const auto snapshot = build_scheduler_snapshot(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const auto record = ahfl::checkpoint_record::build_checkpoint_record(
        *plan, *session, *journal, *replay, *snapshot);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return PersistenceBootstrapFixture{
        .plan = *plan,
        .session = *session,
        .journal = *journal,
        .replay = *replay,
        .snapshot = *snapshot,
        .record = *record.record,
    };
}

[[nodiscard]] ahfl::persistence_descriptor::CheckpointPersistenceDescriptor
make_valid_persistence_descriptor() {
    ahfl::persistence_descriptor::CheckpointPersistenceDescriptor descriptor;
    descriptor.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    descriptor.session_id = "run-partial-001";
    descriptor.run_id = "run-partial-001";
    descriptor.input_fixture = "fixture.request.partial";
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Partial;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport;
    descriptor.execution_order = {"first", "second"};
    descriptor.planned_durable_identity =
        "plan::workflow-value-flow::run-partial-001::prefix-1";
    descriptor.export_basis_kind =
        ahfl::persistence_descriptor::PersistenceBasisKind::LocalPlanningOnly;
    descriptor.cursor.exportable_prefix_size = 1;
    descriptor.cursor.exportable_prefix = {"first"};
    descriptor.cursor.next_export_candidate_node_name = "second";
    descriptor.cursor.export_ready = true;
    return descriptor;
}

int validate_persistence_descriptor_ok() {
    const auto descriptor = make_valid_persistence_descriptor();
    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_descriptor_blocked_ok() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::Blocked;
    descriptor.cursor.export_ready = false;
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind = ahfl::persistence_descriptor::PersistenceBlockerKind::WaitingOnCheckpointState,
        .message = "waiting for stable checkpoint export basis",
        .node_name = "second",
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_descriptor_terminal_failed_ok() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    descriptor.cursor.export_ready = false;
    descriptor.cursor.next_export_candidate_node_name.reset();
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind = ahfl::persistence_descriptor::PersistenceBlockerKind::WorkflowFailure,
        .message = "workflow failed before durable export handoff",
        .node_name = "second",
    };
    descriptor.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "second",
        .message = "echo capability failed",
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_descriptor_terminal_partial_ok() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalPartial;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial;
    descriptor.cursor.export_ready = false;
    descriptor.cursor.next_export_candidate_node_name.reset();
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind = ahfl::persistence_descriptor::PersistenceBlockerKind::WorkflowPartial,
        .message = "partial workflow retained as local persistence handoff",
        .node_name = "second",
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_descriptor_rejects_missing_planned_identity() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.planned_durable_identity.clear();

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected missing planned durable identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "planned_durable_identity must not be empty")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_non_prefix_cursor() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.cursor.exportable_prefix = {"second"};
    descriptor.cursor.exportable_prefix_size = 1;

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected non-prefix exportable prefix to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "exportable_prefix must be a prefix of execution_order")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_export_ready_with_blocker() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind =
            ahfl::persistence_descriptor::PersistenceBlockerKind::MissingPlannedDurableIdentity,
        .message = "unexpected blocker",
        .node_name = std::nullopt,
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected export_ready with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "cannot contain persistence_blocker when cursor export_ready is true")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_unsupported_checkpoint_record_format() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.source_checkpoint_record_format_version = "ahfl.checkpoint-record.v999";

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported checkpoint record format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_checkpoint_record_format_version must be")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_ready_from_blocked_checkpoint() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::Blocked;

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected ready export from blocked checkpoint to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "ReadyToExport status requires ReadyToPersist checkpoint_status")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_terminal_failed_export_ready() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind = ahfl::persistence_descriptor::PersistenceBlockerKind::WorkflowFailure,
        .message = "workflow failed",
        .node_name = "second",
    };
    descriptor.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "second",
        .message = "echo capability failed",
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected terminal failed export_ready to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "terminal blocked status cannot be export_ready")
               ? 0
               : 1;
}

int validate_persistence_descriptor_rejects_store_adjacent_blocked() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::Blocked;
    descriptor.cursor.export_ready = false;
    descriptor.export_basis_kind =
        ahfl::persistence_descriptor::PersistenceBasisKind::StoreAdjacent;
    descriptor.persistence_blocker = ahfl::persistence_descriptor::PersistenceBlocker{
        .kind = ahfl::persistence_descriptor::PersistenceBlockerKind::WaitingOnCheckpointState,
        .message = "waiting for checkpoint state",
        .node_name = "second",
    };

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked store-adjacent descriptor to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "store-adjacent basis requires ready or completed persistence_status")
               ? 0
               : 1;
}

int validate_persistence_review_ok() {
    const auto built =
        ahfl::persistence_descriptor::build_persistence_review_summary(
            make_valid_persistence_descriptor());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_review_summary(*built.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_review_rejects_unsupported_source_descriptor_format() {
    const auto built =
        ahfl::persistence_descriptor::build_persistence_review_summary(
            make_valid_persistence_descriptor());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v999";

    const auto validation =
        ahfl::persistence_descriptor::validate_persistence_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected persistence review source descriptor format failure\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "source_persistence_descriptor_format_version must be")
               ? 0
               : 1;
}

int build_persistence_review_rejects_invalid_descriptor() {
    auto descriptor = make_valid_persistence_descriptor();
    descriptor.planned_durable_identity.clear();

    const auto summary =
        ahfl::persistence_descriptor::build_persistence_review_summary(descriptor);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid persistence review descriptor input failure\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(summary.diagnostics,
                               "persistence descriptor planned_durable_identity must not be empty")
               ? 0
               : 1;
}

int build_persistence_descriptor_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *descriptor.descriptor;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.cursor.exportable_prefix_size != 2 ||
        value.cursor.exportable_prefix.size() != 2 ||
        value.cursor.exportable_prefix[0] != "first" ||
        value.cursor.exportable_prefix[1] != "second" ||
        value.cursor.next_export_candidate_node_name.has_value() || value.cursor.export_ready ||
        value.persistence_blocker.has_value() ||
        value.export_basis_kind !=
            ahfl::persistence_descriptor::PersistenceBasisKind::LocalPlanningOnly ||
        value.planned_durable_identity != "plan::workflow-value-flow::run-001::prefix-2") {
        std::cerr << "unexpected completed persistence descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_descriptor_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *descriptor.descriptor;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed ||
        !value.workflow_failure_summary.has_value() || value.cursor.exportable_prefix_size != 0 ||
        !value.cursor.exportable_prefix.empty() ||
        value.cursor.next_export_candidate_node_name.has_value() || value.cursor.export_ready ||
        !value.persistence_blocker.has_value() ||
        value.persistence_blocker->kind !=
            ahfl::persistence_descriptor::PersistenceBlockerKind::WorkflowFailure ||
        !value.persistence_blocker->node_name.has_value() ||
        *value.persistence_blocker->node_name != "first" ||
        value.planned_durable_identity != "plan::workflow-value-flow::run-failed-001::prefix-0") {
        std::cerr << "unexpected failed persistence descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_descriptor_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *descriptor.descriptor;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport ||
        value.cursor.exportable_prefix_size != 0 || !value.cursor.exportable_prefix.empty() ||
        !value.cursor.next_export_candidate_node_name.has_value() ||
        *value.cursor.next_export_candidate_node_name != "first" || !value.cursor.export_ready ||
        value.persistence_blocker.has_value() ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-partial-001::prefix-0") {
        std::cerr << "unexpected partial persistence descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_descriptor_rejects_invalid_checkpoint_record(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }
    fixture->record.source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v999";

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (!descriptor.has_errors()) {
        std::cerr << "expected invalid checkpoint record persistence descriptor failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            descriptor.diagnostics,
            "checkpoint record source_scheduler_snapshot_format_version must be")) {
        descriptor.diagnostics.render(std::cout);
        std::cerr << "missing invalid checkpoint record persistence descriptor diagnostic\n";
        return 1;
    }

    return 0;
}

int build_persistence_descriptor_rejects_checkpoint_workflow_mismatch(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }
    fixture->record.workflow_canonical_name = "app::main::WrongWorkflow";

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (!descriptor.has_errors()) {
        std::cerr << "expected checkpoint workflow mismatch persistence descriptor failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            descriptor.diagnostics,
            "persistence descriptor bootstrap checkpoint record workflow_canonical_name does not match runtime session")) {
        descriptor.diagnostics.render(std::cout);
        std::cerr << "missing checkpoint workflow mismatch persistence descriptor diagnostic\n";
        return 1;
    }

    return 0;
}

int build_persistence_review_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary = ahfl::persistence_descriptor::build_persistence_review_summary(
        *descriptor.descriptor);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted ||
        value.next_action !=
            ahfl::persistence_descriptor::PersistenceReviewNextActionKind::WorkflowCompleted ||
        value.exportable_prefix_size != 2 || value.exportable_prefix.size() != 2 ||
        value.exportable_prefix[0] != "first" || value.exportable_prefix[1] != "second" ||
        value.next_export_candidate_node_name.has_value() || value.export_ready ||
        value.persistence_blocker.has_value() ||
        value.store_boundary_summary !=
            "local persistence planning only; durable store mutation ABI not yet promised" ||
        value.export_preview !=
            "workflow already completed; completed exportable prefix is retained for archival review" ||
        value.next_step_recommendation !=
            "archive completed persistence handoff; no further export action") {
        std::cerr << "unexpected completed persistence review summary\n";
        return 1;
    }

    return 0;
}

int build_persistence_review_failed_workflow(const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary = ahfl::persistence_descriptor::build_persistence_review_summary(
        *descriptor.descriptor);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed ||
        value.next_action !=
            ahfl::persistence_descriptor::PersistenceReviewNextActionKind::InvestigateFailure ||
        !value.workflow_failure_summary.has_value() || value.exportable_prefix_size != 0 ||
        !value.exportable_prefix.empty() || !value.persistence_blocker.has_value() ||
        value.persistence_blocker->kind !=
            ahfl::persistence_descriptor::PersistenceBlockerKind::WorkflowFailure ||
        value.export_preview !=
            "workflow failed at node 'first'; export is closed for current descriptor" ||
        value.next_step_recommendation !=
            "inspect workflow failure before planning durable export") {
        std::cerr << "unexpected failed persistence review summary\n";
        return 1;
    }

    return 0;
}

int build_persistence_review_partial_workflow(const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary = ahfl::persistence_descriptor::build_persistence_review_summary(
        *descriptor.descriptor);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport ||
        value.next_action !=
            ahfl::persistence_descriptor::PersistenceReviewNextActionKind::ExportPersistenceHandoff ||
        value.exportable_prefix_size != 0 || !value.exportable_prefix.empty() ||
        !value.next_export_candidate_node_name.has_value() ||
        *value.next_export_candidate_node_name != "first" || !value.export_ready ||
        value.persistence_blocker.has_value() ||
        value.export_preview !=
            "descriptor can export current prefix before node 'first' with planned durable identity 'plan::workflow-value-flow::run-partial-001::prefix-0'" ||
        value.next_step_recommendation !=
            "export current persistence handoff before future durable-store work") {
        std::cerr << "unexpected partial persistence review summary\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "expected test command\n";
        return 1;
    }

    const std::string_view command = argv[1];

    if (command == "validate-persistence-descriptor-ok") {
        return validate_persistence_descriptor_ok();
    }

    if (command == "validate-persistence-descriptor-blocked-ok") {
        return validate_persistence_descriptor_blocked_ok();
    }

    if (command == "validate-persistence-descriptor-terminal-failed-ok") {
        return validate_persistence_descriptor_terminal_failed_ok();
    }

    if (command == "validate-persistence-descriptor-terminal-partial-ok") {
        return validate_persistence_descriptor_terminal_partial_ok();
    }

    if (command == "validate-persistence-descriptor-rejects-missing-planned-identity") {
        return validate_persistence_descriptor_rejects_missing_planned_identity();
    }

    if (command == "validate-persistence-descriptor-rejects-non-prefix-cursor") {
        return validate_persistence_descriptor_rejects_non_prefix_cursor();
    }

    if (command == "validate-persistence-descriptor-rejects-export-ready-with-blocker") {
        return validate_persistence_descriptor_rejects_export_ready_with_blocker();
    }

    if (command == "validate-persistence-descriptor-rejects-unsupported-checkpoint-record-format") {
        return validate_persistence_descriptor_rejects_unsupported_checkpoint_record_format();
    }

    if (command == "validate-persistence-descriptor-rejects-ready-from-blocked-checkpoint") {
        return validate_persistence_descriptor_rejects_ready_from_blocked_checkpoint();
    }

    if (command == "validate-persistence-descriptor-rejects-terminal-failed-export-ready") {
        return validate_persistence_descriptor_rejects_terminal_failed_export_ready();
    }

    if (command == "validate-persistence-descriptor-rejects-store-adjacent-blocked") {
        return validate_persistence_descriptor_rejects_store_adjacent_blocked();
    }

    if (command == "validate-persistence-review-ok") {
        return validate_persistence_review_ok();
    }

    if (command ==
        "validate-persistence-review-rejects-unsupported-source-descriptor-format") {
        return validate_persistence_review_rejects_unsupported_source_descriptor_format();
    }

    if (command == "build-persistence-review-rejects-invalid-descriptor") {
        return build_persistence_review_rejects_invalid_descriptor();
    }

    if (argc < 3) {
        std::cerr << "expected project descriptor path\n";
        return 1;
    }

    const std::filesystem::path project_descriptor = argv[2];

    if (command == "build-persistence-descriptor-project-workflow-value-flow") {
        return build_persistence_descriptor_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-persistence-descriptor-failed-workflow") {
        return build_persistence_descriptor_failed_workflow(project_descriptor);
    }

    if (command == "build-persistence-descriptor-partial-workflow") {
        return build_persistence_descriptor_partial_workflow(project_descriptor);
    }

    if (command == "build-persistence-descriptor-rejects-invalid-checkpoint-record") {
        return build_persistence_descriptor_rejects_invalid_checkpoint_record(project_descriptor);
    }

    if (command == "build-persistence-descriptor-rejects-checkpoint-workflow-mismatch") {
        return build_persistence_descriptor_rejects_checkpoint_workflow_mismatch(project_descriptor);
    }

    if (command == "build-persistence-review-project-workflow-value-flow") {
        return build_persistence_review_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-persistence-review-failed-workflow") {
        return build_persistence_review_failed_workflow(project_descriptor);
    }

    if (command == "build-persistence-review-partial-workflow") {
        return build_persistence_review_partial_workflow(project_descriptor);
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
