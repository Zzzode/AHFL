#include "cli_pipeline_artifacts.hpp"

#include "compiler/backends/pipeline/execution_plan.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::cli {
namespace {

[[nodiscard]] std::optional<std::string> to_owned_string(std::optional<std::string_view> value) {
    return value.transform([](std::string_view text) { return std::string(text); });
}

[[nodiscard]] std::optional<ahfl::dry_run::DryRunRequest>
build_dry_run_request_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const CommandLineOptions &options,
                              std::string_view command_name,
                              std::ostream &err) {
    auto workflow_name = to_owned_string(options.workflow_name);
    if (!workflow_name.has_value()) {
        workflow_name = plan.entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        err << "error: " << command_name
                  << " requires --workflow or package workflow entry\n";
        return std::nullopt;
    }

    return ahfl::dry_run::DryRunRequest{
        .workflow_canonical_name = std::move(*workflow_name),
        .input_fixture = std::string(*options.input_fixture),
        .run_id = to_owned_string(options.run_id),
    };
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_runtime_session_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const ahfl::dry_run::DryRunRequest &request,
                              const ahfl::dry_run::CapabilityMockSet &mock_set,
                              std::ostream &err) {
    auto session_result = ahfl::runtime_session::build_runtime_session(plan, request, mock_set);
    session_result.diagnostics.render(err);
    if (session_result.has_errors() || !session_result.session.has_value()) {
        return std::nullopt;
    }

    return std::move(*session_result.session);
}

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_execution_journal_for_cli(const ahfl::runtime_session::RuntimeSession &session,
                                std::ostream &err) {
    auto journal_result = ahfl::execution_journal::build_execution_journal(session);
    journal_result.diagnostics.render(err);
    if (journal_result.has_errors() || !journal_result.journal.has_value()) {
        return std::nullopt;
    }

    return std::move(*journal_result.journal);
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView>
build_replay_view_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                          const ahfl::runtime_session::RuntimeSession &session,
                          const ahfl::execution_journal::ExecutionJournal &journal,
                          std::ostream &err) {
    auto replay_result = ahfl::replay_view::build_replay_view(plan, session, journal);
    replay_result.diagnostics.render(err);
    if (replay_result.has_errors() || !replay_result.replay.has_value()) {
        return std::nullopt;
    }

    return std::move(*replay_result.replay);
}

[[nodiscard]] std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot>
build_scheduler_snapshot_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                 const ahfl::runtime_session::RuntimeSession &session,
                                 const ahfl::execution_journal::ExecutionJournal &journal,
                                 const ahfl::replay_view::ReplayView &replay,
                                 std::ostream &err) {
    auto snapshot_result =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(plan, session, journal, replay);
    snapshot_result.diagnostics.render(err);
    if (snapshot_result.has_errors() || !snapshot_result.snapshot.has_value()) {
        return std::nullopt;
    }

    return std::move(*snapshot_result.snapshot);
}

[[nodiscard]] std::optional<ahfl::checkpoint_record::CheckpointRecord>
build_checkpoint_record_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                const ahfl::runtime_session::RuntimeSession &session,
                                const ahfl::execution_journal::ExecutionJournal &journal,
                                const ahfl::replay_view::ReplayView &replay,
                                const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
                                std::ostream &err) {
    auto record_result =
        ahfl::checkpoint_record::build_checkpoint_record(plan, session, journal, replay, snapshot);
    record_result.diagnostics.render(err);
    if (record_result.has_errors() || !record_result.record.has_value()) {
        return std::nullopt;
    }

    return std::move(*record_result.record);
}

[[nodiscard]] std::optional<ahfl::persistence_descriptor::CheckpointPersistenceDescriptor>
build_persistence_descriptor_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                     const ahfl::runtime_session::RuntimeSession &session,
                                     const ahfl::execution_journal::ExecutionJournal &journal,
                                     const ahfl::replay_view::ReplayView &replay,
                                     const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
                                     const ahfl::checkpoint_record::CheckpointRecord &record,
                                     std::ostream &err) {
    auto descriptor_result = ahfl::persistence_descriptor::build_persistence_descriptor(
        plan, session, journal, replay, snapshot, record);
    descriptor_result.diagnostics.render(err);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        return std::nullopt;
    }

    return std::move(*descriptor_result.descriptor);
}

