#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void print_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cout);
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
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
        .inject_prelude = true,
    });

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
    if (result.graph.entry_sources.size() != 1 || result.graph.sources.size() < 6 ||
        result.graph.import_edges.size() < 8) {
        std::cerr << "unexpected source graph shape: entries=" << result.graph.entry_sources.size()
                  << " sources=" << result.graph.sources.size()
                  << " imports=" << result.graph.import_edges.size() << '\n';
        return 1;
    }

    if (!result.graph.module_to_source.contains("app::main") ||
        !result.graph.module_to_source.contains("lib::types") ||
        !result.graph.module_to_source.contains("std::prelude") ||
        !result.graph.module_to_source.contains("std::option") ||
        !result.graph.module_to_source.contains("std::result") ||
        !result.graph.module_to_source.contains("std::collections") ||
        !result.graph.module_to_source.contains("std::string") ||
        !result.graph.module_to_source.contains("std::cmp") ||
        !result.graph.module_to_source.contains("std::fmt")) {
        std::cerr << "missing expected module ownership entries\n";
        return 1;
    }

    return 0;
}

int run_fail_missing(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
        .inject_prelude = true,
    });

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
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
        .inject_prelude = true,
    });

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
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
        .inject_prelude = true,
    });

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
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry, root / "alt" / "types.ahfl"},
        .search_roots = {root},
        .inject_prelude = true,
    });

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

int run_ok_project_stdlib_root_wins_over_bundled_copy(const std::filesystem::path &source_root) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto bundle_root = std::filesystem::temp_directory_path() /
                             ("ahfl-project-parse-bundled-stdlib-" + std::to_string(stamp));
    const auto bundle_std = bundle_root / "std";

    std::error_code error;
    std::filesystem::remove_all(bundle_root, error);
    if (!std::filesystem::create_directories(bundle_std, error) || error) {
        std::cerr << "failed to create bundled stdlib fixture: " << bundle_std << '\n';
        return 1;
    }

    if (!write_text_file(bundle_std / "ahfl.toml",
                         "manifest_version = 1\n\n"
                         "[package]\n"
                         "name = \"std\"\n"
                         "version = \"0.1.0\"\n"
                         "edition = \"2026\"\n"
                         "kind = \"standard-library\"\n\n"
                         "[module]\n"
                         "prefix = \"std\"\n"
                         "root = \".\"\n\n"
                         "[prelude]\n"
                         "module = \"std::prelude\"\n"
                         "injection = \"explicit\"\n\n"
                         "[exports]\n"
                         "modules = [\"prelude\", \"option\"]\n\n"
                         "[compiler_intrinsics]\n"
                         "allow = [\"option_*\"]\n") ||
        !write_text_file(bundle_std / "prelude.ahfl", "module std::prelude;\n") ||
        !write_text_file(bundle_std / "option.ahfl",
                         "module std::option;\n"
                         "enum Option<T> { Some(T), None, }\n")) {
        std::cerr << "failed to write bundled stdlib fixture\n";
        std::filesystem::remove_all(bundle_root, error);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {source_root / "std" / "collections.ahfl"},
        .search_roots = {source_root},
        .stdlib_search_roots = {bundle_root},
    });

    std::filesystem::remove_all(bundle_root, error);

    if (result.has_errors()) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    const auto option_source = result.graph.module_to_source.find("std::option");
    if (option_source == result.graph.module_to_source.end()) {
        std::cerr << "missing std::option module owner\n";
        return 1;
    }

    const auto expected = source_root / "std" / "option.ahfl";
    for (const auto &source : result.graph.sources) {
        if (source.id != option_source->second) {
            continue;
        }
        if (!std::filesystem::equivalent(source.path, expected, error) || error) {
            std::cerr << "std::option resolved to unexpected path: " << source.path << " expected "
                      << expected << '\n';
            return 1;
        }
        return 0;
    }

    std::cerr << "std::option source id did not map to a loaded source\n";
    return 1;
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
        input.include_stdlib = false;
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
    const auto missing_dependency = frontend.parse_project(make_input({}));
    if (!missing_dependency.has_errors() ||
        !contains_message(missing_dependency.diagnostics,
                          "package prefix 'app' does not depend on package prefix 'lib'")) {
        print_diagnostics(missing_dependency.diagnostics);
        std::cerr << "expected missing package dependency diagnostic\n";
        return 1;
    }

    const auto declared_dependency = frontend.parse_project(make_input({"lib"}));
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

    if (test_case == "ok-project-stdlib-root-wins-over-bundled-copy") {
        return run_ok_project_stdlib_root_wins_over_bundled_copy(entry);
    }

    if (test_case == "package-dependency-gates-imports") {
        return run_package_dependency_gates_imports(entry);
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
