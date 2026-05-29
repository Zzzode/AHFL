#include "pipeline_core_commands.hpp"
#include "cli_pipeline_artifacts.hpp"
#include "output_context.hpp"

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
#include "pipeline/persistence/descriptor/descriptor.hpp"
#include "pipeline/persistence/descriptor/review.hpp"
#include "pipeline/persistence/export/manifest.hpp"
#include "pipeline/persistence/export/review.hpp"
#include "pipeline/observation/scheduler_snapshot/review.hpp"
#include "pipeline/observation/scheduler_snapshot/snapshot.hpp"
#include "pipeline/persistence/store_import/descriptor.hpp"
#include "pipeline/persistence/store_import/review.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::cli {
namespace {

// Pattern A: emit a cached pipeline artifact directly.
template <auto Getter, auto Printer>
[[nodiscard]] int emit_cached(const ahfl::ir::Program &program,
                              const ahfl::handoff::PackageMetadata &metadata,
                              const ahfl::dry_run::CapabilityMockSet &mock_set,
                              const CommandLineOptions &options, std::string_view cmd,
                              const OutputContext &io = {}) {
    CliPipelineArtifacts artifacts({program, metadata, mock_set, options, cmd, io.err});
    const auto *result = (artifacts.*Getter)();
    if (result == nullptr) return 1;
    Printer(*result, io.out);
    return 0;
}

// Pattern B: emit a review summary derived from a base artifact.
template <auto Getter, auto ReviewBuilder, auto Printer>
[[nodiscard]] int emit_review(const ahfl::ir::Program &program,
                              const ahfl::handoff::PackageMetadata &metadata,
                              const ahfl::dry_run::CapabilityMockSet &mock_set,
                              const CommandLineOptions &options, std::string_view cmd,
                              const OutputContext &io = {}) {
    CliPipelineArtifacts artifacts({program, metadata, mock_set, options, cmd, io.err});
    const auto *base = (artifacts.*Getter)();
    if (base == nullptr) return 1;
    const auto summary = ReviewBuilder(*base);
    summary.diagnostics.render(io.err);
    if (summary.has_errors() || !summary.summary.has_value()) return 1;
    Printer(*summary.summary, io.out);
    return 0;
}

} // namespace

[[nodiscard]] int
emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                     const ahfl::handoff::PackageMetadata &metadata) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) return 1;
    ahfl::print_execution_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] int
emit_dry_run_trace_with_diagnostics(const ahfl::ir::Program &program,
                                    const ahfl::handoff::PackageMetadata &metadata,
                                    const ahfl::dry_run::CapabilityMockSet &mock_set,
                                    const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::dry_run_trace, ahfl::print_dry_run_trace_json>(
        program, metadata, mock_set, options, "emit-dry-run-trace");
}

[[nodiscard]] int
emit_runtime_session_with_diagnostics(const ahfl::ir::Program &program,
                                      const ahfl::handoff::PackageMetadata &metadata,
                                      const ahfl::dry_run::CapabilityMockSet &mock_set,
                                      const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::runtime_session, ahfl::print_runtime_session_json>(
        program, metadata, mock_set, options, "emit-runtime-session");
}

[[nodiscard]] int
emit_execution_journal_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::execution_journal,
                       ahfl::print_execution_journal_json>(
        program, metadata, mock_set, options, "emit-execution-journal");
}

[[nodiscard]] int
emit_replay_view_with_diagnostics(const ahfl::ir::Program &program,
                                  const ahfl::handoff::PackageMetadata &metadata,
                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                  const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::replay_view, ahfl::print_replay_view_json>(
        program, metadata, mock_set, options, "emit-replay-view");
}

[[nodiscard]] int
emit_audit_report_with_diagnostics(const ahfl::ir::Program &program,
                                   const ahfl::handoff::PackageMetadata &metadata,
                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                   const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::audit_report, ahfl::print_audit_report_json>(
        program, metadata, mock_set, options, "emit-audit-report");
}

