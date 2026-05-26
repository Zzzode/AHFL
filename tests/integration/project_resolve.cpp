#include "ahfl/frontend/frontend.hpp"
#include "ahfl/semantics/resolver.hpp"

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

[[nodiscard]] bool contains_text(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

int run_ok_basic(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    if (!resolve_result.symbol_table.find_canonical(ahfl::SymbolNamespace::Types,
                                                    "app::main::UsesRequest") ||
        !resolve_result.symbol_table.find_canonical(ahfl::SymbolNamespace::Types,
                                                    "lib::types::Request")) {
        std::cerr << "missing expected canonical type symbols\n";
        return 1;
    }

    return 0;
}

int run_ok_duplicate_locals(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    if (!resolve_result.symbol_table.find_canonical(ahfl::SymbolNamespace::Types,
                                                    "left::types::Request") ||
        !resolve_result.symbol_table.find_canonical(ahfl::SymbolNamespace::Types,
                                                    "right::types::Request")) {
        std::cerr << "expected duplicate local type names across modules to be preserved\n";
        return 1;
    }

    return 0;
}

int run_fail_unknown_type(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (!resolve_result.has_errors()) {
        std::cerr << "expected resolver failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(resolve_result.diagnostics);
    if (!contains_text(rendered, "unknown type 'types::Missing'") ||
        !contains_text(rendered, "resolve_error/app/main.ahfl:5:14")) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: project_resolve <case> <entry> <root>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path entry = argv[2];
    const std::filesystem::path root = argv[3];

    if (test_case == "ok-basic") {
        return run_ok_basic(entry, root);
    }

    if (test_case == "ok-duplicate-locals") {
        return run_ok_duplicate_locals(entry, root);
    }

    if (test_case == "fail-unknown-type") {
        return run_fail_unknown_type(entry, root);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
