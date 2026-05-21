#include "ahfl/durable_store_import/provider.hpp"

#include <algorithm>

namespace ahfl::durable_store_import {

namespace {

constexpr ProviderArtifactDescriptor kProviderArtifacts[] = {
#define AHFL_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                               \
    artifact_id, type_name, format_version, domain, role, source_header)                           \
    {artifact_id, #type_name, format_version, source_header, domain, role},
#include "ahfl/durable_store_import/provider_artifacts.def"
#undef AHFL_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

} // namespace

std::string_view provider_artifact_domain_name(ProviderArtifactDomain domain) {
    switch (domain) {
    case ProviderArtifactDomain::Binding:
        return "binding";
    case ProviderArtifactDomain::Runtime:
        return "runtime";
    case ProviderArtifactDomain::HostExecution:
        return "host_execution";
    case ProviderArtifactDomain::Sdk:
        return "sdk";
    case ProviderArtifactDomain::Configuration:
        return "configuration";
    case ProviderArtifactDomain::Reliability:
        return "reliability";
    case ProviderArtifactDomain::Governance:
        return "governance";
    case ProviderArtifactDomain::Production:
        return "production";
    }
    return "unknown";
}

std::string_view provider_artifact_role_name(ProviderArtifactRole role) {
    switch (role) {
    case ProviderArtifactRole::Checkpoint:
        return "checkpoint";
    case ProviderArtifactRole::Config:
        return "config";
    case ProviderArtifactRole::Contract:
        return "contract";
    case ProviderArtifactRole::Decision:
        return "decision";
    case ProviderArtifactRole::Event:
        return "event";
    case ProviderArtifactRole::Evidence:
        return "evidence";
    case ProviderArtifactRole::Manifest:
        return "manifest";
    case ProviderArtifactRole::Matrix:
        return "matrix";
    case ProviderArtifactRole::Placeholder:
        return "placeholder";
    case ProviderArtifactRole::Plan:
        return "plan";
    case ProviderArtifactRole::Policy:
        return "policy";
    case ProviderArtifactRole::Preview:
        return "preview";
    case ProviderArtifactRole::Profile:
        return "profile";
    case ProviderArtifactRole::Readiness:
        return "readiness";
    case ProviderArtifactRole::Receipt:
        return "receipt";
    case ProviderArtifactRole::Record:
        return "record";
    case ProviderArtifactRole::Registry:
        return "registry";
    case ProviderArtifactRole::Report:
        return "report";
    case ProviderArtifactRole::Request:
        return "request";
    case ProviderArtifactRole::Result:
        return "result";
    case ProviderArtifactRole::Review:
        return "review";
    case ProviderArtifactRole::Summary:
        return "summary";
    }
    return "unknown";
}

std::span<const ProviderArtifactDescriptor> provider_artifacts() {
    return std::span<const ProviderArtifactDescriptor>{kProviderArtifacts};
}

const ProviderArtifactDescriptor *find_provider_artifact_by_id(std::string_view artifact_id) {
    const auto artifacts = provider_artifacts();
    const auto it = std::find_if(artifacts.begin(), artifacts.end(), [&](const auto &artifact) {
        return artifact.artifact_id == artifact_id;
    });
    return it == artifacts.end() ? nullptr : &*it;
}

const ProviderArtifactDescriptor *
find_provider_artifact_by_format_version(std::string_view format_version) {
    const auto artifacts = provider_artifacts();
    const auto it = std::find_if(artifacts.begin(), artifacts.end(), [&](const auto &artifact) {
        return artifact.format_version == format_version;
    });
    return it == artifacts.end() ? nullptr : &*it;
}

std::size_t provider_artifact_count(ProviderArtifactDomain domain) {
    const auto artifacts = provider_artifacts();
    return static_cast<std::size_t>(
        std::count_if(artifacts.begin(), artifacts.end(), [&](const auto &artifact) {
            return artifact.domain == domain;
        }));
}

} // namespace ahfl::durable_store_import
