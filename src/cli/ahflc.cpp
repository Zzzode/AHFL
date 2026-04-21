#include "ahfl/backends/driver.hpp"
#include "ahfl/audit_report/report.hpp"
#include "ahfl/backends/checkpoint_record.hpp"
#include "ahfl/backends/checkpoint_review.hpp"
#include "ahfl/backends/durable_store_import_request.hpp"
#include "ahfl/backends/durable_store_import_decision.hpp"
#include "ahfl/backends/durable_store_import_receipt.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_request.hpp"
#include "ahfl/backends/durable_store_import_decision_review.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_review.hpp"
#include "ahfl/backends/durable_store_import_receipt_review.hpp"
#include "ahfl/backends/durable_store_import_review.hpp"
#include "ahfl/backends/audit_report.hpp"
#include "ahfl/backends/dry_run_trace.hpp"
#include "ahfl/backends/execution_plan.hpp"
#include "ahfl/backends/execution_journal.hpp"
#include "ahfl/backends/persistence_descriptor.hpp"
#include "ahfl/backends/persistence_export_manifest.hpp"
#include "ahfl/backends/persistence_export_review.hpp"
#include "ahfl/backends/persistence_review.hpp"
#include "ahfl/backends/replay_view.hpp"
#include "ahfl/backends/scheduler_review.hpp"
#include "ahfl/backends/runtime_session.hpp"
#include "ahfl/backends/scheduler_snapshot.hpp"
#include "ahfl/backends/store_import_descriptor.hpp"
#include "ahfl/backends/store_import_review.hpp"
#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/checkpoint_record/review.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/review.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/persistence_descriptor/review.hpp"
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/persistence_export/review.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"
#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/store_import/review.hpp"
#include "command_catalog.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;
using ahfl::cli::CommandKind;
using ahfl::cli::CommandLineOptions;
using ahfl::cli::CommandListKind;
using ahfl::cli::command_list;
using ahfl::cli::command_name;
using ahfl::cli::command_token_to_kind;
using ahfl::cli::count_enabled_actions;
using ahfl::cli::format_comma_or_commands;
using ahfl::cli::infer_effective_command;
using ahfl::cli::is_action_enabled;
using ahfl::cli::is_capability_input_supported_command;
using ahfl::cli::is_command_requiring_package;
using ahfl::cli::is_package_supported_command;
using ahfl::cli::print_usage;
using ahfl::cli::set_command_option;

[[nodiscard]] std::optional<ahfl::BackendKind>
selected_backend(std::optional<CommandKind> command) {
    if (!command.has_value()) {
        return std::nullopt;
    }

    switch (*command) {
    case CommandKind::EmitIr:
        return ahfl::BackendKind::Ir;
    case CommandKind::EmitIrJson:
        return ahfl::BackendKind::IrJson;
    case CommandKind::EmitNativeJson:
        return ahfl::BackendKind::NativeJson;
    case CommandKind::EmitExecutionPlan:
        return ahfl::BackendKind::ExecutionPlan;
    case CommandKind::EmitPackageReview:
        return ahfl::BackendKind::PackageReview;
    case CommandKind::EmitSummary:
        return ahfl::BackendKind::Summary;
    case CommandKind::EmitSmv:
        return ahfl::BackendKind::Smv;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] ahfl::handoff::ExecutableKind
to_executable_kind(ahfl::PackageAuthoringTargetKind kind) {
    switch (kind) {
    case ahfl::PackageAuthoringTargetKind::Agent:
        return ahfl::handoff::ExecutableKind::Agent;
    case ahfl::PackageAuthoringTargetKind::Workflow:
        return ahfl::handoff::ExecutableKind::Workflow;
    }

    return ahfl::handoff::ExecutableKind::Workflow;
}

[[nodiscard]] ahfl::handoff::PackageMetadata
lower_package_metadata(const ahfl::PackageAuthoringDescriptor &descriptor) {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string(ahfl::handoff::kFormatVersion),
        .name = descriptor.package_name,
        .version = descriptor.package_version,
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = to_executable_kind(descriptor.entry.kind),
        .canonical_name = descriptor.entry.name,
    };
    metadata.export_targets.reserve(descriptor.exports.size());
    for (const auto &target : descriptor.exports) {
        metadata.export_targets.push_back(ahfl::handoff::ExecutableRef{
            .kind = to_executable_kind(target.kind),
            .canonical_name = target.name,
        });
    }
    for (const auto &binding : descriptor.capability_bindings) {
        metadata.capability_binding_keys.emplace(binding.capability, binding.binding_key);
    }
    return metadata;
}