[[nodiscard]] int
emit_scheduler_snapshot_with_diagnostics(const ahfl::ir::Program &program,
                                         const ahfl::handoff::PackageMetadata &metadata,
                                         const ahfl::dry_run::CapabilityMockSet &mock_set,
                                         const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::scheduler_snapshot,
                       ahfl::print_scheduler_snapshot_json>(
        program, metadata, mock_set, options, "emit-scheduler-snapshot");
}

[[nodiscard]] int
emit_scheduler_review_with_diagnostics(const ahfl::ir::Program &program,
                                       const ahfl::handoff::PackageMetadata &metadata,
                                       const ahfl::dry_run::CapabilityMockSet &mock_set,
                                       const CommandLineOptions &options) {
    return emit_review<&CliPipelineArtifacts::scheduler_snapshot,
                       ahfl::scheduler_snapshot::build_scheduler_decision_summary,
                       ahfl::print_scheduler_review>(
        program, metadata, mock_set, options, "emit-scheduler-review");
}

[[nodiscard]] int
emit_checkpoint_record_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::checkpoint_record,
                       ahfl::print_checkpoint_record_json>(
        program, metadata, mock_set, options, "emit-checkpoint-record");
}

[[nodiscard]] int
emit_checkpoint_review_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options) {
    return emit_review<&CliPipelineArtifacts::checkpoint_record,
                       ahfl::checkpoint_record::build_checkpoint_review_summary,
                       ahfl::print_checkpoint_review>(
        program, metadata, mock_set, options, "emit-checkpoint-review");
}

[[nodiscard]] int
emit_persistence_descriptor_with_diagnostics(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::persistence_descriptor,
                       ahfl::print_persistence_descriptor_json>(
        program, metadata, mock_set, options, "emit-persistence-descriptor");
}

[[nodiscard]] int
emit_persistence_review_with_diagnostics(const ahfl::ir::Program &program,
                                         const ahfl::handoff::PackageMetadata &metadata,
                                         const ahfl::dry_run::CapabilityMockSet &mock_set,
                                         const CommandLineOptions &options) {
    return emit_review<&CliPipelineArtifacts::persistence_descriptor,
                       ahfl::persistence_descriptor::build_persistence_review_summary,
                       ahfl::print_persistence_review>(
        program, metadata, mock_set, options, "emit-persistence-review");
}

[[nodiscard]] int
emit_export_manifest_with_diagnostics(const ahfl::ir::Program &program,
                                      const ahfl::handoff::PackageMetadata &metadata,
                                      const ahfl::dry_run::CapabilityMockSet &mock_set,
                                      const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::export_manifest,
                       ahfl::print_persistence_export_manifest_json>(
        program, metadata, mock_set, options, "emit-export-manifest");
}

[[nodiscard]] int
emit_export_review_with_diagnostics(const ahfl::ir::Program &program,
                                    const ahfl::handoff::PackageMetadata &metadata,
                                    const ahfl::dry_run::CapabilityMockSet &mock_set,
                                    const CommandLineOptions &options) {
    return emit_review<&CliPipelineArtifacts::export_manifest,
                       ahfl::persistence_export::build_persistence_export_review_summary,
                       ahfl::print_persistence_export_review>(
        program, metadata, mock_set, options, "emit-export-review");
}

[[nodiscard]] int
emit_store_import_descriptor_with_diagnostics(const ahfl::ir::Program &program,
                                              const ahfl::handoff::PackageMetadata &metadata,
                                              const ahfl::dry_run::CapabilityMockSet &mock_set,
                                              const CommandLineOptions &options) {
    return emit_cached<&CliPipelineArtifacts::store_import_descriptor,
                       ahfl::print_store_import_descriptor_json>(
        program, metadata, mock_set, options, "emit-store-import-descriptor");
}

[[nodiscard]] int
emit_store_import_review_with_diagnostics(const ahfl::ir::Program &program,
                                          const ahfl::handoff::PackageMetadata &metadata,
                                          const ahfl::dry_run::CapabilityMockSet &mock_set,
                                          const CommandLineOptions &options) {
    return emit_review<&CliPipelineArtifacts::store_import_descriptor,
                       ahfl::store_import::build_store_import_review_summary,
                       ahfl::print_store_import_review>(
        program, metadata, mock_set, options, "emit-store-import-review");
}

} // namespace ahfl::cli
