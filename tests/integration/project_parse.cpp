#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using ahfl::test_support::project_input_with_repo_std;

void print_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cout);
}

void print_diagnostics(const std::vector<ahfl::package_graph::Diagnostic> &diagnostics) {
    ahfl::test_support::print_package_graph_diagnostics(diagnostics, std::cout);
}

[[nodiscard]] std::string render_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    std::ostringstream out;
    diagnostics.render(out);
    return out.str();
}

[[nodiscard]] bool contains_message(const ahfl::DiagnosticBag &diagnostics,
                                    std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool contains_code(const ahfl::DiagnosticBag &diagnostics, std::string_view code) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool contains_text(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool write_text_file(const std::filesystem::path &path, std::string_view text) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

int run_diagnostics_support_metadata_smoke() {
    ahfl::DiagnosticBag diagnostics;
    diagnostics.error()
        .code(ahfl::ErrorCode<ahfl::DiagnosticCategory::Validation>{"TEST_VALIDATION"})
        .message("metadata smoke")
        .emit();

    if (!diagnostics.has_error() || diagnostics.entries().size() != 1) {
        std::cerr << "expected exactly one error diagnostic\n";
        return 1;
    }

    const auto &entry = diagnostics.entries().front();
    if (!entry.code.has_value() || *entry.code != "validation.TEST_VALIDATION") {
        std::cerr << "expected validation diagnostic code 'validation.TEST_VALIDATION', got '"
                  << (entry.code.has_value() ? *entry.code : "<none>") << "'\n";
        return 1;
    }

    std::ostringstream rendered;
    diagnostics.render(rendered, std::nullopt, true);
    if (!contains_text(rendered.str(), "error [validation.TEST_VALIDATION]")) {
        std::cerr << "expected render(include_code=true) to include category.code\n";
        return 1;
    }

    return 0;
}

int run_source_file_position_smoke() {
    ahfl::SourceFile source;
    source.display_name = "inline";
    source.content = "alpha\nbeta\ngamma";

    const auto at_start = source.locate(0);
    const auto at_second_line_start = source.locate(6);
    const auto at_second_line_end = source.locate(10);
    const auto at_third_line_start = source.locate(11);
    const auto at_second_line_col3 = source.offset_of(2, 3);
    const auto at_third_line_col2 = source.offset_of(3, 2);

    if (at_start.line != 1 || at_start.column != 1 || at_second_line_start.line != 2 ||
        at_second_line_start.column != 1 || at_second_line_end.line != 2 ||
        at_second_line_end.column != 5 || at_third_line_start.line != 3 ||
        at_third_line_start.column != 1 || at_second_line_col3 != 8 || at_third_line_col2 != 12) {
        std::cerr << "unexpected source locate/offset mapping\n";
        return 1;
    }

    return 0;
}

int run_ok_basic(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root, "ok-app", entry, ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend, *input.input);

    if (result.has_errors()) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    // NOTE: source count and import count are intentionally lower-bounded, not
    // hardcoded. The prelude (auto-injected for every non-std module) grows as
    // new std sub-modules are re-exported (e.g. std::string / std::cmp /
    // std::fmt with P6a-01/P6a-02/P6a-03), and each such addition pulls in
    // its transitive stdlib imports. Pinning an exact number would force the
    // test to be re-tweaked on every stdlib expansion; a lower bound plus a
    // spot-check of required modules preserves the intent of the test (the
    // graph is non-trivial and contains everything the app needs) without
    // being fragile in that way.
    if (result.graph.entry_sources.size() != 1 || result.graph.sources.size() < 3 ||
        result.graph.import_edges.size() < 2) {
        std::cerr << "unexpected source graph shape: entries=" << result.graph.entry_sources.size()
                  << " sources=" << result.graph.sources.size()
                  << " imports=" << result.graph.import_edges.size() << '\n';
        return 1;
    }

    if (!result.graph.module_to_source.contains("app::main") ||
        !result.graph.module_to_source.contains("lib::types") ||
        !result.graph.module_to_source.contains("std::option")) {
        std::cerr << "missing expected module ownership entries\n";
        return 1;
    }

    return 0;
}

int run_fail_missing(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root, "missing-app", entry, ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend, *input.input);

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);

    if (!contains_message(result.diagnostics,
                          "failed to resolve imported module 'missing::types'") ||
        !contains_text(rendered, "missing/app/main.ahfl:2:1")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_mismatch(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root, "mismatch-app", entry, ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend, *input.input);

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);

    if (!contains_message(
            result.diagnostics,
            "source file declares module 'lib::wrong' but import requested 'lib::types'") ||
        !contains_text(rendered, "mismatch/lib/types.ahfl:1:1") ||
        !contains_text(rendered, "mismatch/app/main.ahfl:2:1")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_no_module(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root, "no-module-app", entry, ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend, *input.input);

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);
    if (!contains_message(result.diagnostics,
                          "project-aware source file must declare exactly one module") ||
        !contains_text(rendered, "no_module/lib/types.ahfl:1:1") ||
        !contains_text(rendered, "no_module/app/main.ahfl:2:1")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_duplicate_owner(const std::filesystem::path &entry,
                             const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    auto input = project_input_with_repo_std(entry, root);
    input.entry_files.push_back(root / "alt" / "types.ahfl");
    const auto result = ahfl::parse_project(frontend, input);

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);
    if (!contains_message(result.diagnostics, "duplicate module owner for 'lib::types'") ||
        !contains_text(rendered, "duplicate_owner/alt/types.ahfl:1:1") ||
        !contains_text(rendered, "duplicate_owner/lib/types.ahfl:1:1")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_std_import_requires_explicit_module_root(const std::filesystem::path &workspace_root) {
    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!std::filesystem::create_directories(workspace_root / "app", error) || error) {
        std::cerr << "failed to create std import fixture: " << workspace_root << '\n';
        return 1;
    }

    const auto entry = workspace_root / "app" / "main.ahfl";
    if (!write_text_file(entry,
                         "module app::main;\n"
                         "import std::option as option;\n"
                         "struct UsesStd { value: option::Option<Int>; }\n")) {
        std::cerr << "failed to write std import fixture\n";
        std::filesystem::remove_all(workspace_root, error);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend,
                                            ahfl::ProjectInput{
                                                .entry_files = {entry},
                                                .search_roots = {workspace_root},
                                            });

    std::filesystem::remove_all(workspace_root, error);

    if (!result.has_errors() ||
        !contains_message(result.diagnostics, "failed to resolve imported module 'std::option'")) {
        print_diagnostics(result.diagnostics);
        std::cerr << "expected std import to require an explicit module root\n";
        return 1;
    }

    return 0;
}

int run_package_dependency_gates_imports(const std::filesystem::path &workspace_root) {
    const auto app_root = workspace_root / "app";
    const auto lib_root = workspace_root / "lib";

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!std::filesystem::create_directories(app_root, error) || error ||
        !std::filesystem::create_directories(lib_root, error) || error) {
        std::cerr << "failed to create package dependency fixture: " << workspace_root << '\n';
        return 1;
    }

    const auto app_main = app_root / "main.ahfl";
    if (!write_text_file(app_main,
                         "module app::main;\n"
                         "import lib::api as lib_api;\n") ||
        !write_text_file(lib_root / "api.ahfl", "module lib::api;\n")) {
        std::cerr << "failed to write package dependency fixture\n";
        return 1;
    }

    const auto make_input = [&](std::vector<std::string> app_dependencies) {
        ahfl::ProjectInput input;
        input.entry_files.push_back(app_main);
        input.inject_prelude = false;
        input.enforce_package_dependencies = true;
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "app",
            .root = app_root,
            .exported_modules = {"main"},
            .dependency_prefixes = std::move(app_dependencies),
        });
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "lib",
            .root = lib_root,
            .exported_modules = {"api"},
        });
        return input;
    };

    const ahfl::Frontend frontend;
    const auto missing_dependency = ahfl::parse_project(frontend, make_input({}));
    if (!missing_dependency.has_errors() ||
        !contains_code(missing_dependency.diagnostics, "E::package_dependency_missing") ||
        !contains_message(missing_dependency.diagnostics,
                          "package prefix 'app' does not depend on package prefix 'lib'")) {
        print_diagnostics(missing_dependency.diagnostics);
        std::cerr << "expected missing package dependency diagnostic\n";
        return 1;
    }

    const auto declared_dependency = ahfl::parse_project(frontend, make_input({"lib"}));
    if (declared_dependency.has_errors()) {
        print_diagnostics(declared_dependency.diagnostics);
        return 1;
    }

    if (!declared_dependency.graph.module_to_source.contains("app::main") ||
        !declared_dependency.graph.module_to_source.contains("lib::api")) {
        std::cerr << "expected declared dependency import to load both modules\n";
        return 1;
    }

    return 0;
}