[[nodiscard]] std::optional<ahfl::persistence_export::PersistenceExportManifest>
build_export_manifest_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal,
    const ahfl::replay_view::ReplayView &replay,
    const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
    const ahfl::checkpoint_record::CheckpointRecord &record,
    const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor &descriptor,
    std::ostream &err) {
    auto manifest_result = ahfl::persistence_export::build_persistence_export_manifest(
        plan, session, journal, replay, snapshot, record, descriptor);
    manifest_result.diagnostics.render(err);
    if (manifest_result.has_errors() || !manifest_result.manifest.has_value()) {
        return std::nullopt;
    }

    return std::move(*manifest_result.manifest);
}

[[nodiscard]] std::optional<ahfl::store_import::StoreImportDescriptor>
build_store_import_descriptor_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal,
    const ahfl::replay_view::ReplayView &replay,
    const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
    const ahfl::checkpoint_record::CheckpointRecord &record,
    const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor &descriptor,
    const ahfl::persistence_export::PersistenceExportManifest &manifest,
    std::ostream &err) {
    auto store_import_result = ahfl::store_import::build_store_import_descriptor(
        plan, session, journal, replay, snapshot, record, descriptor, manifest);
    store_import_result.diagnostics.render(err);
    if (store_import_result.has_errors() || !store_import_result.descriptor.has_value()) {
        return std::nullopt;
    }

    return std::move(*store_import_result.descriptor);
}

} // namespace

std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata,
                             std::ostream &err) {
    auto plan_result =
        ahfl::handoff::build_execution_plan(ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(err);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return std::nullopt;
    }

    return std::move(*plan_result.plan);
}

CliPipelineArtifacts::CliPipelineArtifacts(CliPipelineInputs inputs) : inputs_(inputs) {}

const ahfl::handoff::ExecutionPlan *CliPipelineArtifacts::execution_plan() {
    return execution_plan_.get([&] {
        return build_execution_plan_for_cli(inputs_.program, inputs_.metadata, inputs_.err);
    });
}

const ahfl::dry_run::DryRunRequest *CliPipelineArtifacts::dry_run_request() {
    return dry_run_request_.get([&]() -> std::optional<ahfl::dry_run::DryRunRequest> {
        const auto *plan = execution_plan();
        if (plan == nullptr) return std::nullopt;
        return build_dry_run_request_for_cli(*plan, inputs_.options, inputs_.command_name, inputs_.err);
    });
}

const ahfl::runtime_session::RuntimeSession *CliPipelineArtifacts::runtime_session() {
    return runtime_session_.get([&]() -> std::optional<ahfl::runtime_session::RuntimeSession> {
        const auto *plan = execution_plan();
        const auto *request = dry_run_request();
        if (plan == nullptr || request == nullptr) return std::nullopt;
        return build_runtime_session_for_cli(*plan, *request, inputs_.mock_set, inputs_.err);
    });
}

const ahfl::execution_journal::ExecutionJournal *CliPipelineArtifacts::execution_journal() {
    return execution_journal_.get([&]() -> std::optional<ahfl::execution_journal::ExecutionJournal> {
        const auto *session = runtime_session();
        if (session == nullptr) return std::nullopt;
        return build_execution_journal_for_cli(*session, inputs_.err);
    });
}

const ahfl::replay_view::ReplayView *CliPipelineArtifacts::replay_view() {
    return replay_view_.get([&]() -> std::optional<ahfl::replay_view::ReplayView> {
        const auto *plan = execution_plan();
        const auto *session = runtime_session();
        const auto *journal = execution_journal();
        if (plan == nullptr || session == nullptr || journal == nullptr) return std::nullopt;
        return build_replay_view_for_cli(*plan, *session, *journal, inputs_.err);
    });
}

