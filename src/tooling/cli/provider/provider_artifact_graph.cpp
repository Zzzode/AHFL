#include "tooling/cli/provider/provider_artifact_graph.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider_steps.hpp"
#include "tooling/cli/provider/provider_pipeline_cache.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace ahfl::cli {
namespace {

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    [[nodiscard]] std::optional<ProviderArtifact> build_##kind_name(                               \
        ProviderPipelineCache &cache) {                                                            \
        const auto &inputs = cache.inputs();                                                       \
        auto artifact = builder(inputs.program,                                                    \
                                inputs.metadata,                                                   \
                                inputs.mock_set,                                                   \
                                inputs.options,                                                    \
                                inputs.command_name,                                               \
                                cache);                                                            \
        if (!artifact.has_value()) {                                                               \
            return std::nullopt;                                                                   \
        }                                                                                          \
        return ProviderArtifact{std::move(*artifact)};                                             \
    }                                                                                              \
    [[nodiscard]] bool print_##kind_name(const ProviderArtifact &artifact, std::ostream &out) {    \
        const auto *typed = std::get_if<ahfl::durable_store_import::artifact_type>(&artifact);     \
        if (typed == nullptr) {                                                                    \
            return false;                                                                          \
        }                                                                                          \
        ahfl::printer(*typed, out);                                                                \
        return true;                                                                               \
    }
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

constexpr std::array kProviderArtifactGraph{
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id_literal,                       \
                                                        visibility_name,                           \
                                                        order_value,                               \
                                                        dep_count,                                 \
                                                        dependency_list)                           \
    ProviderArtifactGraphNode{                                                                     \
        .kind = ProviderArtifactKind::kind_name,                                                   \
        .build = build_##kind_name,                                                                \
        .print = print_##kind_name,                                                                \
    },
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

} // namespace

std::span<const ProviderArtifactGraphNode> provider_artifact_graph_nodes() {
    return kProviderArtifactGraph;
}

const ProviderArtifactGraphNode *provider_artifact_graph_node(ProviderArtifactKind kind) {
    const auto match = std::find_if(kProviderArtifactGraph.begin(),
                                    kProviderArtifactGraph.end(),
                                    [kind](const auto &node) { return node.kind == kind; });
    return match == kProviderArtifactGraph.end() ? nullptr : &*match;
}

bool print_provider_artifact(ProviderArtifactKind kind,
                             const ProviderArtifact &artifact,
                             std::ostream &out) {
    const auto *node = provider_artifact_graph_node(kind);
    return node != nullptr && node->print(artifact, out);
}

} // namespace ahfl::cli
