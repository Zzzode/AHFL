#include "tooling/package/registry_package_input.hpp"

#include "compiler/manifest/manifest.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>

namespace ahfl::package {
namespace {

[[nodiscard]] bool registry_entry_is_valid(const RegistryIndexEntry &entry,
                                           std::vector<std::string> &diagnostics) {
    auto parsed = parse_registry_index_entry(serialize_registry_index_entry(entry));
    if (parsed.has_errors()) {
        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path &path,
                                                        std::vector<std::string> &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back("failed to read materialized registry manifest: " +
                              path.generic_string());
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

[[nodiscard]] bool dependency_matches(const RegistryDependencyMetadata &metadata,
                                      const ahfl::manifest::DependencySpec &dependency) {
    if (metadata.name != dependency.key || metadata.source != dependency.source) {
        return false;
    }
    if (metadata.registry != dependency.registry) {
        return false;
    }
    return metadata.version_requirement == dependency.version;
}

[[nodiscard]] std::string default_registry_id(const std::optional<std::string> &registry) {
    return registry.value_or("default");
}

[[nodiscard]] std::string materialization_directory_name(const RegistryIndexEntry &entry) {
    constexpr std::string_view prefix = "sha256:";
    auto digest = std::string_view{entry.source_archive_sha256};
    if (digest.starts_with(prefix)) {
        digest.remove_prefix(prefix.size());
    }
    const auto short_digest = digest.substr(0, std::min<std::size_t>(digest.size(), 12));
    return entry.package + "-" + entry.version + "-" + std::string{short_digest};
}

void validate_manifest_matches_registry(const RegistryIndexEntry &registry,
                                        const ahfl::manifest::PackageManifest &manifest,
                                        std::vector<std::string> &diagnostics) {
    if (manifest.package_name != registry.package) {
        diagnostics.push_back("registry package manifest name '" + manifest.package_name +
                              "' does not match registry package '" + registry.package + "'");
    }
    if (manifest.package_version != registry.version) {
        diagnostics.push_back("registry package manifest version '" + manifest.package_version +
                              "' does not match registry version '" + registry.version + "'");
    }
    for (const auto &dependency : manifest.dependencies) {
        if (dependency.source != "sysroot" && dependency.source != "registry") {
            diagnostics.push_back("registry package manifest contains local dependency '" +
                                  dependency.key + "' with source '" + dependency.source + "'");
        }
    }
    if (manifest.dependencies.size() != registry.dependencies.size()) {
        diagnostics.push_back(
            "registry dependency metadata does not match manifest dependency count");
        return;
    }
    for (const auto &dependency : manifest.dependencies) {
        const auto found =
            std::find_if(registry.dependencies.begin(),
                         registry.dependencies.end(),
                         [&](const auto &metadata) { return metadata.name == dependency.key; });
        if (found == registry.dependencies.end()) {
            diagnostics.push_back("registry dependency metadata is missing manifest dependency '" +
                                  dependency.key + "'");
            continue;
        }
        if (!dependency_matches(*found, dependency)) {
            diagnostics.push_back("registry dependency metadata for '" + dependency.key +
                                  "' does not match manifest dependency declaration");
        }
    }
}

} // namespace

RegistryPackageInputResult
materialize_registry_package_input(const RegistryPackageInputMaterialization &input) {
    RegistryPackageInputResult result;

    if (!registry_entry_is_valid(input.registry, result.diagnostics)) {
        return result;
    }
    if (input.archive.package_name != input.registry.package) {
        result.diagnostics.push_back("source archive package '" + input.archive.package_name +
                                     "' does not match registry package '" +
                                     input.registry.package + "'");
    }
    if (input.archive.archive_sha256 != input.registry.source_archive_sha256) {
        result.diagnostics.push_back(
            "source archive digest does not match registry source_archive_sha256");
    }
    if (input.archive.manifest_sha256 != input.registry.manifest_sha256) {
        result.diagnostics.push_back(
            "source archive manifest digest does not match registry manifest_sha256");
    }
    if (result.has_errors()) {
        return result;
    }

    auto materialized = materialize_source_archive(input.archive, input.payload, input.output_root);
    result.materialized_files = std::move(materialized.files);
    if (materialized.has_errors() || !materialized.package_root.has_value()) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  materialized.diagnostics.begin(),
                                  materialized.diagnostics.end());
        return result;
    }

    const auto manifest_path = *materialized.package_root / "ahfl.toml";
    auto manifest_text = read_text_file(manifest_path, result.diagnostics);
    if (!manifest_text.has_value()) {
        return result;
    }

    auto parsed_manifest = ahfl::manifest::parse_package_manifest(*manifest_text);
    if (parsed_manifest.has_errors() || !parsed_manifest.manifest.has_value()) {
        for (const auto &diagnostic : parsed_manifest.diagnostics) {
            result.diagnostics.push_back("registry package manifest parse error: " +
                                         diagnostic.message);
        }
        return result;
    }

    validate_manifest_matches_registry(
        input.registry, *parsed_manifest.manifest, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.package = package_graph::PackageInput{
        .manifest = std::move(*parsed_manifest.manifest),
        .package_root = *materialized.package_root,
        .source = package_graph::PackageSourceKind::Registry,
        .manifest_path = manifest_path,
        .checksum = input.archive.archive_sha256,
        .registry =
            package_graph::RegistryPackageIdentity{
                .registry_id = input.registry.registry_id,
                .source_archive_sha256 = input.registry.source_archive_sha256,
                .manifest_sha256 = input.registry.manifest_sha256,
                .public_api_sha256 = input.registry.public_api_sha256,
            },
    };
    return result;
}

namespace {

struct RegistryDependencyRequest {
    std::string name;
    std::string registry_id;
    std::string version_requirement;
};

class RegistryPackageInputResolver {
  public:
    explicit RegistryPackageInputResolver(const RegistryPackageInputResolution &input)
        : input_(input) {}