const ahfl::scheduler_snapshot::SchedulerSnapshot *CliPipelineArtifacts::scheduler_snapshot() {
    return scheduler_snapshot_.get(
        [&]() -> std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot> {
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            if (plan == nullptr || session == nullptr || journal == nullptr || replay == nullptr)
                return std::nullopt;
            return build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay, inputs_.err);
        });
}

const ahfl::checkpoint_record::CheckpointRecord *CliPipelineArtifacts::checkpoint_record() {
    return checkpoint_record_.get(
        [&]() -> std::optional<ahfl::checkpoint_record::CheckpointRecord> {
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            if (plan == nullptr || session == nullptr || journal == nullptr || replay == nullptr ||
                snapshot == nullptr)
                return std::nullopt;
            return build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot, inputs_.err);
        });
}

const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor *
CliPipelineArtifacts::persistence_descriptor() {
    return persistence_descriptor_.get(
        [&]() -> std::optional<ahfl::persistence_descriptor::CheckpointPersistenceDescriptor> {
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            if (plan == nullptr || session == nullptr || journal == nullptr || replay == nullptr ||
                snapshot == nullptr || record == nullptr)
                return std::nullopt;
            return build_persistence_descriptor_for_cli(*plan, *session, *journal, *replay,
                                                       *snapshot, *record, inputs_.err);
        });
}

const ahfl::persistence_export::PersistenceExportManifest *
CliPipelineArtifacts::export_manifest() {
    return export_manifest_.get(
        [&]() -> std::optional<ahfl::persistence_export::PersistenceExportManifest> {
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            const auto *descriptor = persistence_descriptor();
            if (plan == nullptr || session == nullptr || journal == nullptr || replay == nullptr ||
                snapshot == nullptr || record == nullptr || descriptor == nullptr)
                return std::nullopt;
            return build_export_manifest_for_cli(*plan, *session, *journal, *replay, *snapshot,
                                                 *record, *descriptor, inputs_.err);
        });
}

const ahfl::store_import::StoreImportDescriptor *CliPipelineArtifacts::store_import_descriptor() {
    return store_import_descriptor_.get(
        [&]() -> std::optional<ahfl::store_import::StoreImportDescriptor> {
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            const auto *descriptor = persistence_descriptor();
            const auto *manifest = export_manifest();
            if (plan == nullptr || session == nullptr || journal == nullptr || replay == nullptr ||
                snapshot == nullptr || record == nullptr || descriptor == nullptr ||
                manifest == nullptr)
                return std::nullopt;
            return build_store_import_descriptor_for_cli(*plan, *session, *journal, *replay,
                                                        *snapshot, *record, *descriptor, *manifest, inputs_.err);
        });
}

const ahfl::dry_run::DryRunTrace *CliPipelineArtifacts::dry_run_trace() {
    return dry_run_trace_.get([&]() -> std::optional<ahfl::dry_run::DryRunTrace> {
        const auto *plan = execution_plan();
        const auto *request = dry_run_request();
        if (plan == nullptr || request == nullptr) return std::nullopt;
        auto dry_run = ahfl::dry_run::run_local_dry_run(*plan, *request, inputs_.mock_set);
        dry_run.diagnostics.render(inputs_.err);
        if (dry_run.has_errors() || !dry_run.trace.has_value()) return std::nullopt;
        return std::move(*dry_run.trace);
    });
}

const ahfl::audit_report::AuditReport *CliPipelineArtifacts::audit_report() {
    return audit_report_.get([&]() -> std::optional<ahfl::audit_report::AuditReport> {
        const auto *plan = execution_plan();
        const auto *session = runtime_session();
        const auto *journal = execution_journal();
        const auto *trace = dry_run_trace();
        if (plan == nullptr || session == nullptr || journal == nullptr || trace == nullptr)
            return std::nullopt;
        auto report = ahfl::audit_report::build_audit_report(*plan, *session, *journal, *trace);
        report.diagnostics.render(inputs_.err);
        if (report.has_errors() || !report.report.has_value()) return std::nullopt;
        return std::move(*report.report);
    });
}

} // namespace ahfl::cli
