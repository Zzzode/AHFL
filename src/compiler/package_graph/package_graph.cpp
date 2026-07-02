#include "compiler/package_graph/package_graph.hpp"

#include "base/support/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ahfl::package_graph {
namespace {

constexpr std::string_view kPackageGraph = "E::package_graph";

[[nodiscard]] std::filesystem::path normalize_manifest_path(const std::filesystem::path &path);

void add_error(std::vector<Diagnostic> &diagnostics, std::string message, SourceRange range = {}) {
    diagnostics.push_back(Diagnostic{
        .code = std::string{kPackageGraph},
        .message = std::move(message),
        .range = range,
    });
}

[[nodiscard]] std::filesystem::path module_root_for(const PackageInput &input) {
    return (input.package_root / input.manifest.module_root).lexically_normal();
}

[[nodiscard]] bool path_has_escape_or_invalid_part(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
        return true;
    }
    for (const auto &part : path) {
        if (part.empty() || part == "." || part == "..") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::filesystem::path module_file_path(const std::filesystem::path &root,
                                                     const std::filesystem::path &module_path) {
    auto file = root / module_path;
    file += ".ahfl";
    return normalize_manifest_path(file);
}

[[nodiscard]] std::filesystem::path
directory_module_path(const std::filesystem::path &root, const std::filesystem::path &module_path) {
    return normalize_manifest_path(root / module_path / "mod.ahfl");
}

[[nodiscard]] TargetNode
make_target(PackageId package, std::size_t index, const manifest::TargetManifest &manifest) {
    return TargetNode{
        .id = TargetId{.package = package, .value = index},
        .name = manifest.name,
        .kind = manifest.kind,
        .entry = manifest.entry,
        .exports = manifest.exports,
        .capability_bindings = manifest.capability_bindings,
    };
}

[[nodiscard]] PackageNode make_node(PackageId id, const PackageInput &input) {
    PackageNode node;
    node.id = id;
    node.source = input.source;
    node.name = input.manifest.package_name;
    node.version = input.manifest.package_version;
    node.kind = input.manifest.package_kind;
    node.module_prefix = input.manifest.module_prefix;
    node.package_root = input.package_root.lexically_normal();
    node.module_root = module_root_for(input);
    node.manifest_path = input.manifest_path.lexically_normal();
    node.checksum = input.checksum;
    node.exported_modules = input.manifest.exported_modules;
    node.targets.reserve(input.manifest.targets.size());
    for (std::size_t i = 0; i < input.manifest.targets.size(); ++i) {
        node.targets.push_back(make_target(id, i, input.manifest.targets[i]));
    }
    return node;
}

struct IndexedInput {
    PackageId id;
    const PackageInput *input{nullptr};
};

[[nodiscard]] bool is_user_package(PackageSourceKind source) noexcept {
    return source != PackageSourceKind::Sysroot;
}

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  std::string &content,
                                  std::vector<Diagnostic> &diagnostics,
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

[[nodiscard]] std::filesystem::path normalize_manifest_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

[[nodiscard]] std::filesystem::path logical_package_path(const std::filesystem::path &package_root,
                                                         const std::filesystem::path &path) {
    std::error_code error;
    auto relative = std::filesystem::relative(path, package_root, error);
    if (!error && !relative.empty()) {
        return relative.lexically_normal();
    }
    relative = path.lexically_relative(package_root);
    if (!relative.empty()) {
        return relative.lexically_normal();
    }
    return path.filename();
}

void append_checksum_record(std::string &payload,
                            std::string_view kind,
                            const std::filesystem::path &logical_path,
                            std::string_view content) {
    payload.append(kind);
    payload.push_back('\0');
    payload.append(logical_path.generic_string());
    payload.push_back('\0');
    payload.append(std::to_string(content.size()));
    payload.push_back('\0');
    payload.append(content);
    payload.push_back('\0');
}

[[nodiscard]] std::string normalize_newlines(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '\r') {
            output.push_back('\n');
            if (index + 1 < input.size() && input[index + 1] == '\n') {
                ++index;
            }
            continue;
        }
        output.push_back(input[index]);
    }
    return output;
}

