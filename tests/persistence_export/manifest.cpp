#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/persistence_export/review.hpp"
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

struct ExportBootstrapFixture {
    ahfl::handoff::ExecutionPlan plan;
    ahfl::runtime_session::RuntimeSession session;
    ahfl::execution_journal::ExecutionJournal journal;
    ahfl::replay_view::ReplayView replay;
    ahfl::scheduler_snapshot::SchedulerSnapshot snapshot;
    ahfl::checkpoint_record::CheckpointRecord record;
    ahfl::persistence_descriptor::CheckpointPersistenceDescriptor descriptor;
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

[[nodiscard]] std::optional<ExportBootstrapFixture>
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

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        *plan, *session, *journal, *replay, *snapshot, *record.record);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return ExportBootstrapFixture{
        .plan = *plan,
        .session = *session,
        .journal = *journal,
        .replay = *replay,
        .snapshot = *snapshot,
        .record = *record.record,
        .descriptor = *descriptor.descriptor,
    };
}

[[nodiscard]] ahfl::persistence_export::PersistenceExportManifest
make_valid_manifest() {
    using namespace ahfl;

    persistence_export::PersistenceExportManifest manifest;
    manifest.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    manifest.session_id = "run-partial-001";
    manifest.run_id = "run-partial-001";
    manifest.input_fixture = "fixture.request.partial";
    manifest.workflow_status = runtime_session::WorkflowSessionStatus::Partial;
    manifest.checkpoint_status = checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    manifest.persistence_status =
        persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport;
    manifest.export_package_identity = "export::workflow-value-flow::run-partial-001::prefix-1";
    manifest.planned_durable_identity =
        "plan::workflow-value-flow::run-partial-001::prefix-1";
    manifest.manifest_boundary_kind =
        persistence_export::ManifestBoundaryKind::LocalBundleOnly;
    manifest.artifact_bundle.entry_count = 2;
    manifest.artifact_bundle.entries = {
        persistence_export::ExportArtifactEntry{
            .artifact_kind =
                persistence_export::ExportArtifactKind::PersistenceDescriptor,
            .logical_artifact_name = "persistence-descriptor.json",
            .source_format_version = std::string(
                persistence_descriptor::kPersistenceDescriptorFormatVersion),
        },
        persistence_export::ExportArtifactEntry{
            .artifact_kind = persistence_export::ExportArtifactKind::CheckpointRecord,
            .logical_artifact_name = "checkpoint-record.json",
            .source_format_version =
                std::string(checkpoint_record::kCheckpointRecordFormatVersion),
        },
    };
    manifest.manifest_ready = true;
    manifest.manifest_status =
        persistence_export::PersistenceExportManifestStatus::ReadyToImport;
    return manifest;
}

