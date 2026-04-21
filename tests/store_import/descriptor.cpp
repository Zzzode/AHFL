#include "ahfl/frontend/frontend.hpp"
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"
#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/store_import/review.hpp"

#include "../common/test_support.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct StoreImportBootstrapFixture {
    ahfl::handoff::ExecutionPlan plan;
    ahfl::runtime_session::RuntimeSession session;
    ahfl::execution_journal::ExecutionJournal journal;
    ahfl::replay_view::ReplayView replay;
    ahfl::scheduler_snapshot::SchedulerSnapshot snapshot;
    ahfl::checkpoint_record::CheckpointRecord record;
    ahfl::persistence_descriptor::CheckpointPersistenceDescriptor persistence_descriptor;
    ahfl::persistence_export::PersistenceExportManifest export_manifest;
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

[[nodiscard]] std::optional<StoreImportBootstrapFixture>
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

    const auto persistence_descriptor =
        ahfl::persistence_descriptor::build_persistence_descriptor(
            *plan, *session, *journal, *replay, *snapshot, *record.record);
    if (persistence_descriptor.has_errors() || !persistence_descriptor.descriptor.has_value()) {
        persistence_descriptor.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto export_manifest = ahfl::persistence_export::build_persistence_export_manifest(
        *plan, *session, *journal, *replay, *snapshot, *record.record,
        *persistence_descriptor.descriptor);
    if (export_manifest.has_errors() || !export_manifest.manifest.has_value()) {
        export_manifest.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return StoreImportBootstrapFixture{
        .plan = *plan,
        .session = *session,
        .journal = *journal,
        .replay = *replay,
        .snapshot = *snapshot,
        .record = *record.record,
        .persistence_descriptor = *persistence_descriptor.descriptor,
        .export_manifest = *export_manifest.manifest,
    };
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_valid_descriptor() {
    using namespace ahfl;

    store_import::StoreImportDescriptor descriptor;
    descriptor.source_package_identity = handoff::PackageIdentity{
        .format_version = std::string(handoff::kFormatVersion),
        .name = "workflow-value-flow",
        .version = "0.2.0",
    };
    descriptor.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    descriptor.session_id = "run-partial-001";
    descriptor.run_id = "run-partial-001";
    descriptor.input_fixture = "fixture.request.partial";
    descriptor.workflow_status = runtime_session::WorkflowSessionStatus::Partial;
    descriptor.checkpoint_status = checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    descriptor.persistence_status =
        persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport;
    descriptor.manifest_status =
        persistence_export::PersistenceExportManifestStatus::ReadyToImport;
    descriptor.export_package_identity =
        "export::workflow-value-flow::run-partial-001::prefix-1";
    descriptor.store_import_candidate_identity =
        "store-import::workflow-value-flow::run-partial-001::stage-3";
    descriptor.planned_durable_identity =
        "plan::workflow-value-flow::run-partial-001::prefix-1";
    descriptor.descriptor_boundary_kind =
        store_import::StoreImportBoundaryKind::AdapterConsumable;
    descriptor.staging_artifact_set.entry_count = 3;
    descriptor.staging_artifact_set.entries = {
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::ExportManifest,
            .logical_artifact_name = "export-manifest.json",
            .source_format_version = std::string(
                persistence_export::kPersistenceExportManifestFormatVersion),
            .required_for_import = true,
        },
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::PersistenceDescriptor,
            .logical_artifact_name = "persistence-descriptor.json",
            .source_format_version = std::string(
                persistence_descriptor::kPersistenceDescriptorFormatVersion),
            .required_for_import = true,
        },
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::CheckpointRecord,
            .logical_artifact_name = "checkpoint-record.json",
            .source_format_version =
                std::string(checkpoint_record::kCheckpointRecordFormatVersion),
            .required_for_import = true,
        },
    };
    descriptor.import_ready = true;
    descriptor.descriptor_status = store_import::StoreImportDescriptorStatus::ReadyToImport;
    return descriptor;
}

