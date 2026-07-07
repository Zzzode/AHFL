#pragma once

#include "compiler/package_graph/package_graph.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ahfl::test_support {

[[nodiscard]] inline std::filesystem::path
repo_root_from_integration_root(const std::filesystem::path &root) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(root, error);
    const auto fixture_root = error ? root.lexically_normal() : normalized;
    return fixture_root.parent_path().parent_path().parent_path();
}

[[nodiscard]] inline std::filesystem::path
repo_root_from_source_file(const std::filesystem::path &source_file) {
    std::error_code error;
    auto current =
        source_file.is_absolute() ? source_file : std::filesystem::absolute(source_file, error);
    if (error) {
        current = source_file;
        error.clear();
    }

    const auto normalized = std::filesystem::weakly_canonical(current, error);
    current = error ? current.lexically_normal() : normalized;
    error.clear();

    if (std::filesystem::is_regular_file(current, error)) {
        current = current.parent_path();
    }
    error.clear();

    for (auto candidate = current; !candidate.empty(); candidate = candidate.parent_path()) {
        if (std::filesystem::exists(candidate / "std" / "ahfl.toml", error) && !error &&
            std::filesystem::exists(candidate / "src", error) && !error) {
            return candidate;
        }
        error.clear();
        if (candidate == candidate.parent_path()) {
            break;
        }
    }

    auto cwd = std::filesystem::current_path(error);
    if (!error && std::filesystem::exists(cwd / "std" / "ahfl.toml", error) && !error) {
        return cwd;
    }
    return current;
}

[[nodiscard]] inline std::vector<std::string> repo_std_exports() {
    return {"prelude",
            "bool",
            "int",
            "float",
            "option",
            "result",
            "string",
            "collections",
            "cmp",
            "fmt",
            "decimal",
            "json",
            "time",
            "uuid",
            "traits"};
}

[[nodiscard]] inline std::vector<std::string> repo_std_intrinsics_allow() {
    return {"option_*",
            "result_*",
            "list_*",
            "set_*",
            "map_*",
            "string_*",
            "decimal_*",
            "json_*",
            "time_*",
            "uuid_*",
            "cmp_raw_compare",
            "int_to_string",
            "bool_to_string",
            "float_to_string",
            "float_trunc_to_int",
            "int_to_float"};
}

[[nodiscard]] inline std::vector<std::string>
exported_modules_for_module_root(const std::filesystem::path &module_root) {
    std::vector<std::string> modules;
    std::error_code error;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             module_root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || error || entry.path().extension() != ".ahfl") {
            error.clear();
            continue;
        }

        auto relative = entry.path().lexically_relative(module_root);
        if (relative.filename() == "mod.ahfl") {
            relative = relative.parent_path();
        } else {
            relative.replace_extension();
        }
        modules.push_back(relative.generic_string());
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return modules;
}

inline void append_workspace_module_roots(ProjectInput &input, const std::filesystem::path &root) {
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }

        const auto exports = exported_modules_for_module_root(entry.path());
        if (exports.empty()) {
            continue;
        }
        input.module_roots.push_back(ProjectInput::ModuleRoot{
            .prefix = entry.path().filename().generic_string(),
            .root = entry.path(),
            .exported_modules = exports,
        });
    }
}

inline void append_repo_std_module_root(ProjectInput &input,
                                        const std::filesystem::path &repo_root) {
    input.module_roots.push_back(ProjectInput::ModuleRoot{
        .prefix = "std",
        .root = repo_root / "std",
        .exported_modules = repo_std_exports(),
        .compiler_intrinsics_allow = repo_std_intrinsics_allow(),
    });
}

struct PackageProjectInputResult {
    std::optional<ProjectInput> input;
    std::vector<package_graph::Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty() || !input.has_value();
    }
};

inline void print_package_graph_diagnostics(
    const std::vector<package_graph::Diagnostic> &diagnostics,
    std::ostream &out) {
    for (const auto &diagnostic : diagnostics) {
        out << "error";
        if (!diagnostic.code.empty()) {
            out << " [" << diagnostic.code << "]";
        }
        out << ": " << diagnostic.message << '\n';
        for (const auto &related : diagnostic.related) {
            out << "  note: " << related.path.generic_string() << ": " << related.message << '\n';
        }
    }
}

