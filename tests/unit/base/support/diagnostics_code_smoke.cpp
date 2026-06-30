// ============================================================================
// Smoke tests for P4 DECREASES diagnostic codes and format templates.
//
// Scope: verify that all 6 DECREASES-family error codes are registered in the
// diagnostic error code table, that their message templates format to the
// expected shape, and the two mandated per-code severity defaults
// (IN_NON_PURE = error, SHADOWED_RECEIVER = warning) round-trip through the
// DiagnosticBag rendering pipeline.
//
// These are intentionally light "smoke" tests: they do NOT prove that the
// compiler ever emits the diagnostics. That is the job of the later
// semantic-hardening milestones.
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

// ============================================================================
// 1. Error codes are present and produce the expected full_code() strings.
// ============================================================================

void test_six_codes_registered() {
    using namespace ahfl::error_codes::typecheck;

    check(DecreasesExpectsNumeric.full_code() == "typecheck.DECREASES_EXPECTS_NUMERIC",
          "diag_code.DecreasesExpectsNumeric");
    check(DecreasesExpectsPure.full_code() == "typecheck.DECREASES_EXPECTS_PURE",
          "diag_code.DecreasesExpectsPure");
    check(DecreasesIllegalOwner.full_code() == "typecheck.DECREASES_ILLEGAL_OWNER",
          "diag_code.DecreasesIllegalOwner");
    check(DecreasesDuplicate.full_code() == "typecheck.DECREASES_DUPLICATE",
          "diag_code.DecreasesDuplicate");
    check(InNonPure.full_code() == "typecheck.IN_NON_PURE", "diag_code.InNonPure");
    check(ShadowedReceiver.full_code() == "typecheck.SHADOWED_RECEIVER",
          "diag_code.ShadowedReceiver");
}

// ============================================================================
// 2. All 6 message templates produce non-empty formatted strings.
//    We do not pin the exact wording for every placeholder (that would make
//    the tests brittle), we only check that the {}-placeholders expand and
//    that a stable key phrase survives.
// ============================================================================

void test_six_templates_format() {
    using namespace ahfl::messages::typecheck;

    auto s1 = DecreasesExpectsNumeric.format_with("Bool");
    check_contains(s1, "Int", "fmt.DecreasesExpectsNumeric.mentions_Int");
    check_contains(s1, "Bool", "fmt.DecreasesExpectsNumeric.mentions_got");

    auto s2 = DecreasesExpectsPure.format_with("capability call");
    check_contains(s2, "pure", "fmt.DecreasesExpectsPure.mentions_pure");
    check_contains(s2, "capability call", "fmt.DecreasesExpectsPure.mentions_effect");

    auto s3 = DecreasesIllegalOwner.format_with("agent", "MyAgent");
    check_contains(s3, "predicates", "fmt.DecreasesIllegalOwner.mentions_predicates");
    check_contains(s3, "MyAgent", "fmt.DecreasesIllegalOwner.mentions_owner");

    auto s4 = DecreasesDuplicate.format_with("factorial");
    check_contains(s4, "DECREASES", "fmt.DecreasesDuplicate.mentions_keyword");
    check_contains(s4, "factorial", "fmt.DecreasesDuplicate.mentions_callable");

    auto s5 = InNonPure.format_with("external effect");
    check_contains(s5, "invariant", "fmt.InNonPure.mentions_invariant");
    check_contains(s5, "external effect", "fmt.InNonPure.mentions_effect");

    auto s6 = ShadowedReceiver.format_with("n", "x");
    check_contains(s6, "shadows", "fmt.ShadowedReceiver.mentions_shadow");
    check_contains(s6, "termination", "fmt.ShadowedReceiver.mentions_termination");
    check_contains(s6, "x", "fmt.ShadowedReceiver.mentions_receiver");
}

// ============================================================================
// 3. IN_NON_PURE: severity = error, renders with code + message.
// ============================================================================

void test_in_non_pure_severity_and_render() {
    using namespace ahfl;
    namespace ec = ahfl::error_codes::typecheck;
    namespace msg = ahfl::messages::typecheck;

    DiagnosticBag bag;
    std::move(bag.error()
                  .code(ec::InNonPure)
                  .message(msg::InNonPure, "predicate call"))
        .emit();

    check(bag.has_error(), "smoke.InNonPure.has_error_true");
    check(bag.error_count() == 1, "smoke.InNonPure.error_count_1");
    check(bag.warning_count() == 0, "smoke.InNonPure.warning_count_0");

    const auto &entries = bag.entries();
    check(entries.size() == 1, "smoke.InNonPure.one_entry");
    check(entries[0].severity == DiagnosticSeverity::Error,
          "smoke.InNonPure.severity_error");
    check(entries[0].code.has_value() && *entries[0].code == "typecheck.IN_NON_PURE",
          "smoke.InNonPure.code_string");

    // Render with include_code=true and confirm both severity and code appear.
    std::ostringstream out;
    bag.render(out, std::nullopt, /*include_code=*/true);
    auto rendered = out.str();
    check_contains(rendered, "error", "smoke.InNonPure.render_has_severity");
    check_contains(rendered, "typecheck.IN_NON_PURE",
                   "smoke.InNonPure.render_has_code");
    check_contains(rendered, "invariant", "smoke.InNonPure.render_has_message_key");
}

// ============================================================================
// 4. SHADOWED_RECEIVER: severity = warning, renders with code + message.
// ============================================================================

void test_shadowed_receiver_severity_and_render() {
    using namespace ahfl;
    namespace ec = ahfl::error_codes::typecheck;
    namespace msg = ahfl::messages::typecheck;

    DiagnosticBag bag;
    std::move(bag.warning()
                  .code(ec::ShadowedReceiver)
                  .message(msg::ShadowedReceiver, "inner", "counter"))
        .emit();

    check(!bag.has_error(), "smoke.ShadowedReceiver.no_error");
    check(bag.has_warning(), "smoke.ShadowedReceiver.has_warning_true");
    check(bag.warning_count() == 1, "smoke.ShadowedReceiver.warning_count_1");
    check(bag.error_count() == 0, "smoke.ShadowedReceiver.error_count_0");

    const auto &entries = bag.entries();
    check(entries.size() == 1, "smoke.ShadowedReceiver.one_entry");
    check(entries[0].severity == DiagnosticSeverity::Warning,
          "smoke.ShadowedReceiver.severity_warning");
    check(entries[0].code.has_value() &&
              *entries[0].code == "typecheck.SHADOWED_RECEIVER",
          "smoke.ShadowedReceiver.code_string");

    std::ostringstream out;
    bag.render(out, std::nullopt, /*include_code=*/true);
    auto rendered = out.str();
    check_contains(rendered, "warning", "smoke.ShadowedReceiver.render_has_severity");
    check_contains(rendered, "typecheck.SHADOWED_RECEIVER",
                   "smoke.ShadowedReceiver.render_has_code");
    check_contains(rendered, "shadows", "smoke.ShadowedReceiver.render_has_message_key");
    check_contains(rendered, "counter", "smoke.ShadowedReceiver.render_has_receiver");
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
    test_six_codes_registered();
    test_six_templates_format();
    test_in_non_pure_severity_and_render();
    test_shadowed_receiver_severity_and_render();
    test_related_notes_render_cli_text_and_json();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
