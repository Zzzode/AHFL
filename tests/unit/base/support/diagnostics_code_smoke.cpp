// ============================================================================
// Smoke tests for cross-module related-note rendering (CLI text + JSON lines).
// ============================================================================

#include "ahfl/base/support/diagnostics.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

void check_contains(std::string_view haystack,
                    std::string_view needle,
                    const std::string &test_name) {
    check(haystack.find(needle) != std::string_view::npos, test_name);
}

// g-4 Phase 2 CLI golden: verify that a Diagnostic whose related notes carry
// cross-module source metadata renders with the expected indented "note:"
// lines (DiagnosticBag::render drives CLI text printing for ahflc --check)
// and that the serialized JSON lines (mirroring JsonDiagnosticConsumer) emit
// a relatedInformation array so CI log parsers can consume it.
void test_related_notes_render_cli_text_and_json() {
    using namespace ahfl;
    using namespace ahfl::error_codes::resolve;

    // Primary diagnostic carries TWO related notes: one in the same source
    // (no line/column because no SourceFile is registered) and one in a
    // different module whose location we express as a source label.
    DiagnosticBag bag;
    std::move(bag.error()
                  .code(MultipleModuleDeclarations)
                  .message("multiple module declarations are not supported in one source file")
                  .source_name("app/main.ahfl", SourcePosition{2, 1})
                  .with_note("first module declaration is here",
                             /* range */ std::nullopt,
                             /* source_id */ std::nullopt,
                             std::optional<std::string>{"app/main.ahfl"})
                  .with_note("other declaration in module 'lib::shared'",
                             /* range */ std::nullopt,
                             /* source_id */ std::nullopt,
                             std::optional<std::string>{"lib/shared.ahfl"}))
        .emit();

    // --- Text rendering ---
    {
        std::ostringstream out;
        bag.render(out, std::nullopt, /*include_code=*/true);
        const auto text = out.str();
        // Each indented note line must be present with the 2-space "note:"
        // prefix and the correct cross-module source label.
        check_contains(text, "  note: first module declaration is here (app/main.ahfl)",
                       "cliText.first_note_with_source_label");
        check_contains(
            text, "  note: other declaration in module 'lib::shared' (lib/shared.ahfl)",
            "cliText.cross_module_note_label");
    }

    // --- JSON lines rendering (inline mirror of JsonDiagnosticConsumer) ---
    // We manually produce the same output shape that the CLI --format json
    // consumer emits so the golden is pinned without pulling the CLI
    // toolchain into the base/support test target.
    {
        std::ostringstream out;
        for (const auto &diag : bag.entries()) {
            out << "{\"severity\":\"" << to_string(diag.severity) << "\",\"message\":\""
                << diag.message << "\"";
            if (diag.code.has_value()) {
                out << ",\"code\":\"" << *diag.code << "\"";
            }
            if (diag.source_name.has_value()) {
                out << ",\"file\":\"" << *diag.source_name << "\"";
                if (diag.position.has_value()) {
                    out << ",\"line\":" << diag.position->line
                        << ",\"column\":" << diag.position->column;
                }
            }
            if (!diag.related.empty()) {
                out << ",\"relatedInformation\":[";
                for (std::size_t i = 0; i < diag.related.size(); ++i) {
                    if (i != 0) out << ",";
                    const auto &r = diag.related[i];
                    out << "{\"message\":\"" << r.message << "\"";
                    const std::optional<std::string_view> file =
                        r.source_name.has_value()
                            ? std::optional<std::string_view>{*r.source_name}
                            : (diag.source_name.has_value()
                                   ? std::optional<std::string_view>{*diag.source_name}
                                   : std::nullopt);
                    if (file.has_value() || r.range.has_value()) {
                        out << ",\"location\":{";
                        if (file.has_value()) {
                            out << "\"file\":\"" << *file << "\"";
                        }
                        out << "}";
                    }
                    out << "}";
                }
                out << "]";
            }
            out << "}\n";
        }
        const auto jsonl = out.str();
        check_contains(jsonl, "\"relatedInformation\"",
                       "cliJson.emits_related_information_array");
        check_contains(jsonl, "other declaration in module",
                       "cliJson.related_message_round_trips");
        check_contains(jsonl, "\"file\":\"lib/shared.ahfl\"",
                       "cliJson.related_cross_module_file_label");
    }
}

} // namespace

int main() {
    test_related_notes_render_cli_text_and_json();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