int validate_persistence_export_manifest_ok() {
    const auto manifest = make_valid_manifest();
    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_export_manifest_blocked_ok() {
    auto manifest = make_valid_manifest();
    manifest.manifest_ready = false;
    manifest.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;
    manifest.next_required_artifact_kind =
        ahfl::persistence_export::ExportArtifactKind::PersistenceReview;
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::MissingArtifactBundle,
        .message = "waiting for readable export review artifact",
        .logical_artifact_name = "persistence-review.txt",
    };

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_export_manifest_terminal_failed_ok() {
    auto manifest = make_valid_manifest();
    manifest.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    manifest.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    manifest.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    manifest.manifest_ready = false;
    manifest.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed;
    manifest.next_required_artifact_kind.reset();
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::WorkflowFailure,
        .message = "workflow failed before export package could be finalized",
        .logical_artifact_name = std::nullopt,
    };
    manifest.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "first",
        .message = "echo capability failed",
    };

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_export_manifest_terminal_partial_ok() {
    auto manifest = make_valid_manifest();
    manifest.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalPartial;
    manifest.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial;
    manifest.manifest_ready = false;
    manifest.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalPartial;
    manifest.next_required_artifact_kind.reset();
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::WorkflowPartial,
        .message = "partial workflow retained as local export package handoff",
        .logical_artifact_name = std::nullopt,
    };

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_export_manifest_rejects_missing_export_package_identity() {
    auto manifest = make_valid_manifest();
    manifest.export_package_identity.clear();

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected missing export package identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "export_package_identity must not be empty")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_duplicate_artifact_name() {
    auto manifest = make_valid_manifest();
    manifest.artifact_bundle.entries[1].logical_artifact_name =
        "persistence-descriptor.json";

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected duplicate artifact logical name to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "contains duplicate logical_artifact_name")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_ready_with_blocker() {
    auto manifest = make_valid_manifest();
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::MissingArtifactBundle,
        .message = "unexpected blocker",
        .logical_artifact_name = std::nullopt,
    };

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected manifest_ready with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "cannot contain store_import_blocker when manifest_ready is true")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_ready_from_blocked_persistence() {
    auto manifest = make_valid_manifest();
    manifest.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::Blocked;

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected ready import from blocked persistence to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "ReadyToImport status requires ReadyToExport persistence_status")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_unsupported_source_descriptor_format() {
    auto manifest = make_valid_manifest();
    manifest.source_persistence_descriptor_format_version =
        "ahfl.persistence-descriptor.v999";

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source descriptor format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_persistence_descriptor_format_version must be")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_store_import_adjacent_blocked() {
    auto manifest = make_valid_manifest();
    manifest.manifest_ready = false;
    manifest.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;
    manifest.manifest_boundary_kind =
        ahfl::persistence_export::ManifestBoundaryKind::StoreImportAdjacent;
    manifest.next_required_artifact_kind =
        ahfl::persistence_export::ExportArtifactKind::PersistenceReview;
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::WaitingOnPersistenceState,
        .message = "waiting for stable persistence handoff",
        .logical_artifact_name = "persistence-review.txt",
    };

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked store-import-adjacent manifest to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "store-import-adjacent boundary requires ready or completed manifest_status")
               ? 0
               : 1;
}

int validate_persistence_export_manifest_rejects_terminal_failed_without_failure_summary() {
    auto manifest = make_valid_manifest();
    manifest.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    manifest.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    manifest.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    manifest.manifest_ready = false;
    manifest.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed;
    manifest.next_required_artifact_kind.reset();
    manifest.store_import_blocker = ahfl::persistence_export::StoreImportBlocker{
        .kind = ahfl::persistence_export::StoreImportBlockerKind::WorkflowFailure,
        .message = "workflow failed",
        .logical_artifact_name = std::nullopt,
    };
    manifest.workflow_failure_summary.reset();

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_manifest(manifest);
    if (!validation.has_errors()) {
        std::cerr << "expected terminal failed without failure summary to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "TerminalFailed status requires workflow_failure_summary")
               ? 0
               : 1;
}

int build_persistence_export_manifest_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *manifest.manifest;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalCompleted ||
        !value.manifest_ready || value.store_import_blocker.has_value() ||
        value.next_required_artifact_kind.has_value() ||
        value.artifact_bundle.entry_count != 2 ||
        value.export_package_identity != "export::workflow-value-flow::run-001::prefix-2" ||
        value.planned_durable_identity != "plan::workflow-value-flow::run-001::prefix-2") {
        std::cerr << "unexpected completed persistence export manifest bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_manifest_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *manifest.manifest;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed ||
        value.manifest_ready || !value.store_import_blocker.has_value() ||
        value.store_import_blocker->kind !=
            ahfl::persistence_export::StoreImportBlockerKind::WorkflowFailure ||
        value.next_required_artifact_kind.has_value() ||
        !value.workflow_failure_summary.has_value() ||
        value.export_package_identity !=
            "export::workflow-value-flow::run-failed-001::prefix-0" ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-failed-001::prefix-0") {
        std::cerr << "unexpected failed persistence export manifest bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_manifest_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *manifest.manifest;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::ReadyToImport ||
        !value.manifest_ready || value.store_import_blocker.has_value() ||
        value.next_required_artifact_kind.has_value() ||
        value.export_package_identity !=
            "export::workflow-value-flow::run-partial-001::prefix-0" ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-partial-001::prefix-0") {
        std::cerr << "unexpected partial persistence export manifest bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_manifest_rejects_invalid_descriptor(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }
    fixture->descriptor.source_checkpoint_record_format_version = "ahfl.checkpoint-record.v999";

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (!manifest.has_errors()) {
        std::cerr << "expected invalid descriptor export manifest failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            manifest.diagnostics,
            "persistence descriptor source_checkpoint_record_format_version must be")) {
        manifest.diagnostics.render(std::cout);
        std::cerr << "missing invalid descriptor export manifest diagnostic\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_manifest_rejects_descriptor_workflow_mismatch(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }
    fixture->descriptor.workflow_canonical_name = "app::main::WrongWorkflow";

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (!manifest.has_errors()) {
        std::cerr << "expected descriptor workflow mismatch export manifest failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            manifest.diagnostics,
            "persistence export manifest bootstrap persistence descriptor workflow_canonical_name does not match runtime session")) {
        manifest.diagnostics.render(std::cout);
        std::cerr << "missing descriptor workflow mismatch export manifest diagnostic\n";
        return 1;
    }

    return 0;
}

