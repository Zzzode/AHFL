#pragma once

#include "compiler/package_graph/package_graph.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ahfl::project_discovery {

struct WorkspaceBoundary {
    std::filesystem::path root;
};

enum class ProjectContextKind {
    Package,
    WorkspaceMember,
    SysrootPackage,
};

struct ProjectDiscoveryInput {
    std::filesystem::path document_path;
    std::vector<WorkspaceBoundary> workspace_boundaries;
    std::optional<std::filesystem::path> explicit_manifest_path;
    std::optional<std::filesystem::path> explicit_workspace_manifest_path;
    std::optional<std::filesystem::path> explicit_sysroot_path;
};

struct ProjectContext {
    ProjectContextKind kind{ProjectContextKind::Package};
    package_graph::PackageGraph graph;
    std::filesystem::path graph_manifest_path;
    std::filesystem::path package_manifest_path;
    std::optional<std::filesystem::path> workspace_manifest_path;
    std::filesystem::path sysroot_manifest_path;
};

struct ProjectDiscoveryResult {
    std::optional<ProjectContext> context;
    std::vector<package_graph::Diagnostic> diagnostics;
    bool project_manifest_found{false};

    [[nodiscard]] bool has_errors() const noexcept {
        return !diagnostics.empty();
    }
};

[[nodiscard]] ProjectDiscoveryResult discover_project_context(const ProjectDiscoveryInput &input);
[[nodiscard]] std::filesystem::path normalize_project_path(const std::filesystem::path &path);
[[nodiscard]] std::optional<std::filesystem::path> default_sysroot_manifest_from_environment();

} // namespace ahfl::project_discovery
