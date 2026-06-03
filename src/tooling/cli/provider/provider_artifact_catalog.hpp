#pragma once

#include "tooling/cli/command_catalog.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

enum class ProviderArtifactKind {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind, command_kind, artifact_type, builder, printer, command_token, visibility, order)         \
    kind,
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

enum class ProviderArtifactVisibility {
    Public,
    Internal
};

[[nodiscard]] ProviderArtifactVisibility provider_artifact_visibility(ProviderArtifactKind kind);
[[nodiscard]] std::optional<ProviderArtifactKind>
provider_artifact_for_command(CommandKind command);
[[nodiscard]] std::optional<ProviderArtifactVisibility>
provider_artifact_visibility_for_command(CommandKind command);
[[nodiscard]] std::string_view provider_artifact_command_token(ProviderArtifactKind kind);

} // namespace ahfl::cli
