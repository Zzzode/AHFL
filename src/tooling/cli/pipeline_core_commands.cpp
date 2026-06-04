#include "pipeline_core_commands.hpp"
#include "cli_pipeline_artifacts.hpp"

#include "compiler/backends/pipeline/audit_report.hpp"
#include "compiler/backends/pipeline/checkpoint_record.hpp"
#include "compiler/backends/pipeline/checkpoint_review.hpp"
#include "compiler/backends/pipeline/dry_run_trace.hpp"
#include "compiler/backends/pipeline/execution_journal.hpp"
#include "compiler/backends/pipeline/execution_plan.hpp"
#include "compiler/backends/pipeline/persistence_descriptor.hpp"
#include "compiler/backends/pipeline/persistence_export_manifest.hpp"
#include "compiler/backends/pipeline/persistence_export_review.hpp"
#include "compiler/backends/pipeline/persistence_review.hpp"
#include "compiler/backends/pipeline/replay_view.hpp"
#include "compiler/backends/pipeline/runtime_session.hpp"
#include "compiler/backends/pipeline/scheduler_review.hpp"
#include "compiler/backends/pipeline/scheduler_snapshot.hpp"
#include "compiler/backends/pipeline/store_import_descriptor.hpp"
#include "compiler/backends/pipeline/store_import_review.hpp"
#include "pipeline/observation/checkpoint_record/record.hpp"
#include "pipeline/observation/checkpoint_record/review.hpp"
#include "pipeline/observation/scheduler_snapshot/review.hpp"
#include "pipeline/observation/scheduler_snapshot/snapshot.hpp"
#include "pipeline/persistence/descriptor/descriptor.hpp"
#include "pipeline/persistence/descriptor/review.hpp"
#include "pipeline/persistence/export/manifest.hpp"
#include "pipeline/persistence/export/review.hpp"
#include "pipeline/persistence/store_import/descriptor.hpp"
#include "pipeline/persistence/store_import/review.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::cli {
namespace {

[[nodiscard]] const ahfl::dry_run::CapabilityMockSet *
require_mock_set(const PackagePipelineContext &context) {
    if (context.mock_set == nullptr) {
        context.io.err << "error: internal command dispatch failed: missing capability mocks\n";
        return nullptr;
    }
    return context.mock_set;
}

// Pattern A: emit a cached pipeline artifact directly.
template <CommandKind Command, auto Getter, auto Printer>
[[nodiscard]] int emit_cached(const PackagePipelineContext &context) {
    const auto *mock_set = require_mock_set(context);
    if (mock_set == nullptr)
        return 1;

    CliPipelineArtifacts artifacts({context.program,
                                    context.metadata,
                                    *mock_set,
                                    context.options,
                                    command_name(Command),
                                    context.io.err});
    const auto *result = (artifacts.*Getter)();
    if (result == nullptr)
        return 1;
    Printer(*result, context.io.out);
    return 0;
}

// Pattern B: emit a review summary derived from a base artifact.
template <CommandKind Command, auto Getter, auto ReviewBuilder, auto Printer>
[[nodiscard]] int emit_review(const PackagePipelineContext &context) {
    const auto *mock_set = require_mock_set(context);
    if (mock_set == nullptr)
        return 1;

    CliPipelineArtifacts artifacts({context.program,
                                    context.metadata,
                                    *mock_set,
                                    context.options,
                                    command_name(Command),
                                    context.io.err});
    const auto *base = (artifacts.*Getter)();
    if (base == nullptr)
        return 1;
    const auto summary = ReviewBuilder(*base);
    summary.diagnostics.render(context.io.err);
    if (summary.has_errors() || !summary.summary.has_value())
        return 1;
    Printer(*summary.summary, context.io.out);
    return 0;
}

[[nodiscard]] int emit_execution_plan_with_diagnostics(const PackagePipelineContext &context) {
    const auto plan =
        build_execution_plan_for_cli(context.program, context.metadata, context.io.err);
    if (!plan.has_value())
        return 1;
    ahfl::print_execution_plan_json(*plan, context.io.out);
    return 0;
}

// O(1) dispatch table indexed by CommandKind enum value.
constexpr auto kCorePipelineCommandHandlers = [] {
    std::array<PackageCommandHandler, static_cast<std::size_t>(CommandKind::_Count)> table{};

    table[static_cast<std::size_t>(CommandKind::EmitExecutionPlan)] =
        emit_execution_plan_with_diagnostics;
    table[static_cast<std::size_t>(CommandKind::EmitDryRunTrace)] =
        emit_cached<CommandKind::EmitDryRunTrace,
                    &CliPipelineArtifacts::dry_run_trace,
                    ahfl::print_dry_run_trace_json>;
    table[static_cast<std::size_t>(CommandKind::EmitRuntimeSession)] =
        emit_cached<CommandKind::EmitRuntimeSession,
                    &CliPipelineArtifacts::runtime_session,
                    ahfl::print_runtime_session_json>;
    table[static_cast<std::size_t>(CommandKind::EmitExecutionJournal)] =
        emit_cached<CommandKind::EmitExecutionJournal,
                    &CliPipelineArtifacts::execution_journal,
                    ahfl::print_execution_journal_json>;
    table[static_cast<std::size_t>(CommandKind::EmitReplayView)] =
        emit_cached<CommandKind::EmitReplayView,
                    &CliPipelineArtifacts::replay_view,
                    ahfl::print_replay_view_json>;
    table[static_cast<std::size_t>(CommandKind::EmitAuditReport)] =
        emit_cached<CommandKind::EmitAuditReport,
                    &CliPipelineArtifacts::audit_report,
                    ahfl::print_audit_report_json>;
    table[static_cast<std::size_t>(CommandKind::EmitSchedulerSnapshot)] =
        emit_cached<CommandKind::EmitSchedulerSnapshot,
                    &CliPipelineArtifacts::scheduler_snapshot,
                    ahfl::print_scheduler_snapshot_json>;
    table[static_cast<std::size_t>(CommandKind::EmitSchedulerReview)] =
        emit_review<CommandKind::EmitSchedulerReview,
                    &CliPipelineArtifacts::scheduler_snapshot,
                    ahfl::scheduler_snapshot::build_scheduler_decision_summary,
                    ahfl::print_scheduler_review>;
    table[static_cast<std::size_t>(CommandKind::EmitCheckpointRecord)] =
        emit_cached<CommandKind::EmitCheckpointRecord,
                    &CliPipelineArtifacts::checkpoint_record,
                    ahfl::print_checkpoint_record_json>;
    table[static_cast<std::size_t>(CommandKind::EmitCheckpointReview)] =
        emit_review<CommandKind::EmitCheckpointReview,
                    &CliPipelineArtifacts::checkpoint_record,
                    ahfl::checkpoint_record::build_checkpoint_review_summary,
                    ahfl::print_checkpoint_review>;
    table[static_cast<std::size_t>(CommandKind::EmitPersistenceDescriptor)] =
        emit_cached<CommandKind::EmitPersistenceDescriptor,
                    &CliPipelineArtifacts::persistence_descriptor,
                    ahfl::print_persistence_descriptor_json>;
    table[static_cast<std::size_t>(CommandKind::EmitPersistenceReview)] =
        emit_review<CommandKind::EmitPersistenceReview,
                    &CliPipelineArtifacts::persistence_descriptor,
                    ahfl::persistence_descriptor::build_persistence_review_summary,
                    ahfl::print_persistence_review>;
    table[static_cast<std::size_t>(CommandKind::EmitExportManifest)] =
        emit_cached<CommandKind::EmitExportManifest,
                    &CliPipelineArtifacts::export_manifest,
                    ahfl::print_persistence_export_manifest_json>;
    table[static_cast<std::size_t>(CommandKind::EmitExportReview)] =
        emit_review<CommandKind::EmitExportReview,
                    &CliPipelineArtifacts::export_manifest,
                    ahfl::persistence_export::build_persistence_export_review_summary,
                    ahfl::print_persistence_export_review>;
    table[static_cast<std::size_t>(CommandKind::EmitStoreImportDescriptor)] =
        emit_cached<CommandKind::EmitStoreImportDescriptor,
                    &CliPipelineArtifacts::store_import_descriptor,
                    ahfl::print_store_import_descriptor_json>;
    table[static_cast<std::size_t>(CommandKind::EmitStoreImportReview)] =
        emit_review<CommandKind::EmitStoreImportReview,
                    &CliPipelineArtifacts::store_import_descriptor,
                    ahfl::store_import::build_store_import_review_summary,
                    ahfl::print_store_import_review>;

    return table;
}();

} // namespace

bool handles_core_pipeline_command(CommandKind command) {
    const auto idx = static_cast<std::size_t>(command);
    return idx < kCorePipelineCommandHandlers.size() &&
           kCorePipelineCommandHandlers[idx] != nullptr;
}

std::optional<int> dispatch_core_pipeline_command(CommandKind command,
                                                  const PackagePipelineContext &context) {
    const auto idx = static_cast<std::size_t>(command);
    if (idx >= kCorePipelineCommandHandlers.size()) {
        return std::nullopt;
    }
    const auto handler = kCorePipelineCommandHandlers[idx];
    if (handler == nullptr) {
        return std::nullopt;
    }
    return handler(context);
}

} // namespace ahfl::cli