[[nodiscard]] std::optional<std::string>
compute_package_checksum(const PackageInput &input, std::vector<Diagnostic> &diagnostics) {
    std::string payload;
    payload.append("ahfl.package.checksum.v1");
    payload.push_back('\0');
    const auto canonical_manifest = manifest::canonicalize_package_manifest(input.manifest);
    append_checksum_record(payload,
                           "manifest",
                           logical_package_path(input.package_root, input.manifest_path),
                           canonical_manifest);
    if (input.source != PackageSourceKind::Sysroot) {
        return "sha256:" + ahfl::support::sha256_hex(payload);
    }

    const auto module_root = module_root_for(input);
    std::error_code error;
    const auto module_root_exists = std::filesystem::exists(module_root, error);
    if (error) {
        add_error(diagnostics,
                  "failed to inspect module root '" + module_root.generic_string() + "'");
        return std::nullopt;
    }
    if (!module_root_exists) {
        return "sha256:" + ahfl::support::sha256_hex(payload);
    }

    const auto module_root_is_directory = std::filesystem::is_directory(module_root, error);
    if (error) {
        add_error(diagnostics,
                  "failed to inspect module root '" + module_root.generic_string() + "'");
        return std::nullopt;
    }
    if (!module_root_is_directory) {
        return "sha256:" + ahfl::support::sha256_hex(payload);
    }

    std::vector<std::filesystem::path> source_files;
    try {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 module_root, std::filesystem::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file(error) || error) {
                error.clear();
                continue;
            }
            if (entry.path().extension() == ".ahfl") {
                source_files.push_back(normalize_manifest_path(entry.path()));
            }
        }
    } catch (const std::filesystem::filesystem_error &ex) {
        add_error(diagnostics,
                  "failed to scan package source root '" + module_root.generic_string() +
                      "': " + ex.what());
        return std::nullopt;
    }

    std::sort(source_files.begin(), source_files.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });

    for (const auto &source_file : source_files) {
        std::string content;
        if (!read_text_file(source_file, content, diagnostics, "package source")) {
            return std::nullopt;
        }
        content = normalize_newlines(content);
        append_checksum_record(
            payload, "source", logical_package_path(input.package_root, source_file), content);
    }

    return "sha256:" + ahfl::support::sha256_hex(payload);
}

void append_manifest_diagnostics(std::vector<Diagnostic> &target,
                                 const std::vector<manifest::ManifestDiagnostic> &source) {
    target.reserve(target.size() + source.size());
    for (const auto &diagnostic : source) {
        target.push_back(Diagnostic{
            .code = diagnostic.code,
            .message = diagnostic.message,
            .range = diagnostic.range,
        });
    }
}

[[nodiscard]] bool same_dependency_spec(const manifest::DependencySpec &lhs,
                                        const manifest::DependencySpec &rhs) {
    return lhs.key == rhs.key && lhs.source == rhs.source && lhs.path == rhs.path &&
           lhs.version == rhs.version;
}

[[nodiscard]] PackageInput make_manifest_package_input(manifest::PackageManifest manifest,
                                                       std::filesystem::path manifest_path,
                                                       PackageSourceKind source) {
    const auto normalized_manifest_path = normalize_manifest_path(manifest_path);
    return PackageInput{
        .manifest = std::move(manifest),
        .package_root = normalized_manifest_path.parent_path(),
        .source = source,
        .manifest_path = normalized_manifest_path,
    };
}

