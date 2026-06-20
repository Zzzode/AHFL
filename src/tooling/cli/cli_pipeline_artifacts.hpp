#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "pipeline/execution/execution_journal/journal.hpp"
#include "pipeline/execution/replay_view/replay.hpp"
#include "pipeline/execution/runtime_session/session.hpp"
#include "pipeline/observation/audit_report/report.hpp"
#include "pipeline/observation/checkpoint_record/record.hpp"
#include "pipeline/observation/scheduler_snapshot/snapshot.hpp"
#include "pipeline/persistence/descriptor/descriptor.hpp"
#include "pipeline/persistence/export/manifest.hpp"
#include "pipeline/persistence/store_import/descriptor.hpp"
#include "tooling/cli/command_catalog.hpp"

#include <iostream>
#include <optional>
#include <ostream>
#include <string_view>

namespace ahfl::cli {

template <typename T> class LazyArtifact {
  public:
    template <typename Builder> const T *get(Builder &&build) {
        if (!loaded_) {
            loaded_ = true;
            value_ = build();
        }
        return value_ ? &*value_ : nullptr;
    }

  private:
    bool loaded_{false};
    std::optional<T> value_;
};

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata,
                             std::ostream &err = std::cerr);

struct CliPipelineInputs {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet &mock_set;
    const CommandLineOptions &options;
    std::string_view command_name;
    std::ostream &err = std::cerr;
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

    LazyArtifact<ahfl::handoff::ExecutionPlan> execution_plan_;
    LazyArtifact<ahfl::dry_run::DryRunRequest> dry_run_request_;
    LazyArtifact<ahfl::runtime_session::RuntimeSession> runtime_session_;
    LazyArtifact<ahfl::execution_journal::ExecutionJournal> execution_journal_;
    LazyArtifact<ahfl::replay_view::ReplayView> replay_view_;
    LazyArtifact<ahfl::scheduler_snapshot::SchedulerSnapshot> scheduler_snapshot_;
    LazyArtifact<ahfl::checkpoint_record::CheckpointRecord> checkpoint_record_;
    LazyArtifact<ahfl::persistence_descriptor::CheckpointPersistenceDescriptor>
        persistence_descriptor_;
    LazyArtifact<ahfl::persistence_export::PersistenceExportManifest> export_manifest_;
    LazyArtifact<ahfl::store_import::StoreImportDescriptor> store_import_descriptor_;
    LazyArtifact<ahfl::dry_run::DryRunTrace> dry_run_trace_;
    LazyArtifact<ahfl::audit_report::AuditReport> audit_report_;
};

} // namespace ahfl::cli
