#include "pipeline_runner.hpp"

#include "ahfl/audit_report/report.hpp"
#include "ahfl/backends/audit_report.hpp"
#include "ahfl/backends/checkpoint_record.hpp"
#include "ahfl/backends/checkpoint_review.hpp"
#include "ahfl/backends/dry_run_trace.hpp"
#include "ahfl/backends/durable_store_import_decision.hpp"
#include "ahfl/backends/durable_store_import_decision_review.hpp"
#include "ahfl/backends/durable_store_import_receipt.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_request.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_review.hpp"
#include "ahfl/backends/durable_store_import_receipt_review.hpp"
#include "ahfl/backends/durable_store_import_request.hpp"
#include "ahfl/backends/durable_store_import_review.hpp"
#include "ahfl/backends/execution_journal.hpp"
#include "ahfl/backends/execution_plan.hpp"
#include "ahfl/backends/persistence_descriptor.hpp"
#include "ahfl/backends/persistence_export_manifest.hpp"
#include "ahfl/backends/persistence_export_review.hpp"
#include "ahfl/backends/persistence_review.hpp"
#include "ahfl/backends/replay_view.hpp"
#include "ahfl/backends/runtime_session.hpp"
#include "ahfl/backends/scheduler_review.hpp"
#include "ahfl/backends/scheduler_snapshot.hpp"
#include "ahfl/backends/store_import_descriptor.hpp"
#include "ahfl/backends/store_import_review.hpp"
#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/checkpoint_record/review.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/review.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/persistence_descriptor/review.hpp"
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/persistence_export/review.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/store_import/review.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::cli {
namespace {
[[nodiscard]] std::optional<std::string> to_owned_string(
    std::optional<std::string_view> value) {
    return value.transform([](std::string_view text) { return std::string(text); });
}

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata) {
    auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return std::nullopt;
    }

    return std::move(*plan_result.plan);
}

[[nodiscard]] std::optional<ahfl::dry_run::DryRunRequest>
build_dry_run_request_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const CommandLineOptions &options,
                              std::string_view command_name) {
    auto workflow_name = to_owned_string(options.workflow_name);
    if (!workflow_name.has_value()) {
        workflow_name = plan.entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: " << command_name
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
                              const ahfl::dry_run::CapabilityMockSet &mock_set) {
    auto session_result = ahfl::runtime_session::build_runtime_session(plan, request, mock_set);
    session_result.diagnostics.render(std::cerr);
    if (session_result.has_errors() || !session_result.session.has_value()) {
        return std::nullopt;
    }

    return std::move(*session_result.session);
}

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_execution_journal_for_cli(const ahfl::runtime_session::RuntimeSession &session) {
    auto journal_result = ahfl::execution_journal::build_execution_journal(session);
    journal_result.diagnostics.render(std::cerr);
    if (journal_result.has_errors() || !journal_result.journal.has_value()) {
        return std::nullopt;
    }

    return std::move(*journal_result.journal);
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView> build_replay_view_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal) {
    auto replay_result = ahfl::replay_view::build_replay_view(plan, session, journal);
    replay_result.diagnostics.render(std::cerr);
    if (replay_result.has_errors() || !replay_result.replay.has_value()) {
        return std::nullopt;
    }

    return std::move(*replay_result.replay);
}

[[nodiscard]] std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot>
build_scheduler_snapshot_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                 const ahfl::runtime_session::RuntimeSession &session,
                                 const ahfl::execution_journal::ExecutionJournal &journal,
                                 const ahfl::replay_view::ReplayView &replay) {
    auto snapshot_result =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(plan, session, journal, replay);
    snapshot_result.diagnostics.render(std::cerr);
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
                                const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot) {
    auto record_result =
        ahfl::checkpoint_record::build_checkpoint_record(plan, session, journal, replay, snapshot);
    record_result.diagnostics.render(std::cerr);
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
                                     const ahfl::checkpoint_record::CheckpointRecord &record) {
    auto descriptor_result = ahfl::persistence_descriptor::build_persistence_descriptor(
        plan, session, journal, replay, snapshot, record);
    descriptor_result.diagnostics.render(std::cerr);
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
    const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor &descriptor) {
    auto manifest_result = ahfl::persistence_export::build_persistence_export_manifest(
        plan, session, journal, replay, snapshot, record, descriptor);
    manifest_result.diagnostics.render(std::cerr);
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
    const ahfl::persistence_export::PersistenceExportManifest &manifest) {
    auto store_import_result = ahfl::store_import::build_store_import_descriptor(
        plan, session, journal, replay, snapshot, record, descriptor, manifest);
    store_import_result.diagnostics.render(std::cerr);
    if (store_import_result.has_errors() || !store_import_result.descriptor.has_value()) {
        return std::nullopt;
    }

    return std::move(*store_import_result.descriptor);
}