[[nodiscard]] std::optional<manifest::WorkspaceManifest>
load_workspace_manifest(const std::filesystem::path &manifest_path,
                        std::vector<Diagnostic> &diagnostics) {
    std::string text;
    const auto normalized_manifest_path = normalize_manifest_path(manifest_path);
    if (!read_text_file(normalized_manifest_path, text, diagnostics, "workspace manifest")) {
        return std::nullopt;
    }

    auto parsed = manifest::parse_workspace_manifest(text);
    append_manifest_diagnostics(diagnostics, parsed.diagnostics);
    if (!parsed.manifest.has_value()) {
        return std::nullopt;
    }

    return std::move(*parsed.manifest);
}

[[nodiscard]] std::optional<PackageInput>
load_manifest_package(const std::filesystem::path &manifest_path,
                      PackageSourceKind source,
                      std::vector<Diagnostic> &diagnostics,
                      std::string_view role) {
    std::string text;
    const auto normalized_manifest_path = normalize_manifest_path(manifest_path);
    const auto role_text = std::string{role} + " manifest";
    if (!read_text_file(normalized_manifest_path, text, diagnostics, role_text)) {
        return std::nullopt;
    }

    auto parsed = manifest::parse_package_manifest(text);
    append_manifest_diagnostics(diagnostics, parsed.diagnostics);
    if (!parsed.manifest.has_value()) {
        return std::nullopt;
    }

    auto package =
        make_manifest_package_input(std::move(*parsed.manifest), normalized_manifest_path, source);
    auto checksum = compute_package_checksum(package, diagnostics);
    if (!checksum.has_value()) {
        return std::nullopt;
    }
    package.checksum = std::move(*checksum);
    return package;
}

void apply_workspace_defaults(PackageInput &package,
                              const manifest::WorkspaceManifest &workspace,
                              std::vector<Diagnostic> &diagnostics) {
    for (const auto &workspace_dependency : workspace.dependencies) {
        const auto existing = std::find_if(
            package.manifest.dependencies.begin(),
            package.manifest.dependencies.end(),
            [&](const auto &dependency) { return dependency.key == workspace_dependency.key; });
        if (existing != package.manifest.dependencies.end()) {
            if (!same_dependency_spec(*existing, workspace_dependency)) {
                add_error(diagnostics,
                          "workspace dependency default for '" + workspace_dependency.key +
                              "' conflicts with package dependency declaration",
                          existing->range);
            }
            continue;
        }

        if (workspace_dependency.key == "std" && workspace_dependency.source == "sysroot") {
            package.manifest.dependencies.push_back(workspace_dependency);
        }
    }
}

void collect_path_dependencies(const PackageInput &from,
                               std::vector<PackageInput> &path_packages,
                               std::set<std::string> &loaded_manifest_paths,
                               std::vector<Diagnostic> &diagnostics,
                               bool allow_workspace_dependencies) {
    for (const auto &dependency : from.manifest.dependencies) {
        if (dependency.source == "sysroot") {
            continue;
        }
        if (dependency.source == "workspace") {
            if (!allow_workspace_dependencies) {
                add_error(diagnostics,
                          "workspace dependency '" + dependency.key +
                              "' requires workspace manifest context",
                          dependency.range);
            }
            continue;
        }
        if (dependency.source != "path" || !dependency.path.has_value()) {
            continue;
        }

        const auto dependency_manifest =
            normalize_manifest_path(from.package_root / *dependency.path / "ahfl.toml");
        if (!loaded_manifest_paths.insert(dependency_manifest.string()).second) {
            continue;
        }

        auto loaded = load_manifest_package(
            dependency_manifest, PackageSourceKind::Path, diagnostics, "path dependency");
        if (!loaded.has_value()) {
            continue;
        }

        path_packages.push_back(std::move(*loaded));
        const auto loaded_copy = path_packages.back();
        collect_path_dependencies(loaded_copy,
                                  path_packages,
                                  loaded_manifest_paths,
                                  diagnostics,
                                  allow_workspace_dependencies);
    }
}