int validate_persistence_export_review_ok() {
    const auto built =
        ahfl::persistence_export::build_persistence_export_review_summary(make_valid_manifest());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_review_summary(*built.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_persistence_export_review_rejects_unsupported_source_manifest_format() {
    const auto built =
        ahfl::persistence_export::build_persistence_export_review_summary(make_valid_manifest());
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_export_manifest_format_version = "ahfl.persistence-export-manifest.v999";

    const auto validation =
        ahfl::persistence_export::validate_persistence_export_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected persistence export review source manifest format failure\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "source_export_manifest_format_version must be")
               ? 0
               : 1;
}

int build_persistence_export_review_rejects_invalid_manifest() {
    auto manifest = make_valid_manifest();
    manifest.export_package_identity.clear();

    const auto summary =
        ahfl::persistence_export::build_persistence_export_review_summary(manifest);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid persistence export review manifest input failure\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   summary.diagnostics,
                   "persistence export manifest export_package_identity must not be empty")
               ? 0
               : 1;
}

int build_persistence_export_review_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::persistence_export::build_persistence_export_review_summary(*manifest.manifest);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalCompleted ||
        !value.manifest_ready || value.store_import_blocker.has_value() ||
        value.next_required_artifact_kind.has_value() ||
        value.artifact_bundle_entry_count != 2 ||
        value.next_action !=
            ahfl::persistence_export::PersistenceExportReviewNextActionKind::
                ArchiveCompletedExportPackage ||
        value.export_package_identity != "export::workflow-value-flow::run-001::prefix-2" ||
        value.planned_durable_identity != "plan::workflow-value-flow::run-001::prefix-2") {
        std::cerr << "unexpected completed persistence export review bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_review_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::persistence_export::build_persistence_export_review_summary(*manifest.manifest);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed ||
        value.manifest_ready || !value.store_import_blocker.has_value() ||
        value.store_import_blocker->kind !=
            ahfl::persistence_export::StoreImportBlockerKind::WorkflowFailure ||
        value.next_required_artifact_kind.has_value() ||
        !value.workflow_failure_summary.has_value() ||
        value.next_action !=
            ahfl::persistence_export::PersistenceExportReviewNextActionKind::
                InvestigateExportFailure ||
        value.export_package_identity !=
            "export::workflow-value-flow::run-failed-001::prefix-0" ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-failed-001::prefix-0") {
        std::cerr << "unexpected failed persistence export review bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_persistence_export_review_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->descriptor);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::persistence_export::build_persistence_export_review_summary(*manifest.manifest);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.checkpoint_status !=
            ahfl::checkpoint_record::CheckpointRecordStatus::ReadyToPersist ||
        value.persistence_status !=
            ahfl::persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport ||
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::ReadyToImport ||
        !value.manifest_ready || value.store_import_blocker.has_value() ||
        value.next_required_artifact_kind.has_value() ||
        value.next_action !=
            ahfl::persistence_export::PersistenceExportReviewNextActionKind::
                HandoffExportPackage ||
        value.export_package_identity !=
            "export::workflow-value-flow::run-partial-001::prefix-0" ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-partial-001::prefix-0") {
        std::cerr << "unexpected partial persistence export review bootstrap result\n";
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

    if (command == "validate-persistence-export-manifest-ok") {
        return validate_persistence_export_manifest_ok();
    }

    if (command == "validate-persistence-export-manifest-blocked-ok") {
        return validate_persistence_export_manifest_blocked_ok();
    }

    if (command == "validate-persistence-export-manifest-terminal-failed-ok") {
        return validate_persistence_export_manifest_terminal_failed_ok();
    }

    if (command == "validate-persistence-export-manifest-terminal-partial-ok") {
        return validate_persistence_export_manifest_terminal_partial_ok();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-missing-export-package-identity") {
        return validate_persistence_export_manifest_rejects_missing_export_package_identity();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-duplicate-artifact-name") {
        return validate_persistence_export_manifest_rejects_duplicate_artifact_name();
    }

    if (command == "validate-persistence-export-manifest-rejects-ready-with-blocker") {
        return validate_persistence_export_manifest_rejects_ready_with_blocker();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-ready-from-blocked-persistence") {
        return validate_persistence_export_manifest_rejects_ready_from_blocked_persistence();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-unsupported-source-descriptor-format") {
        return validate_persistence_export_manifest_rejects_unsupported_source_descriptor_format();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-store-import-adjacent-blocked") {
        return validate_persistence_export_manifest_rejects_store_import_adjacent_blocked();
    }

    if (command ==
        "validate-persistence-export-manifest-rejects-terminal-failed-without-failure-summary") {
        return validate_persistence_export_manifest_rejects_terminal_failed_without_failure_summary();
    }

    if (command == "validate-persistence-export-review-ok") {
        return validate_persistence_export_review_ok();
    }

    if (command ==
        "validate-persistence-export-review-rejects-unsupported-source-manifest-format") {
        return validate_persistence_export_review_rejects_unsupported_source_manifest_format();
    }

    if (command == "build-persistence-export-review-rejects-invalid-manifest") {
        return build_persistence_export_review_rejects_invalid_manifest();
    }

    if (argc < 3) {
        std::cerr << "expected project descriptor path\n";
        return 1;
    }

    const std::filesystem::path project_descriptor = argv[2];

    if (command == "build-persistence-export-manifest-project-workflow-value-flow") {
        return build_persistence_export_manifest_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-persistence-export-manifest-failed-workflow") {
        return build_persistence_export_manifest_failed_workflow(project_descriptor);
    }

    if (command == "build-persistence-export-manifest-partial-workflow") {
        return build_persistence_export_manifest_partial_workflow(project_descriptor);
    }

    if (command == "build-persistence-export-manifest-rejects-invalid-descriptor") {
        return build_persistence_export_manifest_rejects_invalid_descriptor(project_descriptor);
    }

    if (command ==
        "build-persistence-export-manifest-rejects-descriptor-workflow-mismatch") {
        return build_persistence_export_manifest_rejects_descriptor_workflow_mismatch(
            project_descriptor);
    }

    if (command == "build-persistence-export-review-project-workflow-value-flow") {
        return build_persistence_export_review_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-persistence-export-review-failed-workflow") {
        return build_persistence_export_review_failed_workflow(project_descriptor);
    }

    if (command == "build-persistence-export-review-partial-workflow") {
        return build_persistence_export_review_partial_workflow(project_descriptor);
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
