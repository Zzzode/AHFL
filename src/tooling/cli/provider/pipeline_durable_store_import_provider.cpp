#include "pipeline_durable_store_import_provider.hpp"

namespace ahfl::cli {

ProviderPipeline::ProviderPipeline(const ahfl::ir::Program &program,
                                   const ahfl::handoff::PackageMetadata &metadata,
                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                   const CommandLineOptions &options,
                                   std::string_view command_name)
    : cache_(program, metadata, mock_set, options, command_name) {}

std::optional<ProviderArtifact> ProviderPipeline::build(ProviderArtifactKind kind) const {
    const auto *artifact = cache_.get(kind);
    if (artifact == nullptr) {
        return std::nullopt;
    }
    return *artifact;
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