[[nodiscard]] inline std::vector<std::string>
dependency_prefixes_for_package(const package_graph::PackageGraph &graph,
                                package_graph::PackageId package_id) {
    std::vector<std::string> prefixes;
    for (const auto &dependency : graph.dependencies) {
        if (dependency.from != package_id) {
            continue;
        }
        const auto *target = graph.find_package(dependency.to);
        if (target != nullptr) {
            prefixes.push_back(target->module_prefix);
        }
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

[[nodiscard]] inline std::vector<std::string>
artifact_exports_for_package(const package_graph::PackageGraph &graph,
                             package_graph::PackageId package_id) {
    std::vector<std::string> exports;
    const auto *package = graph.find_package(package_id);
    if (package == nullptr) {
        return exports;
    }
    for (const auto &target : package->targets) {
        for (const auto &export_item : target.exports) {
            exports.push_back(export_item.name);
        }
    }
    std::sort(exports.begin(), exports.end());
    exports.erase(std::unique(exports.begin(), exports.end()), exports.end());
    return exports;
}

[[nodiscard]] inline ProjectInput
project_input_from_package_graph(const package_graph::PackageGraph &graph,
                                 std::vector<std::filesystem::path> entry_files) {
    ProjectInput input;
    input.entry_files = std::move(entry_files);
    input.inject_prelude = false;
    input.enforce_package_dependencies = true;
    input.module_roots.reserve(graph.module_roots.size());
    for (const auto &root : graph.module_roots) {
        const auto *package = graph.find_package(root.package);
        input.module_roots.push_back(ProjectInput::ModuleRoot{
            .prefix = root.prefix,
            .root = root.root,
            .exported_modules =
                package != nullptr ? package->exported_modules : std::vector<std::string>{},
            .artifact_exports = artifact_exports_for_package(graph, root.package),
            .dependency_prefixes = dependency_prefixes_for_package(graph, root.package),
            .compiler_intrinsics_allow =
                package == nullptr
                    ? std::nullopt
                    : std::optional<std::vector<std::string>>{package->compiler_intrinsics_allow},
        });
    }
    return input;
}

[[nodiscard]] inline ProjectInput
project_input_from_package_graph(const package_graph::PackageGraph &graph,
                                 const std::filesystem::path &entry_file) {
    return project_input_from_package_graph(graph, std::vector<std::filesystem::path>{entry_file});
}

[[nodiscard]] inline PackageProjectInputResult project_input_from_workspace(
    const std::filesystem::path &workspace_root,
    std::string package_name,
    const std::filesystem::path &entry_file,
    const std::filesystem::path &repo_root) {
    auto graph_result = package_graph::build_package_graph_from_workspace(
        package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_root / "ahfl.workspace.toml",
            .package_name = std::move(package_name),
            .sysroot_manifest_path = repo_root / "std" / "ahfl.toml",
        });
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        return PackageProjectInputResult{.diagnostics = std::move(graph_result.diagnostics)};
    }
    return PackageProjectInputResult{
        .input = project_input_from_package_graph(*graph_result.graph, entry_file),
    };
}

[[nodiscard]] inline PackageProjectInputResult project_input_from_manifest(
    const std::filesystem::path &manifest_path,
    const std::filesystem::path &entry_file,
    const std::filesystem::path &repo_root) {
    auto graph_result = package_graph::build_package_graph_from_manifests(
        package_graph::ManifestBuildInput{
            .root_manifest_path = manifest_path,
            .sysroot_manifest_path = repo_root / "std" / "ahfl.toml",
        });
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        return PackageProjectInputResult{.diagnostics = std::move(graph_result.diagnostics)};
    }
    return PackageProjectInputResult{
        .input = project_input_from_package_graph(*graph_result.graph, entry_file),
    };
}

[[nodiscard]] inline ProjectInput project_input_with_repo_std(const std::filesystem::path &entry,
                                                              const std::filesystem::path &root,
                                                              bool inject_prelude = true) {
    ProjectInput input;
    input.entry_files.push_back(entry);
    input.search_roots.push_back(root);
    input.inject_prelude = inject_prelude;
    append_workspace_module_roots(input, root);
    append_repo_std_module_root(input, repo_root_from_integration_root(root));
    return input;
}

[[nodiscard]] inline ProjectInput
project_input_with_repo_std_for_test_file(const std::vector<std::filesystem::path> &entries,
                                          const std::filesystem::path &root,
                                          const std::filesystem::path &test_source_file,
                                          bool inject_prelude = true) {
    ProjectInput input;
    input.entry_files = entries;
    input.search_roots.push_back(root);
    input.inject_prelude = inject_prelude;
    append_workspace_module_roots(input, root);
    append_repo_std_module_root(input, repo_root_from_source_file(test_source_file));
    return input;
}

[[nodiscard]] inline ProjectInput
project_input_with_repo_std_for_test_file(const std::filesystem::path &entry,
                                          const std::filesystem::path &root,
                                          const std::filesystem::path &test_source_file,
                                          bool inject_prelude = true) {
    return project_input_with_repo_std_for_test_file(
        std::vector<std::filesystem::path>{entry}, root, test_source_file, inject_prelude);
}

} // namespace ahfl::test_support
