#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/output_context.hpp"

#include "pipeline/persistence/durable_store_import/provider.hpp"

#include "provider_artifact_catalog.hpp"
#include "provider_pipeline_cache.hpp"

#include <iosfwd>
#include <optional>
#include <string_view>
#include <variant>

namespace ahfl::cli {

using ProviderArtifact = std::variant<std::monostate
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind, command_kind, artifact_type, builder, printer, command_token, visibility, order)         \
    , ahfl::durable_store_import::artifact_type
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
                                      >;

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

[[nodiscard]] bool print_provider_artifact(ProviderArtifactKind kind,
                                           const ProviderArtifact &artifact,
                                           std::ostream &out);

[[nodiscard]] int
emit_provider_artifact_with_diagnostics(ProviderArtifactKind kind,
                                        const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options,
                                        const OutputContext &io = {});

} // namespace ahfl::cli