struct CliPipelineInputs {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet &mock_set;
    const CommandLineOptions &options;
    std::string_view command_name;
};

class CliPipelineArtifacts final {
  public:
    explicit CliPipelineArtifacts(CliPipelineInputs inputs) : inputs_(inputs) {}

    [[nodiscard]] const ahfl::handoff::ExecutionPlan *execution_plan() {
        if (!execution_plan_loaded_) {
            execution_plan_loaded_ = true;
            execution_plan_ = build_execution_plan_for_cli(inputs_.program, inputs_.metadata);
        }

        return execution_plan_ ? &*execution_plan_ : nullptr;
    }

    [[nodiscard]] const ahfl::dry_run::DryRunRequest *dry_run_request() {
        if (!dry_run_request_loaded_) {
            dry_run_request_loaded_ = true;
            const auto *plan = execution_plan();
            if (plan != nullptr) {
                dry_run_request_ =
                    build_dry_run_request_for_cli(*plan, inputs_.options, inputs_.command_name);
            }
        }

        return dry_run_request_ ? &*dry_run_request_ : nullptr;
    }

    [[nodiscard]] const ahfl::runtime_session::RuntimeSession *runtime_session() {
        if (!runtime_session_loaded_) {
            runtime_session_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *request = dry_run_request();
            if (plan != nullptr && request != nullptr) {
                runtime_session_ =
                    build_runtime_session_for_cli(*plan, *request, inputs_.mock_set);
            }
        }

        return runtime_session_ ? &*runtime_session_ : nullptr;
    }

    [[nodiscard]] const ahfl::execution_journal::ExecutionJournal *execution_journal() {
        if (!execution_journal_loaded_) {
            execution_journal_loaded_ = true;
            const auto *session = runtime_session();
            if (session != nullptr) {
                execution_journal_ = build_execution_journal_for_cli(*session);
            }
        }

        return execution_journal_ ? &*execution_journal_ : nullptr;
    }

    [[nodiscard]] const ahfl::replay_view::ReplayView *replay_view() {
        if (!replay_view_loaded_) {
            replay_view_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            if (plan != nullptr && session != nullptr && journal != nullptr) {
                replay_view_ = build_replay_view_for_cli(*plan, *session, *journal);
            }
        }

        return replay_view_ ? &*replay_view_ : nullptr;
    }

    [[nodiscard]] const ahfl::scheduler_snapshot::SchedulerSnapshot *scheduler_snapshot() {
        if (!scheduler_snapshot_loaded_) {
            scheduler_snapshot_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            if (plan != nullptr && session != nullptr && journal != nullptr && replay != nullptr) {
                scheduler_snapshot_ =
                    build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
            }
        }

        return scheduler_snapshot_ ? &*scheduler_snapshot_ : nullptr;
    }

    [[nodiscard]] const ahfl::checkpoint_record::CheckpointRecord *checkpoint_record() {
        if (!checkpoint_record_loaded_) {
            checkpoint_record_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            if (plan != nullptr && session != nullptr && journal != nullptr && replay != nullptr &&
                snapshot != nullptr) {
                checkpoint_record_ =
                    build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
            }
        }

        return checkpoint_record_ ? &*checkpoint_record_ : nullptr;
    }

