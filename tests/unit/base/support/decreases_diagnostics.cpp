#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

namespace codes = ahfl::error_codes::validation;
namespace msgs = ahfl::messages::validation;

using ahfl::DiagnosticSeverity;
using ahfl::SourcePosition;
using ahfl::SourceRange;

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

// ============================================================================
// Error code full_code and message formatting
// ============================================================================

void test_codes_exist_and_format() {
    check(codes::DecreasesNotProven.full_code() == "validation.DECREASES_NOT_PROVEN",
          "code.DecreasesNotProven.full_code");
    check(codes::DecreasesNonLex.full_code() == "validation.DECREASES_NON_LEX",
          "code.DecreasesNonLex.full_code");
    check(codes::DecreasesWildcardInvalid.full_code() == "validation.DECREASES_WILDCARD_INVALID",
          "code.DecreasesWildcardInvalid.full_code");
    check(codes::DecreasesEmpty.full_code() == "validation.DECREASES_EMPTY",
          "code.DecreasesEmpty.full_code");
    check(codes::DecreasesShadowedReceiver.full_code() == "validation.DECREASES_SHADOWED_RECEIVER",
          "code.DecreasesShadowedReceiver.full_code");
    check(codes::DecreasesInNonPure.full_code() == "validation.DECREASES_IN_NON_PURE",
          "code.DecreasesInNonPure.full_code");

    check(msgs::DecreasesNotProven.format_with("len(xs)") ==
              "cannot prove termination: measure 'len(xs)' is not strictly decreasing along all paths",
          "msg.DecreasesNotProven.format_1");

    check(msgs::DecreasesNonLex.format_with("(a, b, c)", "b: String is not an Int") ==
              "termination measure '(a, b, c)' is not a well-founded lexicographic tuple: "
              "component b: String is not an Int",
          "msg.DecreasesNonLex.format_2");

    check(msgs::DecreasesWildcardInvalid.format_with("2") ==
              "wildcard '_' is not allowed as a termination measure component (position 2)",
          "msg.DecreasesWildcardInvalid.format_1");

    check(msgs::DecreasesEmpty.format_with() ==
              "decreases clause is empty: expected at least one measure expression",
          "msg.DecreasesEmpty.format_0");

    check(msgs::DecreasesShadowedReceiver.format_with("n", "Int") ==
              "termination receiver 'n' shadows an outer binding of type 'Int' "
              "(receiver is still safe)",
          "msg.DecreasesShadowedReceiver.format_2");

    check(msgs::DecreasesInNonPure.format_with("write_log") ==
              "decreases clause is only allowed in pure predicates; 'write_log' is marked impure",
          "msg.DecreasesInNonPure.format_1");
}

// ============================================================================
// Severity assignment convention: only SHADOWED_RECEIVER is Warning
// ============================================================================

void test_severity_convention() {
    const auto convention = [](DiagnosticSeverity s) {
        return s == DiagnosticSeverity::Warning || s == DiagnosticSeverity::Error;
    };
    // Sanity-check the severity enum is still compatible with our convention.
    check(convention(DiagnosticSeverity::Warning), "severity.Warning.valid");

    ahfl::DiagnosticBag bag;
    bag.warning()
        .message(msgs::DecreasesShadowedReceiver, "self", "Rec")
        .code(codes::DecreasesShadowedReceiver)
        .emit();
    bag.error().message(msgs::DecreasesEmpty).code(codes::DecreasesEmpty).emit();
    bag.error()
        .message(msgs::DecreasesNotProven, "n")
        .code(codes::DecreasesNotProven)
        .emit();
    bag.error()
        .message(msgs::DecreasesWildcardInvalid, "1")
        .code(codes::DecreasesWildcardInvalid)
        .emit();
    bag.error()
        .message(msgs::DecreasesNonLex, "(i,j)", "j not well-ordered")
        .code(codes::DecreasesNonLex)
        .emit();
    bag.error()
        .message(msgs::DecreasesInNonPure, "emit_event")
        .code(codes::DecreasesInNonPure)
        .emit();

    check(bag.warning_count() == 1, "severity.shadowed_receiver_is_warning");
    check(bag.error_count() == 5, "severity.other_five_are_errors");
    check(bag.has_error(), "severity.bag_has_error");
    check(bag.has_warning(), "severity.bag_has_warning");
}

