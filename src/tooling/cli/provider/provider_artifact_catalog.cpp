#include "tooling/cli/provider/provider_artifact_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ahfl::cli {
namespace {

using Kind = ProviderArtifactKind;

constexpr std::array kProviderArtifacts{
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    ProviderArtifactDescriptor{                                                                    \
        ProviderArtifactKind::kind_name,                                                           \
        artifact_id,                                                                               \
        ProviderArtifactVisibility::visibility,                                                    \
        order,                                                                                     \
    },
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

[[nodiscard]] consteval bool provider_artifact_orders_are_unique() {
    for (std::size_t lhs = 0; lhs < kProviderArtifacts.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < kProviderArtifacts.size(); ++rhs) {
            if (kProviderArtifacts[lhs].order == kProviderArtifacts[rhs].order) {
                return false;
            }
        }
    }
    return true;
}

static_assert(provider_artifact_orders_are_unique(),
              "duplicate provider artifact order in descriptor registry");

constexpr std::array<Kind, 0> kNoDependencies{};

} // namespace

std::span<const ProviderArtifactDescriptor> provider_artifact_descriptors() {
    return kProviderArtifacts;
}

const ProviderArtifactDescriptor *provider_artifact_descriptor(ProviderArtifactKind kind) {
    const auto artifacts = provider_artifact_descriptors();
    const auto match =
        std::find_if(artifacts.begin(), artifacts.end(), [kind](const auto &descriptor) {
            return descriptor.kind == kind;
        });
    return match == artifacts.end() ? nullptr : &*match;
}

std::span<const ProviderArtifactKind> provider_artifact_dependencies(ProviderArtifactKind kind) {
    switch (kind) {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind_name,                                 \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    case Kind::kind_name: {                                                                        \
        static constexpr std::array<Kind, dep_count> deps dependencies;                            \
        return deps;                                                                               \
    }
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    }

    return kNoDependencies;
}

std::optional<ProviderArtifactNode> provider_artifact_node(ProviderArtifactKind kind) {
    const auto *descriptor = provider_artifact_descriptor(kind);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    return ProviderArtifactNode{
        .descriptor = *descriptor,
        .dependencies = provider_artifact_dependencies(kind),
    };
}

bool provider_artifact_has_node(ProviderArtifactKind kind) {
    return provider_artifact_node(kind).has_value();
}

ProviderArtifactVisibility provider_artifact_visibility(ProviderArtifactKind kind) {
    if (const auto *descriptor = provider_artifact_descriptor(kind); descriptor != nullptr) {
        return descriptor->visibility;
    }
    return ProviderArtifactVisibility::Internal;
}

std::string_view provider_artifact_id(ProviderArtifactKind kind) {
    if (const auto *descriptor = provider_artifact_descriptor(kind); descriptor != nullptr) {
        return descriptor->artifact_id;
    }
    return "<unknown-provider-artifact>";
}

std::string provider_artifact_cli_id(ProviderArtifactKind kind) {
    std::string result = "provider/";
    result += provider_artifact_id(kind);
    return result;
}

std::string provider_artifact_command_name(ProviderArtifactKind kind) {
    std::string result = "emit ";
    result += provider_artifact_cli_id(kind);
    return result;
}

std::optional<ProviderArtifactKind> resolve_provider_artifact(std::string_view cli_artifact_id,
                                                              bool include_hidden) {
    constexpr std::string_view kProviderPrefix = "provider/";
    if (!cli_artifact_id.starts_with(kProviderPrefix)) {
        return std::nullopt;
    }

    const auto local_id = cli_artifact_id.substr(kProviderPrefix.size());
    const auto artifacts = provider_artifact_descriptors();
    const auto match =
        std::find_if(artifacts.begin(), artifacts.end(), [local_id](const auto &descriptor) {
            return descriptor.artifact_id == local_id;
        });
    if (match == artifacts.end()) {
        return std::nullopt;
    }
    if (!include_hidden && match->visibility == ProviderArtifactVisibility::Internal) {
        return std::nullopt;
    }
    return match->kind;
}

} // namespace ahfl::cli
