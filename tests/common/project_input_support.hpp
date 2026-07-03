#pragma once

#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ahfl::test_support {

[[nodiscard]] inline std::filesystem::path
repo_root_from_integration_root(const std::filesystem::path &root) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(root, error);
    const auto fixture_root = error ? root.lexically_normal() : normalized;
    return fixture_root.parent_path().parent_path().parent_path();
}

[[nodiscard]] inline std::vector<std::string> repo_std_exports() {
    return {"prelude",
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

} // namespace ahfl::test_support