// ============================================================================
// Rendered output carries the diagnostic code, positioned location, message
// ============================================================================

void test_render_includes_code_and_position() {
    ahfl::DiagnosticBag bag;
    ahfl::SourceFile src{
        .display_name = "verif/term.ahfl",
        .content =
            "module verif;\n"
            "fn sum(n: Int) -> Int decreases _ { return n + sum(n - 1); }\n"
            "fn log(msg: String) -> Unit impure decreases msg { return (); }\n"
            "fn fact(x: Int) -> Int decreases { return x; }\n",
    };

    // Trigger DECREASES_WILDCARD_INVALID on the `_` in line 2.
    const SourceRange wildcard_range{
        .begin_offset = src.offset_of(2, 30),
        .end_offset = src.offset_of(2, 31),
    };
    bag.error()
        .range(wildcard_range)
        .message(msgs::DecreasesWildcardInvalid, "1")
        .code(codes::DecreasesWildcardInvalid)
        .source(src)
        .emit();

    // Trigger DECREASES_IN_NON_PURE on `log` (line 3).
    const SourceRange non_pure_range{
        .begin_offset = src.offset_of(3, 1),
        .end_offset = src.offset_of(3, 4),
    };
    bag.error()
        .range(non_pure_range)
        .message(msgs::DecreasesInNonPure, "log")
        .code(codes::DecreasesInNonPure)
        .source(src)
        .emit();

    // Trigger DECREASES_SHADOWED_RECEIVER as a warning (line 2, `n` param).
    const SourceRange shadow_range{
        .begin_offset = src.offset_of(2, 8),
        .end_offset = src.offset_of(2, 9),
    };
    bag.warning()
        .range(shadow_range)
        .message(msgs::DecreasesShadowedReceiver, "n", "Int")
        .code(codes::DecreasesShadowedReceiver)
        .source(src)
        .emit();

    std::ostringstream out;
    bag.render(out, src, /*include_code=*/true);
    const std::string rendered = out.str();

    const auto contains = [&](std::string_view needle) {
        return rendered.find(needle) != std::string::npos;
    };
    check(contains("validation.DECREASES_WILDCARD_INVALID"),
          "render.contains.code.DECREASES_WILDCARD_INVALID");
    check(contains("validation.DECREASES_IN_NON_PURE"),
          "render.contains.code.DECREASES_IN_NON_PURE");
    check(contains("validation.DECREASES_SHADOWED_RECEIVER"),
          "render.contains.code.DECREASES_SHADOWED_RECEIVER");

    check(contains("wildcard '_' is not allowed as a termination measure component (position 1)"),
          "render.contains.message.DecreasesWildcardInvalid");
    check(contains("'log' is marked impure"),
          "render.contains.message.DecreasesInNonPure");
    check(contains("termination receiver 'n' shadows an outer binding of type 'Int'"),
          "render.contains.message.DecreasesShadowedReceiver");

    check(contains("verif/term.ahfl:"), "render.contains.source_name");
    check(contains("verif/term.ahfl:2:"), "render.contains.wildcard_line_2");
    check(contains("verif/term.ahfl:3:"), "render.contains.non_pure_line_3");
}

// ============================================================================
// Direct Diagnostic construction also records codes and severities
// ============================================================================

void test_direct_diagnostic_roundtrip() {
    ahfl::Diagnostic d;
    d.severity = DiagnosticSeverity::Warning;
    d.message = msgs::DecreasesShadowedReceiver.format_with("self", "Agent");
    d.code = codes::DecreasesShadowedReceiver.full_code();
    d.position = SourcePosition{.line = 7, .column = 12};
    d.source_name = "clause.ahfl";

    check(d.severity == DiagnosticSeverity::Warning, "direct.severity.warning");
    check(d.code.value() == "validation.DECREASES_SHADOWED_RECEIVER", "direct.code");
    check(d.message.find("self") != std::string::npos &&
              d.message.find("Agent") != std::string::npos,
          "direct.message.args");
    check(d.position->line == 7 && d.position->column == 12, "direct.position");
    check(d.source_name.value() == "clause.ahfl", "direct.source_name");
}

} // anonymous namespace

int main() {
    test_codes_exist_and_format();
    test_severity_convention();
    test_render_includes_code_and_position();
    test_direct_diagnostic_roundtrip();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
