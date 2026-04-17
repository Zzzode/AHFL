#include "ahfl/frontend/frontend.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void print_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cout);
}

[[nodiscard]] std::string render_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    std::ostringstream out;
    diagnostics.render(out);
    return out.str();
}

[[nodiscard]] bool contains_message(const ahfl::DiagnosticBag &diagnostics, std::string_view needle) {
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

int run_ok_basic(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });

    if (result.has_errors()) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    if (result.graph.entry_sources.size() != 1 || result.graph.sources.size() != 2 ||
        result.graph.import_edges.size() != 1) {
        std::cerr << "unexpected source graph shape\n";
        return 1;
    }

    if (!result.graph.module_to_source.contains("app::main") ||
        !result.graph.module_to_source.contains("lib::types")) {
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
    });

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);

    if (!contains_message(result.diagnostics, "failed to resolve imported module 'missing::types'") ||
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
    });

    if (!result.has_errors()) {
        std::cerr << "expected project parse failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);

    if (!contains_message(result.diagnostics,
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

int run_fail_duplicate_owner(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry, root / "alt" / "types.ahfl"},
        .search_roots = {root},
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

int run_fail_manifest_escape(const std::filesystem::path &descriptor) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_project_descriptor(descriptor);

    if (!result.has_errors()) {
        std::cerr << "expected descriptor parse failure\n";
        return 1;
    }

    if (!contains_message(result.diagnostics,
                          "descriptor field 'search_roots' must not escape the descriptor root")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_workspace_duplicate_project_name(const std::filesystem::path &workspace) {
    const ahfl::Frontend frontend;
    const auto result =
        frontend.load_project_descriptor_from_workspace(workspace, "dup-project");

    if (!result.has_errors()) {
        std::cerr << "expected workspace selection failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(result.diagnostics);
    if (!contains_message(result.diagnostics,
                          "workspace contains duplicate project name 'dup-project'") ||
        !contains_text(rendered, "workspace_duplicate/left/ahfl.project.json") ||
        !contains_text(rendered, "workspace_duplicate/right/ahfl.project.json")) {
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
    const std::filesystem::path root = argc >= 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{};

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

    if (test_case == "fail-manifest-escape") {
        return run_fail_manifest_escape(entry);
    }

    if (test_case == "fail-workspace-duplicate-project-name") {
        return run_fail_workspace_duplicate_project_name(entry);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