void validate_exported_modules(const IndexedInput &indexed, std::vector<Diagnostic> &diagnostics) {
    std::error_code error;
    if (!std::filesystem::exists(indexed.input->manifest_path, error) || error) {
        return;
    }

    const auto module_root = module_root_for(*indexed.input);
    std::unordered_set<std::string> seen;
    for (const auto &exported : indexed.input->manifest.exported_modules) {
        const std::filesystem::path module_path{exported};
        if (!seen.insert(module_path.generic_string()).second) {
            add_error(diagnostics,
                      "package '" + indexed.input->manifest.package_name +
                          "' exports duplicate module '" + exported + "'");
            continue;
        }

        if (path_has_escape_or_invalid_part(module_path)) {
            add_error(diagnostics,
                      "package '" + indexed.input->manifest.package_name + "' export module '" +
                          exported + "' must be a relative module path inside module root");
            continue;
        }

        const auto single_file = module_file_path(module_root, module_path);
        const auto directory_module = directory_module_path(module_root, module_path);
        const bool single_exists = std::filesystem::exists(single_file, error) && !error;
        error.clear();
        const bool directory_exists = std::filesystem::exists(directory_module, error) && !error;
        error.clear();

        if (single_exists && directory_exists) {
            add_error(diagnostics,
                      "package '" + indexed.input->manifest.package_name + "' export module '" +
                          exported +
                          "' is ambiguous: both single-file and directory-module layouts exist");
            continue;
        }

        if (!single_exists && !directory_exists) {
            add_error(diagnostics,
                      "package '" + indexed.input->manifest.package_name + "' export module '" +
                          exported + "' does not exist under module root '" +
                          module_root.generic_string() + "'");
        }
    }
}

} // namespace

const PackageNode *PackageGraph::find_package(PackageId id) const {
    if (id.value >= packages.size()) {
        return nullptr;
    }
    return &packages[id.value];
}

std::optional<PackageId> PackageGraph::package_by_name(std::string_view name) const {
    for (const auto &package : packages) {
        if (package.name == name) {
            return package.id;
        }
    }
    return std::nullopt;
}

