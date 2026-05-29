#include "pipeline_durable_store_import_provider.hpp"

#include "pipeline_durable_store_import_provider_steps.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"

#include <iostream>
#include <utility>

namespace ahfl::cli {

ProviderPipeline::ProviderPipeline(const ahfl::ir::Program &program,
                                   const ahfl::handoff::PackageMetadata &metadata,
                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                   const CommandLineOptions &options,
                                   std::string_view command_name)
    : cache_(program, metadata, mock_set, options, command_name) {}

std::optional<ProviderArtifact> ProviderPipeline::build(ProviderArtifactKind kind) const {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token,                            \
                                                        visibility)                               \
    case ProviderArtifactKind::kind_name: {                                                       \
        const auto *p = cache_.get_##kind_name();                                                 \
        if (p == nullptr) {                                                                       \
            return std::nullopt;                                                                  \
        }                                                                                         \
        return ProviderArtifact{*p};                                                              \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return std::nullopt;
}

std::optional<ProviderArtifactKind> provider_artifact_for_command(CommandKind command) {
    switch (command) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND(kind, token, position, inference_order,       \
                                                       artifact_kind)                              \
    case CommandKind::kind: return ProviderArtifactKind::artifact_kind;
#include "tooling/cli/provider/durable_store_import_provider_commands.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND
    default: return std::nullopt;
    }
}

std::string_view provider_artifact_command_token(ProviderArtifactKind kind) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token,                            \
                                                        visibility)                               \
    case ProviderArtifactKind::kind_name: return command_token;
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return "<unknown-provider-artifact>";
}

bool print_provider_artifact(ProviderArtifactKind kind,
                             const ProviderArtifact &artifact,
                             std::ostream &out) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token,                            \
                                                        visibility)                               \
    case ProviderArtifactKind::kind_name: {                                                       \
        const auto *typed = std::get_if<ahfl::durable_store_import::artifact_type>(&artifact);     \
        if (typed == nullptr) {                                                                   \
            return false;                                                                         \
        }                                                                                         \
        ahfl::printer(*typed, out);                                                               \
        return true;                                                                              \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return false;
}

int emit_provider_artifact_with_diagnostics(
    ProviderArtifactKind kind,
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

constexpr ProviderArtifactVisibility provider_artifact_visibility(ProviderArtifactKind kind) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name, artifact_type, builder, printer, command_token, visibility) \
    case ProviderArtifactKind::kind_name: return ProviderArtifactVisibility::visibility;
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }
    return ProviderArtifactVisibility::Internal;
}

} // namespace ahfl::cli
