#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

int run_fail_manifest_duplicate_field(const std::filesystem::path &descriptor) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_project_descriptor(descriptor);

    if (!result.has_errors()) {
        std::cerr << "expected descriptor duplicate field parse failure\n";
        return 1;
    }

    if (!contains_message(result.diagnostics, "project descriptor must be valid JSON")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_workspace_duplicate_project_name(const std::filesystem::path &workspace) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_project_descriptor_from_workspace(workspace, "dup-project");

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

int run_ok_package_authoring(const std::filesystem::path &descriptor) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_package_authoring_descriptor(descriptor);

    if (result.has_errors() || !result.descriptor.has_value()) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    const auto &parsed = *result.descriptor;
    if (parsed.format_version != ahfl::kPackageAuthoringFormatVersion ||
        parsed.package_name != "refund-audit" || parsed.package_version != "0.1.0" ||
        parsed.entry.kind != ahfl::PackageAuthoringTargetKind::Workflow ||
        parsed.entry.name != "app::main::RefundAuditWorkflow" || parsed.exports.size() != 2 ||
        parsed.capability_bindings.size() != 2) {
        std::cerr << "unexpected package authoring descriptor shape\n";
        return 1;
    }

    if (parsed.exports[0].kind != ahfl::PackageAuthoringTargetKind::Workflow ||
        parsed.exports[0].name != "app::main::RefundAuditWorkflow" ||
        parsed.exports[1].kind != ahfl::PackageAuthoringTargetKind::Agent ||
        parsed.exports[1].name != "lib::agents::RefundAudit") {
        std::cerr << "unexpected export targets\n";
        return 1;
    }

    if (parsed.capability_bindings[0].capability != "OrderQuery" ||
        parsed.capability_bindings[0].binding_key != "order.query" ||
        parsed.capability_bindings[1].capability != "AuditDecision" ||
        parsed.capability_bindings[1].binding_key != "audit.decision") {
        std::cerr << "unexpected capability bindings\n";
        return 1;
    }

    return 0;
}

int run_fail_package_unsupported_format(const std::filesystem::path &descriptor) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_package_authoring_descriptor(descriptor);

    if (!result.has_errors()) {
        std::cerr << "expected package authoring descriptor parse failure\n";
        return 1;
    }

    if (!contains_message(result.diagnostics,
                          "unsupported package authoring descriptor format_version")) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_package_duplicate_binding_key(const std::filesystem::path &descriptor) {
    const ahfl::Frontend frontend;
    const auto result = frontend.load_package_authoring_descriptor(descriptor);

    if (!result.has_errors()) {
        std::cerr << "expected package authoring descriptor parse failure\n";
        return 1;
    }

    if (!contains_message(result.diagnostics,
                          "package authoring descriptor contains duplicate binding_key")) {
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

    if (test_case == "ok-project-stdlib-root-wins-over-bundled-copy") {
        return run_ok_project_stdlib_root_wins_over_bundled_copy(entry);
    }

    if (test_case == "fail-manifest-escape") {
        return run_fail_manifest_escape(entry);
    }

    if (test_case == "fail-manifest-duplicate-field") {
        return run_fail_manifest_duplicate_field(entry);
    }

    if (test_case == "fail-workspace-duplicate-project-name") {
        return run_fail_workspace_duplicate_project_name(entry);
    }

    if (test_case == "ok-package-authoring") {
        return run_ok_package_authoring(entry);
    }

    if (test_case == "fail-package-unsupported-format") {
        return run_fail_package_unsupported_format(entry);
    }

    if (test_case == "fail-package-duplicate-binding-key") {
        return run_fail_package_duplicate_binding_key(entry);
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
