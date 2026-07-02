#pragma once

#include "ahfl/base/support/source.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::manifest {

struct ManifestDiagnostic {
    std::string code;
    std::string message;
    SourceRange range{};
};

struct DependencySpec {
    std::string key;
    std::string source;
    std::optional<std::string> path;
    std::optional<std::string> version;
    SourceRange range{};
};

struct CapabilityBindingManifest {
    std::string capability;
    std::string binding_key;
    SourceRange range{};
};

struct HandoffExportManifest {
    std::string kind{"workflow"};
    std::string name;
    SourceRange range{};
};

struct ExportedModuleManifest {
    std::string module_path;
    SourceRange range{};
};

struct TargetManifest {
    std::string name;
    std::string kind;
    std::string entry;
    std::vector<HandoffExportManifest> exports;
    std::vector<CapabilityBindingManifest> capability_bindings;
    SourceRange range{};
};

struct PackageManifest {
    int manifest_version{0};
    std::string package_name;
    std::string package_version;
    std::string edition;
    std::string package_kind;
    std::string module_prefix;
    std::string module_root;
    std::vector<ExportedModuleManifest> exported_modules;
    std::optional<std::string> prelude_module;
    std::optional<std::string> prelude_injection;
    std::vector<std::string> compiler_intrinsics_allow;
    std::vector<TargetManifest> targets;
    std::vector<DependencySpec> dependencies;
};

struct WorkspaceManifest {
    int manifest_version{0};
    std::string workspace_name;
    std::vector<std::string> members;
    int resolver_version{0};
    std::vector<DependencySpec> dependencies;
};

template <typename T> struct ManifestResult {
    std::optional<T> manifest;
    std::vector<ManifestDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] ManifestResult<PackageManifest> parse_package_manifest(std::string_view input);
[[nodiscard]] ManifestResult<WorkspaceManifest> parse_workspace_manifest(std::string_view input);
[[nodiscard]] std::string canonicalize_package_manifest(const PackageManifest &manifest);
[[nodiscard]] std::string canonicalize_workspace_manifest(const WorkspaceManifest &manifest);

} // namespace ahfl::manifest
