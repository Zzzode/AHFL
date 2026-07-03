#include "compiler/project_discovery/discovery.hpp"

#include "compiler/manifest/manifest.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace ahfl::project_discovery {
namespace {

constexpr std::string_view kProjectDiscovery = "E::project_discovery";

void add_error(std::vector<package_graph::Diagnostic> &diagnostics,
               std::string message,
               SourceRange range = {}) {
    diagnostics.push_back(package_graph::Diagnostic{
        .code = std::string{kProjectDiscovery},
        .message = std::move(message),
        .range = range,
    });
}

void append_manifest_diagnostics(std::vector<package_graph::Diagnostic> &target,
                                 const std::vector<manifest::ManifestDiagnostic> &source) {
    target.reserve(target.size() + source.size());
    for (const auto &diagnostic : source) {
        target.push_back(package_graph::Diagnostic{
            .code = diagnostic.code,
            .message = diagnostic.message,
            .range = diagnostic.range,
        });
    }
}

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  std::string &content,
                                  std::vector<package_graph::Diagnostic> &diagnostics,
                                  std::string_view role) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        add_error(diagnostics,
                  "failed to open " + std::string{role} + " '" + path.generic_string() + "'");
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

[[nodiscard]] bool path_is_equal_or_descendant(const std::filesystem::path &path,
                                               const std::filesystem::path &ancestor) {
    auto path_part = path.begin();
    for (auto ancestor_part = ancestor.begin(); ancestor_part != ancestor.end(); ++ancestor_part) {
        if (path_part == path.end() || *path_part != *ancestor_part) {
            return false;
        }
        ++path_part;
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path>
containing_workspace_boundary(const std::filesystem::path &document_path,
                              const std::vector<WorkspaceBoundary> &boundaries) {
    std::optional<std::filesystem::path> best;
    for (const auto &boundary : boundaries) {
        if (boundary.root.empty()) {
            continue;
        }
        const auto normalized = normalize_project_path(boundary.root);
        if (!path_is_equal_or_descendant(document_path, normalized)) {
            continue;
        }
        if (!best.has_value() ||
            normalized.generic_string().size() > best->generic_string().size()) {
            best = normalized;
        }
    }
    return best;
}

[[nodiscard]] std::optional<std::filesystem::path>
find_nearest_named_file_bounded(const std::filesystem::path &start,
                                std::string_view filename,
                                const std::optional<std::filesystem::path> &boundary) {
    if (start.empty()) {
        return std::nullopt;
    }

    auto current = normalize_project_path(start);
    while (!current.empty()) {
        if (boundary.has_value() && !path_is_equal_or_descendant(current, *boundary)) {
            break;
        }

        const auto candidate = current / std::string{filename};
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return normalize_project_path(candidate);
        }

        if (boundary.has_value() && current == *boundary) {
            break;
        }
        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

[[nodiscard]] std::filesystem::path sysroot_manifest_from_path(const std::filesystem::path &path) {
    const auto normalized = normalize_project_path(path);
    if (normalized.filename() == "ahfl.toml") {
        return normalized;
    }
    return normalize_project_path(normalized / "std" / "ahfl.toml");
}

[[nodiscard]] std::optional<std::filesystem::path>
sysroot_manifest_for_discovery(const ProjectDiscoveryInput &input) {
    if (input.explicit_sysroot_path.has_value()) {
        return sysroot_manifest_from_path(*input.explicit_sysroot_path);
    }
    return default_sysroot_manifest_from_environment();
}

[[nodiscard]] std::optional<manifest::PackageManifest>
load_package_manifest(const std::filesystem::path &manifest_path,
                      std::vector<package_graph::Diagnostic> &diagnostics) {
    std::string text;
    const auto normalized = normalize_project_path(manifest_path);
    if (!read_text_file(normalized, text, diagnostics, "package manifest")) {
        return std::nullopt;
    }

    auto parsed = manifest::parse_package_manifest(text);
    append_manifest_diagnostics(diagnostics, parsed.diagnostics);
    if (!parsed.manifest.has_value()) {
        return std::nullopt;
    }
    return std::move(*parsed.manifest);
}

[[nodiscard]] std::optional<manifest::WorkspaceManifest>
load_workspace_manifest(const std::filesystem::path &manifest_path,
                        std::vector<package_graph::Diagnostic> &diagnostics) {
    std::string text;
    const auto normalized = normalize_project_path(manifest_path);
    if (!read_text_file(normalized, text, diagnostics, "workspace manifest")) {
        return std::nullopt;
    }

    auto parsed = manifest::parse_workspace_manifest(text);
    append_manifest_diagnostics(diagnostics, parsed.diagnostics);
    if (!parsed.manifest.has_value()) {
        return std::nullopt;
    }
    return std::move(*parsed.manifest);
}

[[nodiscard]] bool
workspace_contains_package_manifest(const manifest::WorkspaceManifest &workspace,
                                    const std::filesystem::path &workspace_manifest_path,
                                    const std::filesystem::path &package_manifest_path) {
    const auto workspace_root = normalize_project_path(workspace_manifest_path.parent_path());
    const auto package_manifest = normalize_project_path(package_manifest_path);
    for (const auto &member : workspace.members) {
        const auto member_manifest = normalize_project_path(workspace_root / member / "ahfl.toml");
        if (member_manifest == package_manifest) {
            return true;
        }
    }
    return false;
}

void append_package_graph_diagnostics(std::vector<package_graph::Diagnostic> &target,
                                      std::vector<package_graph::Diagnostic> source) {
    target.reserve(target.size() + source.size());
    for (auto &diagnostic : source) {
        target.push_back(std::move(diagnostic));
    }
}

[[nodiscard]] ProjectDiscoveryResult
build_sysroot_context(const std::filesystem::path &sysroot_manifest_path) {
    ProjectDiscoveryResult discovery;
    discovery.project_manifest_found = true;

    auto result = package_graph::build_package_graph_from_sysroot(package_graph::SysrootBuildInput{
        .sysroot_manifest_path = sysroot_manifest_path,
    });
    if (result.has_errors() || !result.graph.has_value()) {
        append_package_graph_diagnostics(discovery.diagnostics, std::move(result.diagnostics));
        return discovery;
    }

    auto manifest = normalize_project_path(sysroot_manifest_path);
    discovery.context = ProjectContext{
        .kind = ProjectContextKind::SysrootPackage,
        .graph = std::move(*result.graph),
        .graph_manifest_path = manifest,
        .package_manifest_path = manifest,
        .sysroot_manifest_path = manifest,
    };
    return discovery;
}

[[nodiscard]] ProjectDiscoveryResult
build_manifest_context(const std::filesystem::path &package_manifest_path,
                       const std::filesystem::path &sysroot_manifest_path) {
    ProjectDiscoveryResult discovery;
    discovery.project_manifest_found = true;

    auto result =
        package_graph::build_package_graph_from_manifests(package_graph::ManifestBuildInput{
            .root_manifest_path = package_manifest_path,
            .sysroot_manifest_path = sysroot_manifest_path,
        });
    if (result.has_errors() || !result.graph.has_value()) {
        append_package_graph_diagnostics(discovery.diagnostics, std::move(result.diagnostics));
        return discovery;
    }

    discovery.context = ProjectContext{
        .kind = ProjectContextKind::Package,
        .graph = std::move(*result.graph),
        .graph_manifest_path = normalize_project_path(package_manifest_path),
        .package_manifest_path = normalize_project_path(package_manifest_path),
        .sysroot_manifest_path = normalize_project_path(sysroot_manifest_path),
    };
    return discovery;
}

[[nodiscard]] ProjectDiscoveryResult
build_workspace_context(const std::filesystem::path &workspace_manifest_path,
                        const std::filesystem::path &package_manifest_path,
                        manifest::PackageManifest package_manifest,
                        const std::filesystem::path &sysroot_manifest_path) {
    ProjectDiscoveryResult discovery;
    discovery.project_manifest_found = true;

    auto result =
        package_graph::build_package_graph_from_workspace(package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_manifest_path,
            .package_name = package_manifest.package_name,
            .sysroot_manifest_path = sysroot_manifest_path,
        });
    if (result.has_errors() || !result.graph.has_value()) {
        append_package_graph_diagnostics(discovery.diagnostics, std::move(result.diagnostics));
        return discovery;
    }

    discovery.context = ProjectContext{
        .kind = ProjectContextKind::WorkspaceMember,
        .graph = std::move(*result.graph),
        .graph_manifest_path = normalize_project_path(workspace_manifest_path),
        .package_manifest_path = normalize_project_path(package_manifest_path),
        .workspace_manifest_path = normalize_project_path(workspace_manifest_path),
        .sysroot_manifest_path = normalize_project_path(sysroot_manifest_path),
    };
    return discovery;
}

} // namespace

std::filesystem::path normalize_project_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

std::optional<std::filesystem::path> default_sysroot_manifest_from_environment() {
    if (const char *env_root = std::getenv("AHFL_SYSROOT");
        env_root != nullptr && *env_root != '\0') {
        return sysroot_manifest_from_path(env_root);
    }

#ifdef AHFL_DEFAULT_SYSROOT
    constexpr std::string_view kDefaultSysroot = AHFL_DEFAULT_SYSROOT;
    if constexpr (!kDefaultSysroot.empty()) {
        return sysroot_manifest_from_path(std::filesystem::path{std::string{kDefaultSysroot}});
    }
#endif

    return std::nullopt;
}

ProjectDiscoveryResult discover_project_context(const ProjectDiscoveryInput &input) {
    ProjectDiscoveryResult discovery;
    const auto document_path = normalize_project_path(input.document_path);
    const auto boundary = containing_workspace_boundary(document_path, input.workspace_boundaries);
    const auto start =
        std::filesystem::is_directory(document_path) ? document_path : document_path.parent_path();

    const auto package_manifest_path =
        input.explicit_manifest_path.has_value()
            ? std::optional<std::filesystem::path>(
                  normalize_project_path(*input.explicit_manifest_path))
            : find_nearest_named_file_bounded(start, "ahfl.toml", boundary);
    if (!package_manifest_path.has_value()) {
        return discovery;
    }
    discovery.project_manifest_found = true;

    const auto sysroot_manifest = sysroot_manifest_for_discovery(input);
    if (!sysroot_manifest.has_value()) {
        add_error(discovery.diagnostics,
                  "failed to locate sysroot std/ahfl.toml; configure ahfl.sysroot or "
                  "AHFL_SYSROOT");
        return discovery;
    }

    const auto normalized_package_manifest = normalize_project_path(*package_manifest_path);
    const auto normalized_sysroot_manifest = normalize_project_path(*sysroot_manifest);
    if (normalized_package_manifest == normalized_sysroot_manifest) {
        return build_sysroot_context(normalized_sysroot_manifest);
    }

    auto package_manifest =
        load_package_manifest(normalized_package_manifest, discovery.diagnostics);
    if (!package_manifest.has_value()) {
        return discovery;
    }

    auto workspace_manifest_path =
        input.explicit_workspace_manifest_path.has_value()
            ? std::optional<std::filesystem::path>(
                  normalize_project_path(*input.explicit_workspace_manifest_path))
            : find_nearest_named_file_bounded(
                  normalized_package_manifest.parent_path(), "ahfl.workspace.toml", boundary);
    while (workspace_manifest_path.has_value()) {
        auto workspace_manifest =
            load_workspace_manifest(*workspace_manifest_path, discovery.diagnostics);
        if (!workspace_manifest.has_value()) {
            return discovery;
        }
        if (workspace_contains_package_manifest(
                *workspace_manifest, *workspace_manifest_path, normalized_package_manifest)) {
            return build_workspace_context(*workspace_manifest_path,
                                           normalized_package_manifest,
                                           std::move(*package_manifest),
                                           normalized_sysroot_manifest);
        }
        if (input.explicit_workspace_manifest_path.has_value()) {
            add_error(discovery.diagnostics,
                      "workspace does not contain package manifest '" +
                          normalized_package_manifest.generic_string() + "'");
            return discovery;
        }
        workspace_manifest_path = find_nearest_named_file_bounded(
            workspace_manifest_path->parent_path().parent_path(), "ahfl.workspace.toml", boundary);
    }

    return build_manifest_context(normalized_package_manifest, normalized_sysroot_manifest);
}

} // namespace ahfl::project_discovery
