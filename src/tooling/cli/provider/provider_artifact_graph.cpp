#include "tooling/cli/provider/provider_artifact_graph.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider_steps.hpp"
#include "tooling/cli/provider/provider_pipeline_cache.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace ahfl::cli {
namespace {

using Kind = ProviderArtifactKind;

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

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id_literal,                       \
                                                        visibility_name,                           \
                                                        order_value,                               \
                                                        dep_count,                                 \
                                                        dependency_list)                           \
    constexpr std::array<Kind, dep_count> kDependencies_##kind_name dependency_list;
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
        .descriptor =                                                                              \
            {                                                                                      \
                .kind = ProviderArtifactKind::kind_name,                                           \
                .artifact_id = artifact_id_literal,                                                \
                .visibility = ProviderArtifactVisibility::visibility_name,                         \
                .order = order_value,                                                              \
            },                                                                                     \
        .dependencies = std::span<const Kind>{kDependencies_##kind_name},                          \
        .build = build_##kind_name,                                                                \
        .print = print_##kind_name,                                                                \
    },
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

[[nodiscard]] consteval bool provider_artifact_orders_are_unique() {
    for (std::size_t lhs = 0; lhs < kProviderArtifactGraph.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < kProviderArtifactGraph.size(); ++rhs) {
            if (kProviderArtifactGraph[lhs].descriptor.order ==
                kProviderArtifactGraph[rhs].descriptor.order) {
                return false;
            }
        }
    }
    return true;
}

static_assert(provider_artifact_orders_are_unique(),
              "duplicate provider artifact order in graph registry");

} // namespace

std::span<const ProviderArtifactGraphNode> provider_artifact_graph_nodes() {
    return kProviderArtifactGraph;
}

const ProviderArtifactGraphNode *provider_artifact_graph_node(ProviderArtifactKind kind) {
    const auto match =
        std::find_if(kProviderArtifactGraph.begin(),
                     kProviderArtifactGraph.end(),
                     [kind](const auto &node) { return node.descriptor.kind == kind; });
    return match == kProviderArtifactGraph.end() ? nullptr : &*match;
}

bool print_provider_artifact(ProviderArtifactKind kind,
                             const ProviderArtifact &artifact,
                             std::ostream &out) {
    const auto *node = provider_artifact_graph_node(kind);
    return node != nullptr && node->print(artifact, out);
}

} // namespace ahfl::cli