    [[nodiscard]] const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor *
    persistence_descriptor() {
        if (!persistence_descriptor_loaded_) {
            persistence_descriptor_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            if (plan != nullptr && session != nullptr && journal != nullptr && replay != nullptr &&
                snapshot != nullptr && record != nullptr) {
                persistence_descriptor_ = build_persistence_descriptor_for_cli(
                    *plan, *session, *journal, *replay, *snapshot, *record);
            }
        }

        return persistence_descriptor_ ? &*persistence_descriptor_ : nullptr;
    }

    [[nodiscard]] const ahfl::persistence_export::PersistenceExportManifest *export_manifest() {
        if (!export_manifest_loaded_) {
            export_manifest_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            const auto *descriptor = persistence_descriptor();
            if (plan != nullptr && session != nullptr && journal != nullptr && replay != nullptr &&
                snapshot != nullptr && record != nullptr && descriptor != nullptr) {
                export_manifest_ = build_export_manifest_for_cli(
                    *plan, *session, *journal, *replay, *snapshot, *record, *descriptor);
            }
        }

        return export_manifest_ ? &*export_manifest_ : nullptr;
    }

    [[nodiscard]] const ahfl::store_import::StoreImportDescriptor *store_import_descriptor() {
        if (!store_import_descriptor_loaded_) {
            store_import_descriptor_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *replay = replay_view();
            const auto *snapshot = scheduler_snapshot();
            const auto *record = checkpoint_record();
            const auto *descriptor = persistence_descriptor();
            const auto *manifest = export_manifest();
            if (plan != nullptr && session != nullptr && journal != nullptr && replay != nullptr &&
                snapshot != nullptr && record != nullptr && descriptor != nullptr &&
                manifest != nullptr) {
                store_import_descriptor_ = build_store_import_descriptor_for_cli(
                    *plan, *session, *journal, *replay, *snapshot, *record, *descriptor, *manifest);
            }
        }

        return store_import_descriptor_ ? &*store_import_descriptor_ : nullptr;
    }

    [[nodiscard]] const ahfl::dry_run::DryRunTrace *dry_run_trace() {
        if (!dry_run_trace_loaded_) {
            dry_run_trace_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *request = dry_run_request();
            if (plan != nullptr && request != nullptr) {
                auto dry_run = ahfl::dry_run::run_local_dry_run(*plan, *request, inputs_.mock_set);
                dry_run.diagnostics.render(std::cerr);
                if (!dry_run.has_errors() && dry_run.trace.has_value()) {
                    dry_run_trace_ = std::move(*dry_run.trace);
                }
            }
        }

        return dry_run_trace_ ? &*dry_run_trace_ : nullptr;
    }

    [[nodiscard]] const ahfl::audit_report::AuditReport *audit_report() {
        if (!audit_report_loaded_) {
            audit_report_loaded_ = true;
            const auto *plan = execution_plan();
            const auto *session = runtime_session();
            const auto *journal = execution_journal();
            const auto *trace = dry_run_trace();
            if (plan != nullptr && session != nullptr && journal != nullptr && trace != nullptr) {
                auto report = ahfl::audit_report::build_audit_report(
                    *plan, *session, *journal, *trace);
                report.diagnostics.render(std::cerr);
                if (!report.has_errors() && report.report.has_value()) {
                    audit_report_ = std::move(*report.report);
                }
            }
        }

        return audit_report_ ? &*audit_report_ : nullptr;
    }

  private:
    CliPipelineInputs inputs_;

    bool execution_plan_loaded_{false};
    std::optional<ahfl::handoff::ExecutionPlan> execution_plan_;

    bool dry_run_request_loaded_{false};
    std::optional<ahfl::dry_run::DryRunRequest> dry_run_request_;