int run_std_package_dependency_gates_imports(const std::filesystem::path &workspace_root) {
    const auto app_root = workspace_root / "app";
    const auto std_root = workspace_root / "std";

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!std::filesystem::create_directories(app_root, error) || error ||
        !std::filesystem::create_directories(std_root, error) || error) {
        std::cerr << "failed to create std dependency fixture: " << workspace_root << '\n';
        return 1;
    }

    const auto app_main = app_root / "main.ahfl";
    if (!write_text_file(app_main,
                         "module app::main;\n"
                         "import std::collections as collections;\n"
                         "struct UsesStd { value: collections::List<Int>; }\n") ||
        !write_text_file(std_root / "collections.ahfl",
                         "module std::collections;\n"
                         "struct List<T> {}\n")) {
        std::cerr << "failed to write std dependency fixture\n";
        return 1;
    }

    const auto make_input = [&](std::vector<std::string> app_dependencies) {
        ahfl::ProjectInput input;
        input.entry_files.push_back(app_main);
        input.inject_prelude = false;
        input.enforce_package_dependencies = true;
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "app",
            .root = app_root,
            .exported_modules = {"main"},
            .dependency_prefixes = std::move(app_dependencies),
        });
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "std",
            .root = std_root,
            .exported_modules = {"collections"},
        });
        return input;
    };

    const ahfl::Frontend frontend;
    const auto missing_std_dependency = ahfl::parse_project(frontend, make_input({}));
    if (!missing_std_dependency.has_errors() ||
        !contains_code(missing_std_dependency.diagnostics, "E::package_dependency_missing") ||
        !contains_message(missing_std_dependency.diagnostics,
                          "package prefix 'app' does not depend on package prefix 'std'")) {
        print_diagnostics(missing_std_dependency.diagnostics);
        std::cerr << "expected missing std dependency diagnostic\n";
        return 1;
    }

    const auto declared_std_dependency = ahfl::parse_project(frontend, make_input({"std"}));
    if (declared_std_dependency.has_errors()) {
        print_diagnostics(declared_std_dependency.diagnostics);
        return 1;
    }

    if (!declared_std_dependency.graph.module_to_source.contains("app::main") ||
        !declared_std_dependency.graph.module_to_source.contains("std::collections")) {
        std::cerr << "expected declared std dependency import to load both modules\n";
        return 1;
    }

    return 0;
}