template <typename InputT>
[[nodiscard]] bool emit_selected_backend(std::optional<CommandKind> effective_command,
                                         const InputT &input,
                                         const ahfl::ResolveResult &resolve_result,
                                         const ahfl::TypeCheckResult &type_check_result,
                                         const ahfl::handoff::PackageMetadata *package_metadata,
                                         std::ostream &out) {
    const auto backend = selected_backend(effective_command);
    if (!backend.has_value()) {
        return false;
    }

    ahfl::emit_backend(
        *backend, input, resolve_result, type_check_result, out, package_metadata);
    return true;
}

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

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  std::string &content,
                                  std::ostream &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics << "error: failed to open capability mock descriptor: "
                    << ahfl::display_path(path) << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
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

[[nodiscard]] std::optional<int> dispatch_package_command(
    CommandKind command, const PackagePipelineContext &context) {
    for (const auto &entry : kPackageCommandDispatch) {
        if (entry.kind == command) {
            return entry.handler(context);
        }
    }

    return std::nullopt;
}

template <typename ResultT>
void render_diagnostics(const ResultT &result, MaybeSourceFile source_file, std::ostream &out) {
    if (source_file.has_value()) {
        result.diagnostics.render(out, *source_file);
        return;
    }

    result.diagnostics.render(out);
}

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_project_ast_outline(graph, out);
}

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << program.declarations.size() << " top-level declaration(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references().size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

void print_success_summary(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << graph.sources.size() << " source(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references().size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

[[nodiscard]] int load_project_input(const CommandLineOptions &options,
                                     const ahfl::Frontend &frontend,
                                     ahfl::ProjectInput &input) {
    if (options.project_descriptor.has_value()) {
        auto descriptor_result =
            frontend.load_project_descriptor(std::string(*options.project_descriptor));
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    if (options.workspace_descriptor.has_value()) {
        auto descriptor_result = frontend.load_project_descriptor_from_workspace(
            std::string(*options.workspace_descriptor), *options.project_name);
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    input.entry_files.push_back(std::string(options.positional.front()));
    input.search_roots.reserve(options.search_roots.size());
    for (const auto search_root : options.search_roots) {
        input.search_roots.push_back(std::string(search_root));
    }

    return -1;
}

template <typename InputT>
[[nodiscard]] int run_analysis_pipeline(const CommandLineOptions &options,
                                        std::optional<CommandKind> effective_command,
                                        const InputT &input,
                                        MaybeSourceFile source_file,
                                        const ahfl::handoff::PackageMetadata *package_metadata,
                                        const ahfl::dry_run::CapabilityMockSet *capability_mock_set) {
    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(input);
    render_diagnostics(resolve_result, source_file, std::cerr);
    if (resolve_result.has_errors()) {
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(input, resolve_result);
    render_diagnostics(type_check_result, source_file, std::cerr);

    if (is_action_enabled(options, CommandKind::DumpTypes)) {
        ahfl::dump_type_environment(
            type_check_result.environment, resolve_result.symbol_table, std::cout);
    }

    if (type_check_result.has_errors()) {
        return 1;
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(input, resolve_result, type_check_result);
    render_diagnostics(validation_result, source_file, std::cerr);
    if (validation_result.has_errors()) {
        return 1;
    }

    if (package_metadata != nullptr) {
        auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        auto metadata_validation =
            ahfl::handoff::validate_package_metadata(ir_program, *package_metadata);
        render_diagnostics(metadata_validation, std::nullopt, std::cerr);
        if (metadata_validation.has_errors()) {
            return 1;
        }

        if (effective_command.has_value()) {
            const PackagePipelineContext context{
                ir_program,
                metadata_validation.metadata,
                capability_mock_set,
                options,
            };
            if (const auto command_status =
                    dispatch_package_command(*effective_command, context);
                command_status.has_value()) {
                return *command_status;
            }
        }

        if (emit_selected_backend(effective_command,
                                  ir_program,
                                  resolve_result,
                                  type_check_result,
                                  &metadata_validation.metadata,
                                  std::cout)) {
            return 0;
        }
    }

    if (emit_selected_backend(effective_command,
                              input,
                              resolve_result,
                              type_check_result,
                              package_metadata,
                              std::cout)) {
        return 0;
    }

    if (!is_action_enabled(options, CommandKind::DumpTypes)) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return 0;
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--project") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--package") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --package requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.package_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--capability-mocks") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --capability-mocks requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.capability_mocks_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--workspace") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workspace requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workspace_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--project-name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project-name requires a project name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_name = arguments[++index];
            continue;
        }

        if (argument == "--workflow") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workflow requires a canonical workflow name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workflow_name = arguments[++index];
            continue;
        }

        if (argument == "--input-fixture") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --input-fixture requires a fixture string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.input_fixture = arguments[++index];
            continue;
        }

        if (argument == "--run-id") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --run-id requires an id string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.run_id = arguments[++index];
            continue;
        }

        if (argument == "--search-root") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --search-root requires a directory path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.search_roots.push_back(arguments[++index]);
            continue;
        }

        if (argument == "--dump-ast") {
            options.dump_ast_requested = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types_requested = true;
            continue;
        }

        const auto command = command_token_to_kind(argument);
        if (command.has_value() && !options.selected_command.has_value() &&
            options.positional.empty()) {
            set_command_option(options, *command);
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        options.positional.push_back(argument);
    }

    return -1;
}