BuildResult build_package_graph(const BuildInput &input) {
    BuildResult result;
    PackageGraph graph;
    std::vector<IndexedInput> inputs;

    auto append = [&](PackageId id, const PackageInput &package_input) {
        graph.packages.push_back(make_node(id, package_input));
        graph.module_roots.push_back(ModuleRootEntry{
            .prefix = package_input.manifest.module_prefix,
            .package = id,
            .root = module_root_for(package_input),
        });
        inputs.push_back(IndexedInput{.id = id, .input = &package_input});
    };

    append(PackageId{0}, input.sysroot_std);
    append(PackageId{1}, input.root_package);
    std::size_t next_id = 2;
    for (const auto &workspace_package : input.workspace_packages) {
        append(PackageId{next_id++}, workspace_package);
    }
    for (const auto &path_package : input.path_packages) {
        append(PackageId{next_id++}, path_package);
    }

    if (input.sysroot_std.manifest.package_name != "std" ||
        input.sysroot_std.manifest.package_kind != "standard-library" ||
        input.sysroot_std.manifest.module_prefix != "std") {
        add_error(result.diagnostics,
                  "sysroot package must be standard-library package 'std' with prefix 'std'");
    }

    std::unordered_map<std::string, PackageId> by_name;
    std::unordered_map<std::string, PackageId> by_prefix;
    for (const auto &package : graph.packages) {
        if (!by_name.emplace(package.name, package.id).second) {
            add_error(result.diagnostics, "duplicate package name '" + package.name + "'");
        }
        if (!by_prefix.emplace(package.module_prefix, package.id).second) {
            add_error(result.diagnostics,
                      "duplicate module prefix '" + package.module_prefix + "'");
        }
    }

    for (const auto &indexed : inputs) {
        validate_exported_modules(indexed, result.diagnostics);
    }

    const auto resolve_dependency =
        [&](const IndexedInput &from,
            const manifest::DependencySpec &dependency) -> std::optional<PackageId> {
        if (dependency.key == "std") {
            if (dependency.source != "sysroot") {
                add_error(result.diagnostics,
                          "user package cannot declare a second std package",
                          dependency.range);
                return std::nullopt;
            }
            return PackageId{0};
        }

        if (from.input->source == PackageSourceKind::Sysroot) {
            add_error(result.diagnostics,
                      "std cannot depend on user package '" + dependency.key + "'",
                      dependency.range);
            return std::nullopt;
        }

        const auto found = by_name.find(dependency.key);
        if (found == by_name.end()) {
            add_error(result.diagnostics,
                      "missing dependency package '" + dependency.key + "'",
                      dependency.range);
            return std::nullopt;
        }

        const auto *target = graph.find_package(found->second);
        if (target == nullptr) {
            add_error(result.diagnostics,
                      "dependency package '" + dependency.key + "' is not in PackageGraph",
                      dependency.range);
            return std::nullopt;
        }

        if (dependency.source == "workspace" && target->source != PackageSourceKind::Workspace &&
            target->source != PackageSourceKind::Root) {
            add_error(result.diagnostics,
                      "workspace dependency '" + dependency.key +
                          "' does not resolve to a workspace package",
                      dependency.range);
            return std::nullopt;
        }

        if (dependency.source == "path" && target->source == PackageSourceKind::Sysroot) {
            add_error(result.diagnostics,
                      "path dependency '" + dependency.key + "' cannot resolve to sysroot",
                      dependency.range);
            return std::nullopt;
        }

        if (dependency.version.has_value() && *dependency.version != target->version) {
            add_error(result.diagnostics,
                      "dependency '" + dependency.key + "' requires version " +
                          *dependency.version + " but resolved " + target->version,
                      dependency.range);
            return std::nullopt;
        }

        return target->id;
    };

    std::vector<std::vector<PackageId>> adjacency(graph.packages.size());
    for (const auto &indexed : inputs) {
        for (const auto &dependency : indexed.input->manifest.dependencies) {
            const auto to = resolve_dependency(indexed, dependency);
            if (!to.has_value()) {
                continue;
            }
            graph.dependencies.push_back(DependencyEdge{
                .from = indexed.id,
                .dependency_key = dependency.key,
                .to = *to,
                .source = dependency.source,
            });
            adjacency[indexed.id.value].push_back(*to);
        }
    }

    for (const auto &package : graph.packages) {
        if (is_user_package(package.source) && package.name == "std") {
            add_error(result.diagnostics, "user package cannot be named 'std'");
        }
    }

    std::vector<int> state(graph.packages.size(), 0);
    std::vector<PackageId> stack;
    bool cycle_reported = false;
    std::function<void(PackageId)> dfs = [&](PackageId id) {
        if (cycle_reported) {
            return;
        }
        state[id.value] = 1;
        stack.push_back(id);
        for (const auto next : adjacency[id.value]) {
            if (state[next.value] == 0) {
                dfs(next);
                continue;
            }
            if (state[next.value] == 1) {
                std::string cycle;
                const auto it = std::find_if(
                    stack.begin(), stack.end(), [next](PackageId item) { return item == next; });
                for (auto cursor = it; cursor != stack.end(); ++cursor) {
                    if (!cycle.empty()) {
                        cycle += " -> ";
                    }
                    if (const auto *node = graph.find_package(*cursor); node != nullptr) {
                        cycle += node->name;
                    }
                }
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                if (const auto *node = graph.find_package(next); node != nullptr) {
                    cycle += node->name;
                }
                add_error(result.diagnostics, "dependency cycle: " + cycle);
                cycle_reported = true;
                return;
            }
        }
        stack.pop_back();
        state[id.value] = 2;
    };

    for (const auto &package : graph.packages) {
        if (state[package.id.value] == 0) {
            dfs(package.id);
        }
    }

    if (!result.has_errors()) {
        result.graph = std::move(graph);
    }
    return result;
}