// KR6.9-B4 fix-forward: a source reachable only through a SYMLINKED DIRECTORY
// must parse. The freeze walk must follow directory symlinks (guarded against
// cycles), exactly as the direct pipeline's `exists(candidate)` gate did.
// Asserts byte-identical parse shape against a real-directory control tree.
int run_symlinked_directory_import(const std::filesystem::path &workspace_root) {
    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);

    // The symlink target lives OUTSIDE every search root: otherwise the freeze
    // walk would enumerate the target directly and the fixture would not pin
    // symlink-directory descent. Only `app/lib` (a symlink) reaches it.
    const auto external_lib = workspace_root / "external_lib";
    std::filesystem::create_directories(external_lib, error);
    if (error || !write_text_file(external_lib / "helper.ahfl", "module app::lib::helper;\n")) {
        std::cerr << "failed to create external symlink target fixture\n";
        return 1;
    }

    const auto symlink_tree = workspace_root / "symlinked";
    std::filesystem::create_directories(symlink_tree / "app", error);
    const auto real_tree = workspace_root / "real";
    std::filesystem::create_directories(real_tree / "app" / "lib", error);
    if (error ||
        !write_text_file(symlink_tree / "app" / "main.ahfl",
                         "module app::main;\n"
                         "import app::lib::helper;\n") ||
        !write_text_file(real_tree / "app" / "main.ahfl",
                         "module app::main;\n"
                         "import app::lib::helper;\n") ||
        !write_text_file(real_tree / "app" / "lib" / "helper.ahfl", "module app::lib::helper;\n")) {
        std::cerr << "failed to create symlinked-directory fixture\n";
        return 1;
    }

    std::filesystem::create_directory_symlink(external_lib, symlink_tree / "app" / "lib", error);
    if (error) {
        // Filesystem cannot create directory symlinks here: the regression is
        // untestable in this environment, not a product failure.
        std::printf("SKIP: symlinked-directory import (no symlink support)\n");
        std::filesystem::remove_all(workspace_root, error);
        return 0;
    }

    // Each tree is its own sole search root, so the external target is reached
    // only through the symlinked directory.
    const auto parse_tree = [](const std::filesystem::path &tree) {
        ahfl::ProjectInput input;
        input.entry_files.push_back(tree / "app" / "main.ahfl");
        input.search_roots.push_back(tree);
        input.inject_prelude = false;
        const ahfl::Frontend frontend;
        return std::pair{ahfl::resolve_project_input(input), ahfl::parse_project(frontend, input)};
    };

    auto [symlink_model, symlink_result] = parse_tree(symlink_tree);
    auto [real_model_unused, real_result] = parse_tree(real_tree);
    static_cast<void>(real_model_unused);

    // The freeze walk must collect the source through the symlinked directory
    // (its canonical path), so resolution needs no disk fallback here.
    bool model_sees_symlinked_helper = false;
    for (const auto &source : symlink_model.sources) {
        if (source.exists_on_disk && source.path.filename() == "helper.ahfl") {
            model_sees_symlinked_helper = true;
            break;
        }
    }

    const bool ok = model_sees_symlinked_helper && !symlink_result.has_errors() &&
                    !real_result.has_errors() &&
                    symlink_result.graph.module_to_source.contains("app::main") &&
                    symlink_result.graph.module_to_source.contains("app::lib::helper") &&
                    real_result.graph.module_to_source.contains("app::main") &&
                    real_result.graph.module_to_source.contains("app::lib::helper") &&
                    symlink_result.graph.sources.size() == real_result.graph.sources.size();

    std::filesystem::remove_all(workspace_root, error);

    if (!ok) {
        std::cerr << "symlinked-directory import diverged from the real-directory control\n";
        if (!model_sees_symlinked_helper) {
            std::cerr << "model did not collect helper.ahfl through the symlinked directory\n";
        }
        print_diagnostics(symlink_result.diagnostics);
        print_diagnostics(real_result.diagnostics);
        return 1;
    }

    return 0;
}

