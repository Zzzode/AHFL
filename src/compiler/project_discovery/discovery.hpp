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

enum class ToolchainProfileOrigin {
    CliFlag,
    Environment,
    CompileDefault,
    LspConfiguration,
    LspInitialization,
    BundledExtension,
};

enum class ToolchainProfileScope {
    GlobalDefault,
    WorkspaceFolder,
};

enum class ToolchainServerCompatibility {
    SameBuild,
    Compatible,
    Incompatible,
};

struct ToolchainProfile {
    std::filesystem::path sysroot_root;
    std::filesystem::path std_manifest;
    ToolchainProfileOrigin origin{ToolchainProfileOrigin::CompileDefault};
    ToolchainProfileScope scope{ToolchainProfileScope::GlobalDefault};
    std::string std_identity;
    ToolchainServerCompatibility server_compatibility{ToolchainServerCompatibility::SameBuild};
};

struct WorkspaceToolchainProfile {
    std::filesystem::path workspace_root;
    ToolchainProfile profile;
};

struct ToolchainProfileSet {
    std::optional<ToolchainProfile> default_profile;
    std::vector<WorkspaceToolchainProfile> workspace_profiles;
    std::vector<package_graph::Diagnostic> diagnostics;
};

struct ToolchainProfileResult {
    std::optional<ToolchainProfile> profile;
    std::vector<package_graph::Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return !diagnostics.empty();
    }
};

struct ToolchainProfileSelection {
    ToolchainProfile profile;
    std::optional<std::filesystem::path> workspace_root;
};

struct ProjectDiscoveryInput {
    std::filesystem::path document_path;
    std::vector<WorkspaceBoundary> workspace_boundaries;
    std::optional<std::filesystem::path> explicit_manifest_path;
    std::optional<std::filesystem::path> explicit_workspace_manifest_path;
    ToolchainProfileSet toolchains;
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
[[nodiscard]] std::optional<std::filesystem::path>
find_package_manifest_for_document(const std::filesystem::path &document_path,
                                   const std::vector<WorkspaceBoundary> &workspace_boundaries);
[[nodiscard]] std::optional<ToolchainProfileSelection>
select_toolchain_profile_for_document(const ToolchainProfileSet &toolchains,
                                      const std::filesystem::path &document_path);
[[nodiscard]] ToolchainProfileResult
toolchain_profile_from_sysroot_input(const std::filesystem::path &path,
                                     ToolchainProfileOrigin origin);
[[nodiscard]] std::optional<ToolchainProfile> default_toolchain_profile_from_compile_default();

} // namespace ahfl::project_discovery
