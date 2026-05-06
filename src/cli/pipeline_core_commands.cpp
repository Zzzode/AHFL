#include "pipeline_core_commands.hpp"
#include "cli_pipeline_artifacts.hpp"

#include "ahfl/backends/audit_report.hpp"
#include "ahfl/backends/checkpoint_record.hpp"
#include "ahfl/backends/checkpoint_review.hpp"
#include "ahfl/backends/dry_run_trace.hpp"
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
#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/persistence_descriptor/review.hpp"
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/persistence_export/review.hpp"
#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/store_import/review.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::cli {

[[nodiscard]] int
emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                     const ahfl::handoff::PackageMetadata &metadata) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_execution_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] int
emit_dry_run_trace_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_runtime_session_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_execution_journal_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_replay_view_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_audit_report_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_scheduler_snapshot_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_scheduler_review_with_diagnostics(const ahfl::ir::Program &program,
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

    const auto summary = ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_scheduler_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int
emit_checkpoint_record_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_checkpoint_review_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_persistence_descriptor_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_persistence_review_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_export_manifest_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_export_review_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_store_import_descriptor_with_diagnostics(const ahfl::ir::Program &program,
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

[[nodiscard]] int
emit_store_import_review_with_diagnostics(const ahfl::ir::Program &program,
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

} // namespace ahfl::cli
