#include "pipeline_durable_store_import_provider.hpp"

#include "pipeline_durable_store_import_provider_steps.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace ahfl::cli {

namespace {

struct ProviderArtifactExecutor {
    ProviderArtifactKind kind;
    bool (*fetch)(ProviderPipelineCache &cache);
    std::optional<ProviderArtifact> (*build)(ProviderPipelineCache &cache);
    bool (*print)(const ProviderArtifact &artifact, std::ostream &out);
};

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    [[nodiscard]] bool fetch_##kind_name(ProviderPipelineCache &cache) {                           \
        return cache.get_##kind_name() != nullptr;                                                 \
    }                                                                                              \
    [[nodiscard]] std::optional<ProviderArtifact> build_##kind_name(                               \
        ProviderPipelineCache &cache) {                                                            \
        const auto *artifact = cache.get_##kind_name();                                            \
        if (artifact == nullptr) {                                                                 \
            return std::nullopt;                                                                   \
        }                                                                                          \
        return ProviderArtifact{*artifact};                                                        \
    }                                                                                              \
    [[nodiscard]] bool print_##kind_name(const ProviderArtifact &artifact, std::ostream &out) {    \
        const auto *typed = std::get_if<ahfl::durable_store_import::artifact_type>(&artifact);     \
        if (typed == nullptr) {                                                                    \
            return false;                                                                          \
        }                                                                                          \
        ahfl::printer(*typed, out);                                                                \
        return true;                                                                               \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

constexpr std::array kProviderArtifactExecutors{
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    ProviderArtifactExecutor{                                                                      \
        ProviderArtifactKind::kind_name,                                                           \
        fetch_##kind_name,                                                                         \
        build_##kind_name,                                                                         \
        print_##kind_name,                                                                         \
    },
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

[[nodiscard]] const ProviderArtifactExecutor *
provider_artifact_executor(ProviderArtifactKind kind) {
    const auto match = std::find_if(kProviderArtifactExecutors.begin(),
                                    kProviderArtifactExecutors.end(),
                                    [kind](const auto &executor) { return executor.kind == kind; });
    return match == kProviderArtifactExecutors.end() ? nullptr : &*match;
}

[[nodiscard]] bool fetch_provider_artifact(ProviderArtifactKind kind,
                                           ProviderPipelineCache &cache) {
    const auto *executor = provider_artifact_executor(kind);
    return executor != nullptr && executor->fetch(cache);
}

} // namespace

ProviderPipeline::ProviderPipeline(const ahfl::ir::Program &program,
                                   const ahfl::handoff::PackageMetadata &metadata,
                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                   const CommandLineOptions &options,
                                   std::string_view command_name)
    : cache_(program, metadata, mock_set, options, command_name) {}

bool prefetch_provider_artifact_dependencies(ProviderArtifactKind kind,
                                             ProviderPipelineCache &cache) {
    const auto node = provider_artifact_node(kind);
    if (!node.has_value()) {
        return false;
    }

    for (const auto dependency : node->dependencies) {
        if (!fetch_provider_artifact(dependency, cache)) {
            return false;
        }
    }
    return true;
}

std::optional<ProviderArtifact> ProviderPipeline::build(ProviderArtifactKind kind) const {
    if (!prefetch_provider_artifact_dependencies(kind, cache_)) {
        return std::nullopt;
    }

    const auto *executor = provider_artifact_executor(kind);
    if (executor == nullptr) {
        return std::nullopt;
    }
    return executor->build(cache_);
}

bool print_provider_artifact(ProviderArtifactKind kind,
                             const ProviderArtifact &artifact,
                             std::ostream &out) {
    const auto *executor = provider_artifact_executor(kind);
    return executor != nullptr && executor->print(artifact, out);
}

int emit_provider_artifact_with_diagnostics(ProviderArtifactKind kind,
                                            const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet &mock_set,
                                            const CommandLineOptions &options,
                                            const OutputContext &io) {
    const auto command_name = provider_artifact_command_name(kind);
    const ProviderPipeline pipeline{
        program,
        metadata,
        mock_set,
        options,
        command_name,
    };
    const auto artifact = pipeline.build(kind);
    if (!artifact.has_value()) {
        return 1;
    }
    if (!print_provider_artifact(kind, *artifact, io.out)) {
        io.err << "error: internal provider artifact dispatch failed for " << command_name << '\n';
        return 1;
    }
    return 0;
}

} // namespace ahfl::cli