// KR6.9-B4 fix-forward: an import target beneath a directory the process can
// traverse but not LIST (mode 0111) must still resolve. A tree walk cannot
// enumerate its names, so absence from the frozen snapshot must not be read as
// non-existence; the resolver replays the candidate's own `exists()` probe.
int run_unlistable_directory_import(const std::filesystem::path &workspace_root) {
    if (::geteuid() == 0) {
        // Root bypasses directory permissions, so the fixture would not exercise
        // the fallback.
        std::printf("SKIP: unlistable-directory import (running as root)\n");
        return 0;
    }

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    std::filesystem::create_directories(workspace_root / "app" / "lib", error);
    if (error) {
        std::cerr << "failed to create unlistable directory fixture\n";
        return 1;
    }

    const auto main_path = workspace_root / "app" / "main.ahfl";
    const auto helper_path = workspace_root / "app" / "lib" / "helper.ahfl";
    if (!write_text_file(main_path,
                         "module app::main;\n"
                         "import app::lib::helper;\n") ||
        !write_text_file(helper_path, "module app::lib::helper;\n")) {
        std::cerr << "failed to write unlistable directory fixture\n";
        return 1;
    }

    // Traversable (execute) but not listable (no read): a direct `exists()` /
    // open on the child still works, while a directory walk skips the parent.
    std::filesystem::permissions(workspace_root / "app" / "lib",
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::replace,
                                 error);
    if (error) {
        std::printf("SKIP: unlistable-directory import (cannot restrict permissions)\n");
        std::filesystem::permissions(workspace_root / "app" / "lib",
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace,
                                     error);
        std::filesystem::remove_all(workspace_root, error);
        return 0;
    }

    ahfl::ProjectInput input;
    input.entry_files.push_back(main_path);
    input.search_roots.push_back(workspace_root);
    input.inject_prelude = false;

    const auto model = ahfl::resolve_project_input(input);
    // Validate the premise: the non-listable directory's child is absent from
    // the frozen snapshot (a walk cannot enumerate it)...
    bool model_misses_helper = true;
    for (const auto &source : model.sources) {
        if (source.path.filename() == "helper.ahfl") {
            model_misses_helper = false;
            break;
        }
    }

    const ahfl::Frontend frontend;
    const auto result = ahfl::parse_project(frontend, model);

    // Restore permissions before any cleanup / diagnostic rendering.
    std::filesystem::permissions(workspace_root / "app" / "lib",
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace,
                                 error);
    std::filesystem::remove_all(workspace_root, error);

    // ...yet the import must resolve exactly as the direct pipeline resolved it.
    if (!model_misses_helper || result.has_errors() ||
        !result.graph.module_to_source.contains("app::main") ||
        !result.graph.module_to_source.contains("app::lib::helper")) {
        std::cerr << "import beneath a non-listable directory did not resolve\n";
        if (!model_misses_helper) {
            std::cerr << "fixture premise failed: helper.ahfl was enumerable\n";
        }
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: project_parse <case> <path> [root]\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path entry = argv[2];
    const std::filesystem::path root =
        argc >= 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{};

    if (test_case == "ok-basic") {
        return run_ok_basic(entry, root);
    }

    if (test_case == "fail-missing") {
        return run_fail_missing(entry, root);
    }

    if (test_case == "fail-mismatch") {
        return run_fail_mismatch(entry, root);
    }

    if (test_case == "fail-no-module") {
        return run_fail_no_module(entry, root);
    }

    if (test_case == "fail-duplicate-owner") {
        return run_fail_duplicate_owner(entry, root);
    }

    if (test_case == "std-import-requires-explicit-module-root") {
        return run_std_import_requires_explicit_module_root(entry);
    }

    if (test_case == "package-dependency-gates-imports") {
        return run_package_dependency_gates_imports(entry);
    }

    if (test_case == "std-package-dependency-gates-imports") {
        return run_std_package_dependency_gates_imports(entry);
    }

    if (test_case == "symlinked-directory-import") {
        return run_symlinked_directory_import(entry);
    }

    if (test_case == "unlistable-directory-import") {
        return run_unlistable_directory_import(entry);
    }

    if (test_case == "diagnostics-support-metadata-smoke") {
        return run_diagnostics_support_metadata_smoke();
    }

    if (test_case == "source-file-position-smoke") {
        return run_source_file_position_smoke();
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
