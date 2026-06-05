#include "provider_pipeline_cache.hpp"

#include "pipeline_durable_store_import_provider_steps.hpp"
#include "tooling/cli/provider/provider_artifact_graph.hpp"

namespace ahfl::cli {

ProviderPipelineCache::ProviderPipelineCache(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options,
                                             std::string_view command_name)
    : inputs_{program, metadata, mock_set, options, command_name} {}

const ProviderPipelineInputs &ProviderPipelineCache::inputs() const {
    return inputs_;
}

const ProviderArtifact *ProviderPipelineCache::get(ProviderArtifactKind kind) {
    const auto [entry, inserted] = artifacts_.try_emplace(kind, std::nullopt);
    if (!inserted) {
        return entry->second.has_value() ? &*entry->second : nullptr;
    }

    const auto *graph_node = provider_artifact_graph_node(kind);
    if (graph_node == nullptr) {
        return nullptr;
    }
    for (const auto dependency : provider_artifact_dependencies(kind)) {
        if (get(dependency) == nullptr) {
            return nullptr;
        }
    }

    entry->second = graph_node->build(*this);
    return entry->second.has_value() ? &*entry->second : nullptr;
}

const ReleaseGateArtifacts *ProviderPipelineCache::get_release_gate_artifacts() {
    if (!release_gate_artifacts_loaded_) {
        release_gate_artifacts_loaded_ = true;
        release_gate_artifacts_ = build_release_gate_artifacts_for_cli(*this);
    }
    return release_gate_artifacts_.has_value() ? &*release_gate_artifacts_ : nullptr;
}

} // namespace ahfl::cli