    bool runtime_session_loaded_{false};
    std::optional<ahfl::runtime_session::RuntimeSession> runtime_session_;

    bool execution_journal_loaded_{false};
    std::optional<ahfl::execution_journal::ExecutionJournal> execution_journal_;

    bool replay_view_loaded_{false};
    std::optional<ahfl::replay_view::ReplayView> replay_view_;

    bool scheduler_snapshot_loaded_{false};
    std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot> scheduler_snapshot_;

    bool checkpoint_record_loaded_{false};
    std::optional<ahfl::checkpoint_record::CheckpointRecord> checkpoint_record_;

    bool persistence_descriptor_loaded_{false};
    std::optional<ahfl::persistence_descriptor::CheckpointPersistenceDescriptor>
        persistence_descriptor_;

    bool export_manifest_loaded_{false};
    std::optional<ahfl::persistence_export::PersistenceExportManifest> export_manifest_;

    bool store_import_descriptor_loaded_{false};
    std::optional<ahfl::store_import::StoreImportDescriptor> store_import_descriptor_;

    bool dry_run_trace_loaded_{false};
    std::optional<ahfl::dry_run::DryRunTrace> dry_run_trace_;

    bool audit_report_loaded_{false};
    std::optional<ahfl::audit_report::AuditReport> audit_report_;
};

[[nodiscard]] int emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                                       const ahfl::handoff::PackageMetadata &metadata) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_execution_plan_json(*plan, std::cout);
    return 0;
}
[[nodiscard]] int emit_dry_run_trace_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-dry-run-trace",
    });
    const auto *trace = artifacts.dry_run_trace();
    if (trace == nullptr) {
        return 1;
    }

    ahfl::print_dry_run_trace_json(*trace, std::cout);
    return 0;
}

[[nodiscard]] int emit_runtime_session_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-runtime-session",
    });
    const auto *session = artifacts.runtime_session();
    if (session == nullptr) {
        return 1;
    }

    ahfl::print_runtime_session_json(*session, std::cout);
    return 0;
}

[[nodiscard]] int emit_execution_journal_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-execution-journal",
    });
    const auto *journal = artifacts.execution_journal();
    if (journal == nullptr) {
        return 1;
    }

    ahfl::print_execution_journal_json(*journal, std::cout);
    return 0;
}

[[nodiscard]] int emit_replay_view_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-replay-view",
    });
    const auto *replay = artifacts.replay_view();
    if (replay == nullptr) {
        return 1;
    }

    ahfl::print_replay_view_json(*replay, std::cout);
    return 0;
}

[[nodiscard]] int emit_audit_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-audit-report",
    });
    const auto *report = artifacts.audit_report();
    if (report == nullptr) {
        return 1;
    }

    ahfl::print_audit_report_json(*report, std::cout);
    return 0;
}

[[nodiscard]] int emit_scheduler_snapshot_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-scheduler-snapshot",
    });
    const auto *snapshot = artifacts.scheduler_snapshot();
    if (snapshot == nullptr) {
        return 1;
    }

    ahfl::print_scheduler_snapshot_json(*snapshot, std::cout);
    return 0;
}

[[nodiscard]] int emit_scheduler_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-scheduler-review",
    });
    const auto *snapshot = artifacts.scheduler_snapshot();
    if (snapshot == nullptr) {
        return 1;
    }

    const auto summary =
        ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_scheduler_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_checkpoint_record_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-checkpoint-record",
    });
    const auto *record = artifacts.checkpoint_record();
    if (record == nullptr) {
        return 1;
    }

    ahfl::print_checkpoint_record_json(*record, std::cout);
    return 0;
}