[[nodiscard]] std::optional<ahfl::store_import::StoreImportReviewSummary>
make_valid_review_summary() {
    const auto summary = ahfl::store_import::build_store_import_review_summary(
        make_valid_descriptor());
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

int validate_store_import_descriptor_ok() {
    const auto descriptor = make_valid_descriptor();
    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_store_import_descriptor_blocked_ok() {
    auto descriptor = make_valid_descriptor();
    descriptor.import_ready = false;
    descriptor.descriptor_status = ahfl::store_import::StoreImportDescriptorStatus::Blocked;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind =
        ahfl::store_import::StagingArtifactKind::PersistenceReview;
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::MissingStagingArtifactSet,
        .message = "waiting for readable persistence review staging artifact",
        .logical_artifact_name = "persistence-review.txt",
    };

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_store_import_descriptor_terminal_failed_ok() {
    auto descriptor = make_valid_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed;
    descriptor.import_ready = false;
    descriptor.descriptor_status =
        ahfl::store_import::StoreImportDescriptorStatus::TerminalFailed;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind.reset();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WorkflowFailure,
        .message = "workflow failed before store import handoff could be finalized",
        .logical_artifact_name = std::nullopt,
    };
    descriptor.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "first",
        .message = "echo capability failed",
    };

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_store_import_descriptor_terminal_partial_ok() {
    auto descriptor = make_valid_descriptor();
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalPartial;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalPartial;
    descriptor.import_ready = false;
    descriptor.descriptor_status =
        ahfl::store_import::StoreImportDescriptorStatus::TerminalPartial;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind.reset();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WorkflowPartial,
        .message = "partial workflow retained as local staging handoff",
        .logical_artifact_name = std::nullopt,
    };

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_store_import_descriptor_rejects_missing_candidate_identity() {
    auto descriptor = make_valid_descriptor();
    descriptor.store_import_candidate_identity.clear();

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected missing store import candidate identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "store_import_candidate_identity must not be empty")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_duplicate_artifact_name() {
    auto descriptor = make_valid_descriptor();
    descriptor.staging_artifact_set.entries[1].logical_artifact_name = "export-manifest.json";

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected duplicate staging artifact logical name to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "contains duplicate logical_artifact_name")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_ready_with_blocker() {
    auto descriptor = make_valid_descriptor();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::MissingStagingArtifactSet,
        .message = "unexpected blocker",
        .logical_artifact_name = std::nullopt,
    };

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected import_ready with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "cannot contain staging_blocker when import_ready is true")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_ready_from_blocked_manifest() {
    auto descriptor = make_valid_descriptor();
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected ready import from blocked manifest to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "ReadyToImport status requires ReadyToImport manifest_status")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_unsupported_source_manifest_format() {
    auto descriptor = make_valid_descriptor();
    descriptor.source_export_manifest_format_version = "ahfl.persistence-export-manifest.v999";

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source manifest format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_export_manifest_format_version must be")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_adapter_consumable_blocked() {
    auto descriptor = make_valid_descriptor();
    descriptor.import_ready = false;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;
    descriptor.descriptor_status =
        ahfl::store_import::StoreImportDescriptorStatus::Blocked;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::AdapterConsumable;
    descriptor.next_required_staging_artifact_kind =
        ahfl::store_import::StagingArtifactKind::PersistenceReview;
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WaitingOnExportManifest,
        .message = "waiting for stable export manifest handoff",
        .logical_artifact_name = "export-manifest.json",
    };

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked adapter-consumable descriptor to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   validation.diagnostics,
                   "adapter-consumable boundary requires ready or completed descriptor_status")
               ? 0
               : 1;
}

int validate_store_import_descriptor_rejects_terminal_failed_without_failure_summary() {
    auto descriptor = make_valid_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed;
    descriptor.import_ready = false;
    descriptor.descriptor_status =
        ahfl::store_import::StoreImportDescriptorStatus::TerminalFailed;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind.reset();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WorkflowFailure,
        .message = "workflow failed",
        .logical_artifact_name = std::nullopt,
    };
    descriptor.workflow_failure_summary.reset();

    const auto validation =
        ahfl::store_import::validate_store_import_descriptor(descriptor);
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

