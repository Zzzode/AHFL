#pragma once

#include "compiler/manifest/manifest.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ahfl::package_graph {

struct PackageId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(PackageId lhs, PackageId rhs) noexcept = default;
};

struct TargetId {
    PackageId package;
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(TargetId lhs, TargetId rhs) noexcept = default;
};

struct ModuleId {
    PackageId package;
    std::string module_path;

    [[nodiscard]] friend bool operator==(const ModuleId &lhs, const ModuleId &rhs) = default;
};

enum class PackageSourceKind {
    Sysroot,
    Root,
    Workspace,
    Path,
};

struct Diagnostic {
    std::string code;
    std::string message;
    SourceRange range{};
};

struct PackageInput {
    manifest::PackageManifest manifest;
    std::filesystem::path package_root;
    PackageSourceKind source{PackageSourceKind::Path};
    std::filesystem::path manifest_path;
    std::string checksum;
};

struct BuildInput {
    PackageInput sysroot_std;
    PackageInput root_package;
    std::vector<PackageInput> workspace_packages;
    std::vector<PackageInput> path_packages;
};

struct ManifestBuildInput {
    std::filesystem::path root_manifest_path;
    std::filesystem::path sysroot_manifest_path;
};

struct WorkspaceBuildInput {
    std::filesystem::path workspace_manifest_path;
    std::string package_name;
    std::filesystem::path sysroot_manifest_path;
};

struct TargetNode {
    TargetId id;
    std::string name;
    std::string kind;
    std::string entry;
    std::vector<std::string> exports;
    std::vector<manifest::CapabilityBindingManifest> capability_bindings;
};

struct DependencyEdge {
    PackageId from;
    std::string dependency_key;
    PackageId to;
    std::string source;
};

struct PackageNode {
    PackageId id;
    PackageSourceKind source{PackageSourceKind::Path};
    std::string name;
    std::string version;
    std::string kind;
    std::string module_prefix;
    std::filesystem::path package_root;
    std::filesystem::path module_root;
    std::filesystem::path manifest_path;
    std::string checksum;
    std::vector<std::string> exported_modules;
    std::vector<TargetNode> targets;
};

struct ModuleRootEntry {
    std::string prefix;
    PackageId package;
    std::filesystem::path root;
};

struct PackageGraph {
    std::vector<PackageNode> packages;
    std::vector<DependencyEdge> dependencies;
    std::vector<ModuleRootEntry> module_roots;

    [[nodiscard]] const PackageNode *find_package(PackageId id) const;
    [[nodiscard]] std::optional<PackageId> package_by_name(std::string_view name) const;
};

struct BuildResult {
    std::optional<PackageGraph> graph;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] BuildResult build_package_graph(const BuildInput &input);
[[nodiscard]] BuildResult build_package_graph_from_manifests(const ManifestBuildInput &input);
[[nodiscard]] BuildResult build_package_graph_from_workspace(const WorkspaceBuildInput &input);
[[nodiscard]] std::string serialize_package_graph_json(const PackageGraph &graph);
[[nodiscard]] std::string_view source_kind_name(PackageSourceKind kind) noexcept;

} // namespace ahfl::package_graph