BuildResult build_package_graph_from_manifests(const ManifestBuildInput &input) {
    BuildResult result;

    auto sysroot = load_manifest_package(
        input.sysroot_manifest_path, PackageSourceKind::Sysroot, result.diagnostics, "sysroot std");
    auto root = load_manifest_package(
        input.root_manifest_path, PackageSourceKind::Root, result.diagnostics, "root package");
    if (result.has_errors() || !sysroot.has_value() || !root.has_value()) {
        return result;
    }

    std::vector<PackageInput> path_packages;
    std::set<std::string> loaded_manifest_paths{
        normalize_manifest_path(input.sysroot_manifest_path).string(),
        normalize_manifest_path(input.root_manifest_path).string(),
    };
    collect_path_dependencies(
        *root, path_packages, loaded_manifest_paths, result.diagnostics, false);
    if (result.has_errors()) {
        return result;
    }

    return build_package_graph(BuildInput{
        .sysroot_std = std::move(*sysroot),
        .root_package = std::move(*root),
        .path_packages = std::move(path_packages),
    });
}

BuildResult build_package_graph_from_workspace(const WorkspaceBuildInput &input) {
    BuildResult result;

    auto sysroot = load_manifest_package(
        input.sysroot_manifest_path, PackageSourceKind::Sysroot, result.diagnostics, "sysroot std");
    auto workspace = load_workspace_manifest(input.workspace_manifest_path, result.diagnostics);
    if (result.has_errors() || !sysroot.has_value() || !workspace.has_value()) {
        return result;
    }

    const auto workspace_manifest_path = normalize_manifest_path(input.workspace_manifest_path);
    const auto workspace_root = workspace_manifest_path.parent_path();
    std::set<std::string> loaded_manifest_paths{
        normalize_manifest_path(input.sysroot_manifest_path).string(),
    };
    std::vector<PackageInput> members;
    members.reserve(workspace->members.size());

    for (const auto &member : workspace->members) {
        const auto member_manifest = normalize_manifest_path(workspace_root / member / "ahfl.toml");
        if (!loaded_manifest_paths.insert(member_manifest.string()).second) {
            add_error(result.diagnostics,
                      "workspace contains duplicate member path '" + member + "'");
            continue;
        }

        auto package = load_manifest_package(
            member_manifest, PackageSourceKind::Workspace, result.diagnostics, "workspace member");
        if (!package.has_value()) {
            continue;
        }

        apply_workspace_defaults(*package, *workspace, result.diagnostics);
        members.push_back(std::move(*package));
    }

    if (result.has_errors()) {
        return result;
    }

    const auto selected = std::find_if(members.begin(), members.end(), [&](const auto &package) {
        return package.manifest.package_name == input.package_name;
    });
    if (selected == members.end()) {
        add_error(result.diagnostics,
                  "workspace does not contain package named '" + input.package_name + "'");
        return result;
    }

    PackageInput root = std::move(*selected);
    root.source = PackageSourceKind::Root;
    members.erase(selected);

    std::vector<PackageInput> path_packages;
    collect_path_dependencies(root, path_packages, loaded_manifest_paths, result.diagnostics, true);
    for (const auto &member : members) {
        collect_path_dependencies(
            member, path_packages, loaded_manifest_paths, result.diagnostics, true);
    }
    if (result.has_errors()) {
        return result;
    }

    return build_package_graph(BuildInput{
        .sysroot_std = std::move(*sysroot),
        .root_package = std::move(root),
        .workspace_packages = std::move(members),
        .path_packages = std::move(path_packages),
    });
}

std::string_view source_kind_name(PackageSourceKind kind) noexcept {
    switch (kind) {
    case PackageSourceKind::Sysroot:
        return "sysroot";
    case PackageSourceKind::Root:
        return "root";
    case PackageSourceKind::Workspace:
        return "workspace";
    case PackageSourceKind::Path:
        return "path";
    }
    return "unknown";
}

} // namespace ahfl::package_graph
