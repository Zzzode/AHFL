#pragma once

#include "audit_report/report.hpp"
#include "checkpoint_record/record.hpp"
#include "cli/command_catalog.hpp"
#include "dry_run/runner.hpp"
#include "execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "persistence_descriptor/descriptor.hpp"
#include "persistence_export/manifest.hpp"
#include "replay_view/replay.hpp"
#include "runtime_session/session.hpp"
#include "scheduler_snapshot/snapshot.hpp"
#include "store_import/descriptor.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata);

struct CliPipelineInputs {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet &mock_set;
    const CommandLineOptions &options;
    std::string_view command_name;
};

class CliPipelineArtifacts final {
  public:
    explicit CliPipelineArtifacts(CliPipelineInputs inputs);

    [[nodiscard]] const ahfl::handoff::ExecutionPlan *execution_plan();
    [[nodiscard]] const ahfl::dry_run::DryRunRequest *dry_run_request();
    [[nodiscard]] const ahfl::runtime_session::RuntimeSession *runtime_session();
    [[nodiscard]] const ahfl::execution_journal::ExecutionJournal *execution_journal();
    [[nodiscard]] const ahfl::replay_view::ReplayView *replay_view();
    [[nodiscard]] const ahfl::scheduler_snapshot::SchedulerSnapshot *scheduler_snapshot();
    [[nodiscard]] const ahfl::checkpoint_record::CheckpointRecord *checkpoint_record();
    [[nodiscard]] const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor *
    persistence_descriptor();
    [[nodiscard]] const ahfl::persistence_export::PersistenceExportManifest *export_manifest();
    [[nodiscard]] const ahfl::store_import::StoreImportDescriptor *store_import_descriptor();
    [[nodiscard]] const ahfl::dry_run::DryRunTrace *dry_run_trace();
    [[nodiscard]] const ahfl::audit_report::AuditReport *audit_report();

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

} // namespace ahfl::cli
