#pragma once

#include "pipeline_durable_store_import.hpp"
#include "pipeline_durable_store_import_provider.hpp"

#include "pipeline/persistence/durable_store_import/provider.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

class ProviderPipelineCache;
struct ReleaseGateArtifacts;

[[nodiscard]] std::optional<ReleaseGateArtifacts>
build_release_gate_artifacts_for_cli(ProviderPipelineCache &cache);

// Internal provider pipeline node builders. The public CLI seam is
// ProviderPipeline::build(ProviderArtifactKind); these declarations are
// generated from the artifact registry to avoid a second hand-maintained list.
//
// Each builder accepts a ProviderPipelineCache& so that dependency artifacts
// can be fetched from the cache rather than rebuilt from scratch.
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    [[nodiscard]] std::optional<ahfl::durable_store_import::artifact_type> builder(                \
        const ahfl::ir::Program &program,                                                          \
        const ahfl::handoff::PackageMetadata &metadata,                                            \
        const ahfl::dry_run::CapabilityMockSet &mock_set,                                          \
        const CommandLineOptions &options,                                                         \
        std::string_view command_name,                                                             \
        ProviderPipelineCache &cache);
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

} // namespace ahfl::cli
