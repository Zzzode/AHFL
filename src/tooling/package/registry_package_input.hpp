#pragma once

#include "compiler/manifest/manifest.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "tooling/package/registry.hpp"
#include "tooling/package/source_archive.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::package {

struct RegistryPackageInputMaterialization {
    RegistryIndexEntry registry;
    SourceArchive archive;
    std::string_view payload;
    std::filesystem::path output_root;
};

struct RegistryPackageInputResult {
    std::optional<package_graph::PackageInput> package;
    std::vector<std::string> materialized_files;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] RegistryPackageInputResult
materialize_registry_package_input(const RegistryPackageInputMaterialization &input);

struct RegistryPackageInputResolution {
    const manifest::PackageManifest *root_manifest{nullptr};
    const Registry *registry{nullptr};
    std::filesystem::path materialization_root;
};

struct RegistryPackageInputResolutionResult {
    std::vector<package_graph::PackageInput> packages;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] RegistryPackageInputResolutionResult
resolve_registry_package_inputs(const RegistryPackageInputResolution &input);

} // namespace ahfl::package