[[nodiscard]] int emit_checkpoint_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-checkpoint-review",
    });
    const auto *record = artifacts.checkpoint_record();
    if (record == nullptr) {
        return 1;
    }

    const auto summary = ahfl::checkpoint_record::build_checkpoint_review_summary(*record);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_checkpoint_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_persistence_descriptor_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-persistence-descriptor",
    });
    const auto *descriptor = artifacts.persistence_descriptor();
    if (descriptor == nullptr) {
        return 1;
    }

    ahfl::print_persistence_descriptor_json(*descriptor, std::cout);
    return 0;
}

[[nodiscard]] int emit_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-persistence-review",
    });
    const auto *descriptor = artifacts.persistence_descriptor();
    if (descriptor == nullptr) {
        return 1;
    }

    const auto summary =
        ahfl::persistence_descriptor::build_persistence_review_summary(*descriptor);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_persistence_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_export_manifest_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-export-manifest",
    });
    const auto *manifest = artifacts.export_manifest();
    if (manifest == nullptr) {
        return 1;
    }

    ahfl::print_persistence_export_manifest_json(*manifest, std::cout);
    return 0;
}

[[nodiscard]] int emit_export_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-export-review",
    });
    const auto *manifest = artifacts.export_manifest();
    if (manifest == nullptr) {
        return 1;
    }

    const auto review =
        ahfl::persistence_export::build_persistence_export_review_summary(*manifest);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_persistence_export_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_store_import_descriptor_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-store-import-descriptor",
    });
    const auto *store_import_descriptor = artifacts.store_import_descriptor();
    if (store_import_descriptor == nullptr) {
        return 1;
    }

    ahfl::print_store_import_descriptor_json(*store_import_descriptor, std::cout);
    return 0;
}

[[nodiscard]] int emit_store_import_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        "emit-store-import-review",
    });
    const auto *store_import_descriptor = artifacts.store_import_descriptor();
    if (store_import_descriptor == nullptr) {
        return 1;
    }

    const auto review =
        ahfl::store_import::build_store_import_review_summary(*store_import_descriptor);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_store_import_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportRequest>
build_durable_store_import_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        command_name,
    });
    const auto *store_import_descriptor = artifacts.store_import_descriptor();
    if (store_import_descriptor == nullptr) {
        return std::nullopt;
    }

    const auto request_result = ahfl::durable_store_import::build_durable_store_import_request(
        *store_import_descriptor);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return std::nullopt;
    }

    return *request_result.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecision>