int run_cli(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    if (const auto parse_status = parse_command_line(arguments, options); parse_status >= 0) {
        return parse_status;
    }

    const auto effective_command = infer_effective_command(options);

    const auto action_count = count_enabled_actions(options);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of "
                  << format_comma_or_commands(command_list(CommandListKind::Action)) << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --project cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && options.project_descriptor.has_value()) {
        std::cerr << "error: --workspace cannot be combined with --project\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_name.has_value() && !options.workspace_descriptor.has_value()) {
        std::cerr << "error: --project-name requires --workspace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.project_name.has_value()) {
        std::cerr << "error: --workspace requires --project-name\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() || options.workspace_descriptor.has_value()
            ? !options.positional.empty()
            : options.positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    if (options.package_descriptor.has_value() &&
        (!effective_command.has_value() ||
         !is_package_supported_command(*effective_command))) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.capability_mocks_descriptor.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.input_fixture.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.run_id.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workflow_name.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --workflow is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set;
    if (effective_command.has_value() &&
        is_command_requiring_package(*effective_command)) {
        const auto selected_command_name = command_name(*effective_command);
        if (!options.package_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --package\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.input_fixture.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --input-fixture\n";
            print_usage(std::cerr);
            return 2;
        }

        std::string capability_mocks_content;
        if (!read_text_file(std::string(*options.capability_mocks_descriptor),
                            capability_mocks_content,
                            std::cerr)) {
            return 1;
        }

        auto mock_parse_result =
            ahfl::dry_run::parse_capability_mock_set_json(capability_mocks_content);
        mock_parse_result.diagnostics.render(std::cerr);
        if (mock_parse_result.has_errors() || !mock_parse_result.mock_set.has_value()) {
            return 1;
        }

        capability_mock_set = std::move(*mock_parse_result.mock_set);
    }

    std::optional<ahfl::handoff::PackageMetadata> package_metadata;

    const bool project_mode = options.project_descriptor.has_value() ||
                              options.workspace_descriptor.has_value() ||
                              !options.search_roots.empty();
    const ahfl::Frontend frontend;
    if (options.package_descriptor.has_value()) {
        auto package_result =
            frontend.load_package_authoring_descriptor(std::string(*options.package_descriptor));
        package_result.diagnostics.render(std::cerr);
        if (package_result.has_errors() || !package_result.descriptor.has_value()) {
            return package_result.has_errors() ? 1 : 0;
        }

        package_metadata = lower_package_metadata(*package_result.descriptor);
    }
    if (project_mode || is_action_enabled(options, CommandKind::DumpProject)) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options, frontend, input);
            load_status >= 0) {
            return load_status;
        }

        auto project_result = frontend.parse_project(input);
        render_diagnostics(project_result, std::nullopt, std::cerr);
        if (project_result.has_errors()) {
            return 1;
        }

        if (effective_command == CommandKind::DumpProject) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return 0;
        }

        if (effective_command == CommandKind::DumpAst) {
            dump_ast_outline(project_result.graph, std::cout);
            return 0;
        }

        return run_analysis_pipeline(options,
                                     effective_command,
                                     project_result.graph,
                                     std::nullopt,
                                     package_metadata ? &*package_metadata : nullptr,
                                     capability_mock_set ? &*capability_mock_set : nullptr);
    }

    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    render_diagnostics(parse_result, std::cref(parse_result.source), std::cerr);

    if (parse_result.program && effective_command == CommandKind::DumpAst) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    return run_analysis_pipeline(options,
                                 effective_command,
                                 *parse_result.program,
                                 std::cref(parse_result.source),
                                 package_metadata ? &*package_metadata : nullptr,
                                 capability_mock_set ? &*capability_mock_set : nullptr);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return run_cli(arguments);
}
