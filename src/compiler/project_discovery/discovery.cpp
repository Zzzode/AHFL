#include "compiler/project_discovery/discovery.hpp"

#include "compiler/manifest/manifest.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

namespace ahfl::project_discovery {
namespace {

constexpr std::string_view kProjectDiscovery = "E::project_discovery";
constexpr std::string_view kToolchainSysrootInvalid = "E::toolchain_sysroot_invalid";
constexpr std::string_view kToolchainSysrootMissing = "E::toolchain_sysroot_missing";
constexpr std::string_view kToolchainSysrootMismatch = "E::toolchain_sysroot_mismatch";
constexpr std::string_view kToolchainProfileAmbiguous = "E::toolchain_profile_ambiguous";
constexpr std::string_view kToolchainIncompatible = "E::toolchain_incompatible";

void add_error_with_code(std::vector<package_graph::Diagnostic> &diagnostics,
                         std::string_view code,
                         std::string message,
                         SourceRange range = {},
                         std::vector<package_graph::Diagnostic::Related> related = {}) {
    diagnostics.push_back(package_graph::Diagnostic{
        .code = std::string{code},
        .message = std::move(message),
        .range = range,
        .related = std::move(related),
    });
}

void add_error(std::vector<package_graph::Diagnostic> &diagnostics,
               std::string message,
               SourceRange range = {}) {
    add_error_with_code(diagnostics, kProjectDiscovery, std::move(message), range);
}

[[nodiscard]] package_graph::Diagnostic::Related
related_path(std::filesystem::path path, std::string message) {
    return package_graph::Diagnostic::Related{
        .path = normalize_project_path(std::move(path)),
        .message = std::move(message),
    };
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

void append_discovery_diagnostics(std::vector<package_graph::Diagnostic> &target,
                                  const std::vector<package_graph::Diagnostic> &source) {
    target.reserve(target.size() + source.size());
    for (const auto &diagnostic : source) {
        target.push_back(diagnostic);
    }
}

[[nodiscard]] bool declares_std_identity(const manifest::PackageManifest &manifest) {
    return manifest.package_name == "std" || manifest.package_kind == "standard-library" ||
           manifest.module_prefix == "std";
}

[[nodiscard]] std::string_view toolchain_origin_name(ToolchainProfileOrigin origin) noexcept {
    switch (origin) {
    case ToolchainProfileOrigin::CliFlag:
        return "cli-flag";
    case ToolchainProfileOrigin::Environment:
        return "environment";
    case ToolchainProfileOrigin::CompileDefault:
        return "compile-default";
    case ToolchainProfileOrigin::LspConfiguration:
        return "lsp-configuration";
    case ToolchainProfileOrigin::LspInitialization:
        return "lsp-initialization";
    case ToolchainProfileOrigin::BundledExtension:
        return "bundled-extension";
    }
    return "unknown";
}

[[nodiscard]] std::optional<ToolchainProfileSelection>
select_toolchain_profile_impl(const ToolchainProfileSet &toolchains,
                              const std::filesystem::path &document_path) {
    const WorkspaceToolchainProfile *best = nullptr;
    for (const auto &candidate : toolchains.workspace_profiles) {
        if (candidate.workspace_root.empty()) {
            continue;
        }
        const auto normalized_root = normalize_project_path(candidate.workspace_root);
        if (!path_is_equal_or_descendant(document_path, normalized_root)) {
            continue;
        }
        if (best == nullptr ||
            normalized_root.generic_string().size() >
                normalize_project_path(best->workspace_root).generic_string().size()) {
            best = &candidate;
        }
    }
    if (best != nullptr) {
        auto profile = best->profile;
        profile.scope = ToolchainProfileScope::WorkspaceFolder;
        return ToolchainProfileSelection{
            .profile = std::move(profile),
            .workspace_root = normalize_project_path(best->workspace_root),
        };
    }
    if (toolchains.default_profile.has_value()) {
        auto profile = *toolchains.default_profile;
        profile.scope = ToolchainProfileScope::GlobalDefault;
        return ToolchainProfileSelection{.profile = std::move(profile)};
    }
    if (auto profile = default_toolchain_profile_from_compile_default(); profile.has_value()) {
        profile->scope = ToolchainProfileScope::GlobalDefault;
        return ToolchainProfileSelection{.profile = std::move(*profile)};
    }
    return std::nullopt;
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

[[nodiscard]] ToolchainProfileResult
toolchain_profile_from_std_manifest(const std::filesystem::path &std_manifest_path,
                                    ToolchainProfileOrigin origin) {
    ToolchainProfileResult result;
    const auto normalized_manifest = normalize_project_path(std_manifest_path);

    std::error_code error;
    if (!std::filesystem::is_regular_file(normalized_manifest, error) || error) {
        add_error_with_code(result.diagnostics,
                            kToolchainSysrootMissing,
                            "sysroot root does not contain std/ahfl.toml at '" +
                                normalized_manifest.generic_string() + "'");
        return result;
    }

    auto package_manifest = load_package_manifest(normalized_manifest, result.diagnostics);
    if (!package_manifest.has_value()) {
        add_error_with_code(result.diagnostics,
                            kToolchainSysrootInvalid,
                            "active std manifest is not a valid AHFL package manifest: '" +
                                normalized_manifest.generic_string() + "'");
        return result;
    }

    if (package_manifest->package_name != "std" ||
        package_manifest->package_kind != "standard-library" ||
        package_manifest->module_prefix != "std") {
        add_error_with_code(result.diagnostics,
                            kToolchainSysrootInvalid,
                            "active std manifest must declare package.name 'std', "
                            "package.kind 'standard-library', and module.prefix 'std'");
        return result;
    }

    package_graph::PackageInput package{
        .manifest = std::move(*package_manifest),
        .package_root = normalized_manifest.parent_path(),
        .source = package_graph::PackageSourceKind::Sysroot,
        .manifest_path = normalized_manifest,
    };
    auto checksum = package_graph::compute_package_checksum(package, result.diagnostics);
    if (!checksum.has_value()) {
        return result;
    }

    result.profile = ToolchainProfile{
        .sysroot_root = normalize_project_path(normalized_manifest.parent_path().parent_path()),
        .std_manifest = normalized_manifest,
        .origin = origin,
        .scope = ToolchainProfileScope::GlobalDefault,
        .std_identity = std::move(*checksum),
        .server_compatibility = ToolchainServerCompatibility::SameBuild,
    };
    return result;
}

void append_package_graph_diagnostics(std::vector<package_graph::Diagnostic> &target,
                                      std::vector<package_graph::Diagnostic> source) {
    target.reserve(target.size() + source.size());
    for (auto &diagnostic : source) {
        target.push_back(std::move(diagnostic));
    }
}

[[nodiscard]] bool same_toolchain_sysroot(const ToolchainProfile &lhs,
                                          const ToolchainProfile &rhs) {
    return normalize_project_path(lhs.std_manifest) == normalize_project_path(rhs.std_manifest);
}

void reject_cross_profile_package_graph(ProjectDiscoveryResult &discovery,
                                        const ProjectDiscoveryInput &input,
                                        const ToolchainProfileSelection &active_selection) {
    if (!discovery.context.has_value()) {
        return;
    }

    for (const auto &package : discovery.context->graph.packages) {
        if (package.source == package_graph::PackageSourceKind::Sysroot) {
            continue;
        }

        const auto package_selection =
            select_toolchain_profile_for_document(input.toolchains, package.manifest_path);
        if (!package_selection.has_value() ||
            same_toolchain_sysroot(active_selection.profile, package_selection->profile)) {
            continue;
        }

        add_error_with_code(
            discovery.diagnostics,
            kToolchainProfileAmbiguous,
            "package graph crosses AHFL toolchain profiles; package manifest '" +
                normalize_project_path(package.manifest_path).generic_string() +
                "' uses sysroot '" +
                normalize_project_path(package_selection->profile.std_manifest).generic_string() +
                "' but active document uses sysroot '" +
                normalize_project_path(active_selection.profile.std_manifest).generic_string() +
                "'",
            {},
            {
                related_path(package.manifest_path, "package manifest in the crossed graph"),
                related_path(package_selection->profile.std_manifest,
                             "sysroot selected for that package manifest"),
                related_path(active_selection.profile.std_manifest,
                             "active sysroot selected for the current document"),
            });
        discovery.context.reset();
        return;
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

std::optional<std::filesystem::path>
find_package_manifest_for_document(const std::filesystem::path &document_path,
                                   const std::vector<WorkspaceBoundary> &workspace_boundaries) {
    const auto normalized_document = normalize_project_path(document_path);
    const auto boundary = containing_workspace_boundary(normalized_document, workspace_boundaries);
    const auto start = std::filesystem::is_directory(normalized_document)
                           ? normalized_document
                           : normalized_document.parent_path();
    return find_nearest_named_file_bounded(start, "ahfl.toml", boundary);
}

std::optional<ToolchainProfileSelection>
select_toolchain_profile_for_document(const ToolchainProfileSet &toolchains,
                                      const std::filesystem::path &document_path) {
    return select_toolchain_profile_impl(toolchains, normalize_project_path(document_path));
}

ToolchainProfileResult toolchain_profile_from_sysroot_input(const std::filesystem::path &path,
                                                            ToolchainProfileOrigin origin) {
    ToolchainProfileResult result;
    if (path.empty()) {
        add_error_with_code(
            result.diagnostics, kToolchainSysrootInvalid, "sysroot path must not be empty");
        return result;
    }

    const auto normalized = normalize_project_path(path);
    if (normalized.filename() == "ahfl.toml") {
        if (normalized.parent_path().filename() != "std") {
            add_error_with_code(
                result.diagnostics,
                kToolchainSysrootInvalid,
                "sysroot manifest input must be '<toolchain-root>/std/ahfl.toml', got '" +
                    normalized.generic_string() + "'");
            return result;
        }
        return toolchain_profile_from_std_manifest(normalized, origin);
    }

    if (normalized.filename() == "std") {
        add_error_with_code(result.diagnostics,
                            kToolchainSysrootInvalid,
                            "sysroot input must be the toolchain root or std/ahfl.toml; got std "
                            "directory '" +
                                normalized.generic_string() + "'");
        return result;
    }

    return toolchain_profile_from_std_manifest(
        normalize_project_path(normalized / "std" / "ahfl.toml"), origin);
}

std::optional<ToolchainProfile> default_toolchain_profile_from_compile_default() {
#ifdef AHFL_DEFAULT_SYSROOT
    constexpr std::string_view kDefaultSysroot = AHFL_DEFAULT_SYSROOT;
    if constexpr (!kDefaultSysroot.empty()) {
        auto result = toolchain_profile_from_sysroot_input(
            std::filesystem::path{std::string{kDefaultSysroot}},
            ToolchainProfileOrigin::CompileDefault);
        return std::move(result.profile);
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

    if (!input.toolchains.diagnostics.empty()) {
        append_discovery_diagnostics(discovery.diagnostics, input.toolchains.diagnostics);
        return discovery;
    }

    const auto active_selection =
        select_toolchain_profile_for_document(input.toolchains, document_path);
    if (!active_selection.has_value()) {
        add_error_with_code(discovery.diagnostics,
                            kToolchainSysrootMissing,
                            "failed to locate sysroot std/ahfl.toml; configure "
                            "ahfl.toolchain.sysroot or pass --sysroot <path>");
        return discovery;
    }
    if (active_selection->profile.server_compatibility ==
        ToolchainServerCompatibility::Incompatible) {
        add_error_with_code(
            discovery.diagnostics,
            kToolchainIncompatible,
            "active AHFL sysroot is incompatible with this language server; "
            "profile origin is '" +
                std::string{toolchain_origin_name(active_selection->profile.origin)} +
                "', std manifest is '" +
                normalize_project_path(active_selection->profile.std_manifest).generic_string() +
                "'",
            {},
            {
                related_path(active_selection->profile.std_manifest,
                             "active std manifest from profile origin '" +
                                 std::string{
                                     toolchain_origin_name(active_selection->profile.origin)} +
                                 "'"),
            });
        return discovery;
    }

    const auto normalized_package_manifest = normalize_project_path(*package_manifest_path);
    const auto normalized_sysroot_manifest =
        normalize_project_path(active_selection->profile.std_manifest);
    if (normalized_package_manifest == normalized_sysroot_manifest) {
        return build_sysroot_context(normalized_sysroot_manifest);
    }

    auto package_manifest =
        load_package_manifest(normalized_package_manifest, discovery.diagnostics);
    if (!package_manifest.has_value()) {
        return discovery;
    }
    if (declares_std_identity(*package_manifest)) {
        add_error_with_code(discovery.diagnostics,
                            kToolchainSysrootMismatch,
                            "this standard-library package is not the active AHFL sysroot; "
                            "active sysroot is '" +
                                normalized_sysroot_manifest.generic_string() +
                                "', opened package is '" +
                                normalized_package_manifest.generic_string() +
                                "'; help: configure ahfl.toolchain.sysroot to '" +
                                normalized_package_manifest.parent_path().parent_path()
                                    .generic_string() +
                                "' when developing corelib",
                            {},
                            {
                                related_path(normalized_package_manifest,
                                             "opened standard-library package manifest"),
                                related_path(normalized_sysroot_manifest,
                                             "active std manifest from profile origin '" +
                                                 std::string{toolchain_origin_name(
                                                     active_selection->profile.origin)} +
                                                 "'"),
                            });
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
            auto workspace_discovery = build_workspace_context(*workspace_manifest_path,
                                                               normalized_package_manifest,
                                                               std::move(*package_manifest),
                                                               normalized_sysroot_manifest);
            reject_cross_profile_package_graph(workspace_discovery, input, *active_selection);
            return workspace_discovery;
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

    auto manifest_discovery =
        build_manifest_context(normalized_package_manifest, normalized_sysroot_manifest);
    reject_cross_profile_package_graph(manifest_discovery, input, *active_selection);
    return manifest_discovery;
}

} // namespace ahfl::project_discovery
