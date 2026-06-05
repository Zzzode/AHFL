#pragma once

#include "tooling/cli/provider/provider_artifact_value.hpp"

#include <iosfwd>
#include <optional>
#include <span>

namespace ahfl::cli {

class ProviderPipelineCache;

struct ProviderArtifactGraphNode {
    ProviderArtifactDescriptor descriptor;
    std::span<const ProviderArtifactKind> dependencies;
    std::optional<ProviderArtifact> (*build)(ProviderPipelineCache &cache);
    bool (*print)(const ProviderArtifact &artifact, std::ostream &out);
};

[[nodiscard]] std::span<const ProviderArtifactGraphNode> provider_artifact_graph_nodes();

[[nodiscard]] const ProviderArtifactGraphNode *
provider_artifact_graph_node(ProviderArtifactKind kind);

[[nodiscard]] bool print_provider_artifact(ProviderArtifactKind kind,
                                           const ProviderArtifact &artifact,
                                           std::ostream &out);

} // namespace ahfl::cli
