#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
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
    [[nodiscard]] const ahfl::dry_run::DryRunTrace *dry_run_trace();

  private:
    CliPipelineInputs inputs_;

    LazyArtifact<ahfl::handoff::ExecutionPlan> execution_plan_;
    LazyArtifact<ahfl::dry_run::DryRunRequest> dry_run_request_;
    LazyArtifact<ahfl::dry_run::DryRunTrace> dry_run_trace_;
};

} // namespace ahfl::cli