build_durable_store_import_decision_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request =
        build_durable_store_import_request_for_cli(program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    decision.diagnostics.render(std::cerr);
    if (decision.has_errors() || !decision.decision.has_value()) {
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecisionReceipt>
build_durable_store_import_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto decision =
        build_durable_store_import_decision_for_cli(program, metadata, mock_set, options, command_name);
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision);
    receipt.diagnostics.render(std::cerr);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceRequest>
build_durable_store_import_receipt_persistence_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    request.diagnostics.render(std::cerr);
    if (request.has_errors() || !request.request.has_value()) {
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] int emit_durable_store_import_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_request_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-request");
    if (!request.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_request_json(*request, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_request_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_decision_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_durable_store_import_decision_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-decision");
    if (!decision.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_decision_json(*decision, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_receipt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-receipt");
    if (!receipt.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_json(*receipt, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_receipt_persistence_request_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-request");
    if (!request.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_persistence_request_json(*request, std::cout);
    return 0;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptReviewSummary>
build_durable_store_import_receipt_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt_review_summary(
            *receipt);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_receipt_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceReviewSummary>
build_durable_store_import_receipt_persistence_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request = build_durable_store_import_receipt_persistence_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::
            build_durable_store_import_decision_receipt_persistence_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_receipt_persistence_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_persistence_review(*review, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_decision_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_durable_store_import_decision_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-decision-review");
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_review_summary(*decision);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_decision_review(*review.summary, std::cout);
    return 0;
}

struct PackagePipelineContext {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet *mock_set;
    const CommandLineOptions &options;
};

using PackageCommandHandler = int (*)(const PackagePipelineContext &context);

template <int (*CommandFn)(const ahfl::ir::Program &,
                           const ahfl::handoff::PackageMetadata &,
                           const ahfl::dry_run::CapabilityMockSet &,
                           const CommandLineOptions &)>
[[nodiscard]] int invoke_package_command(const PackagePipelineContext &context) {
    if (context.mock_set == nullptr) {
        std::cerr << "error: internal command dispatch failed: missing capability mocks\n";
        return 1;
    }

    return CommandFn(context.program, context.metadata, *context.mock_set, context.options);
}

[[nodiscard]] int invoke_execution_plan_command(const PackagePipelineContext &context) {
    return emit_execution_plan_with_diagnostics(context.program, context.metadata);
}

struct PackageCommandDispatchEntry {
    CommandKind kind;
    PackageCommandHandler handler;
};

constexpr PackageCommandDispatchEntry kPackageCommandDispatch[] = {
    {CommandKind::EmitDryRunTrace,
     invoke_package_command<emit_dry_run_trace_with_diagnostics>},
    {CommandKind::EmitExecutionJournal,
     invoke_package_command<emit_execution_journal_with_diagnostics>},
    {CommandKind::EmitReplayView, invoke_package_command<emit_replay_view_with_diagnostics>},
    {CommandKind::EmitAuditReport, invoke_package_command<emit_audit_report_with_diagnostics>},
    {CommandKind::EmitSchedulerSnapshot,
     invoke_package_command<emit_scheduler_snapshot_with_diagnostics>},
    {CommandKind::EmitCheckpointRecord,
     invoke_package_command<emit_checkpoint_record_with_diagnostics>},
    {CommandKind::EmitCheckpointReview,
     invoke_package_command<emit_checkpoint_review_with_diagnostics>},
    {CommandKind::EmitPersistenceDescriptor,
     invoke_package_command<emit_persistence_descriptor_with_diagnostics>},
    {CommandKind::EmitPersistenceReview,
     invoke_package_command<emit_persistence_review_with_diagnostics>},
    {CommandKind::EmitExportManifest,
     invoke_package_command<emit_export_manifest_with_diagnostics>},
    {CommandKind::EmitExportReview, invoke_package_command<emit_export_review_with_diagnostics>},
    {CommandKind::EmitStoreImportDescriptor,
     invoke_package_command<emit_store_import_descriptor_with_diagnostics>},
    {CommandKind::EmitStoreImportReview,
     invoke_package_command<emit_store_import_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportRequest,
     invoke_package_command<emit_durable_store_import_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReview,
     invoke_package_command<emit_durable_store_import_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportDecision,
     invoke_package_command<emit_durable_store_import_decision_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceipt,
     invoke_package_command<emit_durable_store_import_receipt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceRequest,
     invoke_package_command<emit_durable_store_import_receipt_persistence_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportDecisionReview,
     invoke_package_command<emit_durable_store_import_decision_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptReview,
     invoke_package_command<emit_durable_store_import_receipt_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceReview,
     invoke_package_command<emit_durable_store_import_receipt_persistence_review_with_diagnostics>},
    {CommandKind::EmitSchedulerReview,
     invoke_package_command<emit_scheduler_review_with_diagnostics>},
    {CommandKind::EmitRuntimeSession,
     invoke_package_command<emit_runtime_session_with_diagnostics>},
    {CommandKind::EmitExecutionPlan, invoke_execution_plan_command},
};

[[nodiscard]] std::optional<int> dispatch_package_command_impl(
    CommandKind command, const PackagePipelineContext &context) {
    for (const auto &entry : kPackageCommandDispatch) {
        if (entry.kind == command) {
            return entry.handler(context);
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<int> dispatch_package_command(
    CommandKind command,
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet *mock_set,
    const CommandLineOptions &options) {
    const PackagePipelineContext context{
        program,
        metadata,
        mock_set,
        options,
    };
    return dispatch_package_command_impl(command, context);
}

} // namespace ahfl::cli
