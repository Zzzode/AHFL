#pragma once

#include "tooling/cli/provider/provider_artifact_catalog.hpp"

#include <span>

namespace ahfl::cli {

class ProviderPipelineCache;

[[nodiscard]] bool provider_artifact_graph_has_entry(ProviderArtifactKind kind);

[[nodiscard]] std::span<const ProviderArtifactKind>
provider_artifact_dependencies(ProviderArtifactKind kind);

[[nodiscard]] bool prefetch_provider_artifact_dependencies(ProviderArtifactKind kind,
                                                           ProviderPipelineCache &cache);

} // namespace ahfl::cli