int validate_store_import_review_ok() {
    const auto summary = make_valid_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation = ahfl::store_import::validate_store_import_review_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_store_import_review_rejects_unsupported_source_descriptor_format() {
    auto summary = make_valid_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    summary->source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v999";
    const auto validation = ahfl::store_import::validate_store_import_review_summary(*summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source descriptor format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_store_import_descriptor_format_version must be")
               ? 0
               : 1;
}

int build_store_import_descriptor_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
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
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalCompleted ||
        value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::TerminalCompleted ||
        !value.import_ready || value.staging_blocker.has_value() ||
        value.next_required_staging_artifact_kind.has_value() ||
        value.staging_artifact_set.entry_count != 3 ||
        value.export_package_identity != "export::workflow-value-flow::run-001::prefix-2" ||
        value.store_import_candidate_identity !=
            "store-import::workflow-value-flow::run-001::stage-3" ||
        value.planned_durable_identity != "plan::workflow-value-flow::run-001::prefix-2") {
        std::cerr << "unexpected completed store import descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_descriptor_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
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
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed ||
        value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::TerminalFailed ||
        value.import_ready || !value.staging_blocker.has_value() ||
        value.staging_blocker->kind != ahfl::store_import::StagingBlockerKind::WorkflowFailure ||
        value.next_required_staging_artifact_kind.has_value()) {
        std::cerr << "unexpected failed store import descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_descriptor_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
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
        value.manifest_status !=
            ahfl::persistence_export::PersistenceExportManifestStatus::ReadyToImport ||
        value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::ReadyToImport ||
        !value.import_ready || value.staging_blocker.has_value() ||
        value.next_required_staging_artifact_kind.has_value() ||
        value.export_package_identity !=
            "export::workflow-value-flow::run-partial-001::prefix-0" ||
        value.store_import_candidate_identity !=
            "store-import::workflow-value-flow::run-partial-001::stage-3" ||
        value.planned_durable_identity !=
            "plan::workflow-value-flow::run-partial-001::prefix-0") {
        std::cerr << "unexpected partial store import descriptor bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_descriptor_rejects_invalid_manifest(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    fixture->export_manifest.format_version = "ahfl.persistence-export-manifest.v999";
    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (!descriptor.has_errors()) {
        std::cerr << "expected invalid export manifest to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(descriptor.diagnostics,
                               "persistence export manifest format_version must be")
               ? 0
               : 1;
}

int build_store_import_descriptor_rejects_manifest_workflow_mismatch(
    const std::filesystem::path &project_descriptor) {
    auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    fixture->export_manifest.workflow_canonical_name = "app::main::ElseWorkflow";
    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (!descriptor.has_errors()) {
        std::cerr << "expected export manifest workflow mismatch to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
                   descriptor.diagnostics,
                   "export manifest workflow_canonical_name does not match derived export manifest")
               ? 0
               : 1;
}

int build_store_import_review_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::store_import::build_store_import_review_summary(*descriptor.descriptor);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *review.summary;
    if (value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::TerminalCompleted ||
        !value.import_ready || value.staging_blocker.has_value() ||
        value.next_action !=
            ahfl::store_import::StoreImportReviewNextActionKind::
                ArchiveCompletedStoreImportState ||
        value.staging_artifact_entry_count != 3 ||
        value.store_import_candidate_identity !=
            "store-import::workflow-value-flow::run-001::stage-3") {
        std::cerr << "unexpected completed store import review bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_review_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Failed);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::store_import::build_store_import_review_summary(*descriptor.descriptor);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *review.summary;
    if (value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::TerminalFailed ||
        value.import_ready || !value.staging_blocker.has_value() ||
        value.staging_blocker->kind !=
            ahfl::store_import::StagingBlockerKind::WorkflowFailure ||
        value.next_action !=
            ahfl::store_import::StoreImportReviewNextActionKind::
                InvestigateStoreImportFailure) {
        std::cerr << "unexpected failed store import review bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_review_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Partial);
    if (!fixture.has_value()) {
        return 1;
    }

    const auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::store_import::build_store_import_review_summary(*descriptor.descriptor);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *review.summary;
    if (value.descriptor_status !=
            ahfl::store_import::StoreImportDescriptorStatus::ReadyToImport ||
        !value.import_ready || value.staging_blocker.has_value() ||
        value.next_action !=
            ahfl::store_import::StoreImportReviewNextActionKind::
                HandoffStoreImportDescriptor ||
        value.store_import_candidate_identity !=
            "store-import::workflow-value-flow::run-partial-001::stage-3") {
        std::cerr << "unexpected partial store import review bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_store_import_review_rejects_invalid_descriptor(
    const std::filesystem::path &project_descriptor) {
    const auto fixture = build_bootstrap_fixture(project_descriptor, SessionScenario::Completed);
    if (!fixture.has_value()) {
        return 1;
    }

    auto descriptor = ahfl::store_import::build_store_import_descriptor(
        fixture->plan,
        fixture->session,
        fixture->journal,
        fixture->replay,
        fixture->snapshot,
        fixture->record,
        fixture->persistence_descriptor,
        fixture->export_manifest);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        descriptor.diagnostics.render(std::cout);
        return 1;
    }

    descriptor.descriptor->format_version = "ahfl.store-import-descriptor.v999";
    const auto review =
        ahfl::store_import::build_store_import_review_summary(*descriptor.descriptor);
    if (!review.has_errors()) {
        std::cerr << "expected invalid store import descriptor to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(review.diagnostics,
                               "store import descriptor format_version must be")
               ? 0
               : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "expected test command\n";
        return 1;
    }

    const std::string_view command = argv[1];

    if (command == "validate-store-import-descriptor-ok") {
        return validate_store_import_descriptor_ok();
    }

    if (command == "validate-store-import-descriptor-blocked-ok") {
        return validate_store_import_descriptor_blocked_ok();
    }

    if (command == "validate-store-import-descriptor-terminal-failed-ok") {
        return validate_store_import_descriptor_terminal_failed_ok();
    }

    if (command == "validate-store-import-descriptor-terminal-partial-ok") {
        return validate_store_import_descriptor_terminal_partial_ok();
    }

    if (command == "validate-store-import-descriptor-rejects-missing-candidate-identity") {
        return validate_store_import_descriptor_rejects_missing_candidate_identity();
    }

    if (command == "validate-store-import-descriptor-rejects-duplicate-artifact-name") {
        return validate_store_import_descriptor_rejects_duplicate_artifact_name();
    }

    if (command == "validate-store-import-descriptor-rejects-ready-with-blocker") {
        return validate_store_import_descriptor_rejects_ready_with_blocker();
    }

    if (command == "validate-store-import-descriptor-rejects-ready-from-blocked-manifest") {
        return validate_store_import_descriptor_rejects_ready_from_blocked_manifest();
    }

    if (command ==
        "validate-store-import-descriptor-rejects-unsupported-source-manifest-format") {
        return validate_store_import_descriptor_rejects_unsupported_source_manifest_format();
    }

    if (command == "validate-store-import-descriptor-rejects-adapter-consumable-blocked") {
        return validate_store_import_descriptor_rejects_adapter_consumable_blocked();
    }

    if (command ==
        "validate-store-import-descriptor-rejects-terminal-failed-without-failure-summary") {
        return validate_store_import_descriptor_rejects_terminal_failed_without_failure_summary();
    }

    if (command == "validate-store-import-review-ok") {
        return validate_store_import_review_ok();
    }

    if (command ==
        "validate-store-import-review-rejects-unsupported-source-descriptor-format") {
        return validate_store_import_review_rejects_unsupported_source_descriptor_format();
    }

    if (argc < 3) {
        std::cerr << "expected project descriptor path\n";
        return 1;
    }

    const auto project_descriptor = std::filesystem::path(argv[2]);

    if (command == "build-store-import-descriptor-project-workflow-value-flow") {
        return build_store_import_descriptor_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-store-import-descriptor-failed-workflow") {
        return build_store_import_descriptor_failed_workflow(project_descriptor);
    }

    if (command == "build-store-import-descriptor-partial-workflow") {
        return build_store_import_descriptor_partial_workflow(project_descriptor);
    }

    if (command == "build-store-import-descriptor-rejects-invalid-manifest") {
        return build_store_import_descriptor_rejects_invalid_manifest(project_descriptor);
    }

    if (command == "build-store-import-descriptor-rejects-manifest-workflow-mismatch") {
        return build_store_import_descriptor_rejects_manifest_workflow_mismatch(project_descriptor);
    }

    if (command == "build-store-import-review-project-workflow-value-flow") {
        return build_store_import_review_project_workflow_value_flow(project_descriptor);
    }

    if (command == "build-store-import-review-failed-workflow") {
        return build_store_import_review_failed_workflow(project_descriptor);
    }

    if (command == "build-store-import-review-partial-workflow") {
        return build_store_import_review_partial_workflow(project_descriptor);
    }

    if (command == "build-store-import-review-rejects-invalid-descriptor") {
        return build_store_import_review_rejects_invalid_descriptor(project_descriptor);
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
