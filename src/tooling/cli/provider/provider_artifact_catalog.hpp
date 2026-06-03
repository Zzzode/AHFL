#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ahfl::cli {

enum class ProviderArtifactKind {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    kind,
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

enum class ProviderArtifactVisibility {
    Public,
    Internal
};

struct ProviderArtifactDescriptor {
    ProviderArtifactKind kind;
    std::string_view artifact_id;
    ProviderArtifactVisibility visibility;
    int order;
};

struct ProviderArtifactNode {
    ProviderArtifactDescriptor descriptor;
    std::span<const ProviderArtifactKind> dependencies;
};

[[nodiscard]] std::span<const ProviderArtifactDescriptor> provider_artifact_descriptors();
[[nodiscard]] const ProviderArtifactDescriptor *
provider_artifact_descriptor(ProviderArtifactKind kind);
[[nodiscard]] std::optional<ProviderArtifactNode> provider_artifact_node(ProviderArtifactKind kind);
[[nodiscard]] bool provider_artifact_has_node(ProviderArtifactKind kind);
[[nodiscard]] std::span<const ProviderArtifactKind>
provider_artifact_dependencies(ProviderArtifactKind kind);
[[nodiscard]] ProviderArtifactVisibility provider_artifact_visibility(ProviderArtifactKind kind);
[[nodiscard]] std::string_view provider_artifact_id(ProviderArtifactKind kind);
[[nodiscard]] std::string provider_artifact_cli_id(ProviderArtifactKind kind);
[[nodiscard]] std::string provider_artifact_command_name(ProviderArtifactKind kind);
[[nodiscard]] std::optional<ProviderArtifactKind>
resolve_provider_artifact(std::string_view cli_artifact_id, bool include_hidden);

} // namespace ahfl::cli
