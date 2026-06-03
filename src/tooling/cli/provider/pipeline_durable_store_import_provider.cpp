#include "pipeline_durable_store_import_provider.hpp"

#include "pipeline_durable_store_import_provider_steps.hpp"
#include "provider_artifact_graph.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"

#include <iostream>
#include <utility>

namespace ahfl::cli {

namespace {

[[nodiscard]] bool fetch_provider_artifact(ProviderArtifactKind kind,
                                           ProviderPipelineCache &cache) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case ProviderArtifactKind::kind_name:                                                          \
        return cache.get_##kind_name() != nullptr;
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return false;
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
    if (!provider_artifact_graph_has_entry(kind)) {
        return false;
    }

    for (const auto dependency : provider_artifact_dependencies(kind)) {
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

    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case ProviderArtifactKind::kind_name: {                                                        \
        const auto *p = cache_.get_##kind_name();                                                  \
        if (p == nullptr) {                                                                        \
            return std::nullopt;                                                                   \
        }                                                                                          \
        return ProviderArtifact{*p};                                                               \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return std::nullopt;
}

bool print_provider_artifact(ProviderArtifactKind kind,
                             const ProviderArtifact &artifact,
                             std::ostream &out) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case ProviderArtifactKind::kind_name: {                                                        \
        const auto *typed = std::get_if<ahfl::durable_store_import::artifact_type>(&artifact);     \
        if (typed == nullptr) {                                                                    \
            return false;                                                                          \
        }                                                                                          \
        ahfl::printer(*typed, out);                                                                \
        return true;                                                                               \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return false;
}

int emit_provider_artifact_with_diagnostics(ProviderArtifactKind kind,
                                            const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet &mock_set,
                                            const CommandLineOptions &options,
                                            const OutputContext &io) {
    const ProviderPipeline pipeline{
        program,
        metadata,
        mock_set,
        options,
        provider_artifact_command_token(kind),
    };
    const auto artifact = pipeline.build(kind);
    if (!artifact.has_value()) {
        return 1;
    }
    if (!print_provider_artifact(kind, *artifact, io.out)) {
        io.err << "error: internal provider artifact dispatch failed for "
               << provider_artifact_command_token(kind) << '\n';
        return 1;
    }
    return 0;
}

} // namespace ahfl::cli
