#include "tooling/cli/provider/provider_artifact_catalog.hpp"

namespace ahfl::cli {

ProviderArtifactVisibility provider_artifact_visibility(ProviderArtifactKind kind) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case ProviderArtifactKind::kind_name:                                                          \
        return ProviderArtifactVisibility::visibility;
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return ProviderArtifactVisibility::Internal;
}

std::optional<ProviderArtifactKind> provider_artifact_for_command(CommandKind command) {
    switch (command) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case CommandKind::command_kind:                                                                \
        return ProviderArtifactKind::kind_name;
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    default:
        return std::nullopt;
    }
}

std::optional<ProviderArtifactVisibility>
provider_artifact_visibility_for_command(CommandKind command) {
    const auto artifact = provider_artifact_for_command(command);
    if (!artifact.has_value()) {
        return std::nullopt;
    }
    return provider_artifact_visibility(*artifact);
}

std::string_view provider_artifact_command_token(ProviderArtifactKind kind) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind_name, command_kind, artifact_type, builder, printer, command_token, visibility, order)    \
    case ProviderArtifactKind::kind_name:                                                          \
        return command_token;
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return "<unknown-provider-artifact>";
}

} // namespace ahfl::cli