    [[nodiscard]] RegistryPackageInputResolutionResult run() {
        RegistryPackageInputResolutionResult result;
        if (input_.root_manifest == nullptr) {
            result.diagnostics.push_back("registry package resolution requires root manifest");
            return result;
        }
        if (input_.registry == nullptr) {
            result.diagnostics.push_back("registry package resolution requires registry client");
            return result;
        }

        for (const auto &dependency : input_.root_manifest->dependencies) {
            if (dependency.source != "registry") {
                continue;
            }
            if (!dependency.version.has_value()) {
                result.diagnostics.push_back("registry dependency '" + dependency.key +
                                             "' is missing version requirement");
                continue;
            }
            resolve(
                RegistryDependencyRequest{
                    .name = dependency.key,
                    .registry_id = default_registry_id(dependency.registry),
                    .version_requirement = *dependency.version,
                },
                result);
        }
        return result;
    }

  private:
    void resolve(const RegistryDependencyRequest &request,
                 RegistryPackageInputResolutionResult &result) {
        const auto index = input_.registry->fetch_package_index(request.name);
        if (!index.success() || !index.index.has_value()) {
            append_diagnostics(
                "registry package index '" + request.name + "'", index.diagnostics, result);
            return;
        }

        std::vector<RegistryIndexEntry> candidates;
        for (const auto &entry : index.index->versions) {
            if (entry.registry_id == request.registry_id) {
                candidates.push_back(entry);
            }
        }
        if (candidates.empty()) {
            result.diagnostics.push_back("registry dependency '" + request.name +
                                         "' has no candidates in registry '" + request.registry_id +
                                         "'");
            return;
        }

        const auto selection =
            select_registry_candidate(request.name, candidates, request.version_requirement);
        if (!selection.entry.has_value()) {
            result.diagnostics.push_back(selection.error_message);
            return;
        }

        const auto existing = selected_versions_.find(selection.entry->package);
        if (existing != selected_versions_.end()) {
            if (existing->second != selection.entry->version) {
                result.diagnostics.push_back("registry dependency '" + selection.entry->package +
                                             "' resolves to both " + existing->second + " and " +
                                             selection.entry->version);
            }
            return;
        }

        const auto artifacts = input_.registry->fetch_package_artifacts(selection.entry->package,
                                                                        selection.entry->version);
        if (!artifacts.success() || !artifacts.artifacts.has_value()) {
            append_diagnostics("registry package artifacts '" + selection.entry->package + "@" +
                                   selection.entry->version + "'",
                               artifacts.diagnostics,
                               result);
            return;
        }
        if (!same_identity(*selection.entry, artifacts.artifacts->registry)) {
            result.diagnostics.push_back("registry package artifacts for '" +
                                         selection.entry->package + "@" + selection.entry->version +
                                         "' do not match selected registry index entry");
            return;
        }

        auto materialized = materialize_registry_package_input(RegistryPackageInputMaterialization{
            .registry = artifacts.artifacts->registry,
            .archive = artifacts.artifacts->archive,
            .payload = artifacts.artifacts->payload,
            .output_root = input_.materialization_root /
                           materialization_directory_name(artifacts.artifacts->registry),
        });
        if (materialized.has_errors() || !materialized.package.has_value()) {
            append_diagnostics("registry package input '" + selection.entry->package + "@" +
                                   selection.entry->version + "'",
                               materialized.diagnostics,
                               result);
            return;
        }

        selected_versions_[selection.entry->package] = selection.entry->version;
        result.packages.push_back(std::move(*materialized.package));

        for (const auto &dependency : artifacts.artifacts->registry.dependencies) {
            if (dependency.source != "registry") {
                continue;
            }
            if (!dependency.version_requirement.has_value()) {
                result.diagnostics.push_back("registry package '" + selection.entry->package +
                                             "' dependency '" + dependency.name +
                                             "' is missing version requirement");
                continue;
            }
            resolve(
                RegistryDependencyRequest{
                    .name = dependency.name,
                    .registry_id = default_registry_id(dependency.registry),
                    .version_requirement = *dependency.version_requirement,
                },
                result);
        }
    }

    static void append_diagnostics(std::string context,
                                   const std::vector<std::string> &diagnostics,
                                   RegistryPackageInputResolutionResult &result) {
        if (diagnostics.empty()) {
            result.diagnostics.push_back(context + " unavailable");
            return;
        }
        for (const auto &diagnostic : diagnostics) {
            result.diagnostics.push_back(context + ": " + diagnostic);
        }
    }

    [[nodiscard]] static bool same_identity(const RegistryIndexEntry &lhs,
                                            const RegistryIndexEntry &rhs) {
        return lhs.registry_id == rhs.registry_id && lhs.package == rhs.package &&
               lhs.version == rhs.version &&
               lhs.source_archive_sha256 == rhs.source_archive_sha256 &&
               lhs.manifest_sha256 == rhs.manifest_sha256 &&
               lhs.public_api_sha256 == rhs.public_api_sha256;
    }

    const RegistryPackageInputResolution &input_;
    std::map<std::string, std::string> selected_versions_;
};

} // namespace

RegistryPackageInputResolutionResult
resolve_registry_package_inputs(const RegistryPackageInputResolution &input) {
    return RegistryPackageInputResolver(input).run();
}

} // namespace ahfl::package
