#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace ahfl::durable_store_import {

enum class ProviderArtifactDomain {
    Binding,
    Runtime,
    HostExecution,
    Sdk,
    Configuration,
    Reliability,
    Governance,
    Production,
};

enum class ProviderArtifactRole {
    Checkpoint,
    Config,
    Contract,
    Decision,
    Event,
    Evidence,
    Manifest,
    Matrix,
    Placeholder,
    Plan,
    Policy,
    Preview,
    Profile,
    Readiness,
    Receipt,
    Record,
    Registry,
    Report,
    Request,
    Result,
    Review,
    Summary,
};

struct ProviderArtifactDescriptor {
    std::string_view artifact_id;
    std::string_view type_name;
    std::string_view format_version;
    std::string_view source_header;
    ProviderArtifactDomain domain;
    ProviderArtifactRole role;
};

[[nodiscard]] std::string_view provider_artifact_domain_name(ProviderArtifactDomain domain);

[[nodiscard]] std::string_view provider_artifact_role_name(ProviderArtifactRole role);

[[nodiscard]] std::span<const ProviderArtifactDescriptor> provider_artifacts();

[[nodiscard]] const ProviderArtifactDescriptor *
find_provider_artifact_by_id(std::string_view artifact_id);

[[nodiscard]] const ProviderArtifactDescriptor *
find_provider_artifact_by_format_version(std::string_view format_version);

[[nodiscard]] std::size_t provider_artifact_count(ProviderArtifactDomain domain);

} // namespace ahfl::durable_store_import
