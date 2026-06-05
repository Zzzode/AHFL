#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/output_context.hpp"

#include "provider_artifact_graph.hpp"
#include "provider_pipeline_cache.hpp"

#include <iosfwd>
#include <optional>
#include <string_view>

namespace ahfl::cli {

class ProviderPipeline {
  public:
    ProviderPipeline(const ahfl::ir::Program &program,
                     const ahfl::handoff::PackageMetadata &metadata,
                     const ahfl::dry_run::CapabilityMockSet &mock_set,
                     const CommandLineOptions &options,
                     std::string_view command_name);

    [[nodiscard]] std::optional<ProviderArtifact> build(ProviderArtifactKind kind) const;

  private:
    mutable ProviderPipelineCache cache_;
};

[[nodiscard]] int
emit_provider_artifact_with_diagnostics(ProviderArtifactKind kind,
                                        const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options,
                                        const OutputContext &io = {});

} // namespace ahfl::cli
