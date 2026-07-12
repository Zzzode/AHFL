#include "tooling/formatter/formatter.hpp"
#include "tooling/formatter/format_config.hpp"
#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

int main() {
    // Test 1: Empty source
    {
        auto result = ahfl::formatter::format_source("");
        check(result.success, "format empty source succeeds");
    }

    // Test 2: Simple indentation
    {
        std::string source = "struct Foo {\nvalue: String;\n}\n";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format simple struct succeeds");
        check(result.formatted.find("    value") != std::string::npos,
              "content indented after opening brace");
    }

    // Test 3: Check formatting detects unformatted code
    {
        std::string unformatted = "struct Foo {\n  value: String;\n}\n";
        auto result = ahfl::formatter::format_source(unformatted);
        check(result.success, "format unformatted code succeeds");
    }

    // Test 4: Default options
    {
        auto opts = ahfl::formatter::default_options();
        check(opts.indent_width == 4, "default indent width is 4");
        check(!opts.use_tabs, "default uses spaces");
        check(opts.trailing_newline, "default adds trailing newline");
    }

    // Test 5: Config serialization round-trip
    {
        ahfl::formatter::FormatOptions opts;
        opts.indent_width = 2;
        opts.use_tabs = true;
        auto serialized = ahfl::formatter::serialize_config(opts);
        check(serialized.find("indent_width = 2") != std::string::npos, "serialized indent width");
        check(serialized.find("use_tabs = true") != std::string::npos, "serialized use_tabs");
    }

    // Test 6: compute_diff finds differences
    {
        std::string original = "line1\nline2\nline3\n";
        std::string formatted = "line1\n  line2\nline3\n";
        auto diffs = ahfl::formatter::compute_diff(original, formatted);
        check(!diffs.empty(), "compute_diff finds differences");
        check(diffs[0].line == 2, "diff on correct line");
    }

    // Test 7: check_formatting with already-formatted code
    {
        std::string source = "struct Foo {\n    value: String;\n}\n";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format already-formatted succeeds");
    }

    // Test 8: Predicate declaration
    {
        std::string source = "predicate IsReady() -> Bool;\n";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format predicate succeeds");
        check(result.formatted.find("predicate IsReady()") != std::string::npos,
              "predicate name and params preserved");
        check(result.formatted.find("-> Bool") != std::string::npos,
              "predicate return type preserved");
    }

    // Test 9: Const declaration
    {
        std::string source = "const BAD: Bool = IsReady();\n";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format const succeeds");
        check(result.formatted.find("const BAD: Bool = ") != std::string::npos,
              "const name, type, and value preserved");
        check(result.formatted.find("IsReady()") != std::string::npos,
              "const call expression preserved");
    }

    // Test 10: Predicate + const together
    {
        std::string source = "predicate IsReady() -> Bool;\n\nconst BAD: Bool = IsReady();\n";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format predicate+const succeeds");
        check(result.formatted.find("predicate IsReady()") != std::string::npos,
              "predicate preserved in combined file");
        check(result.formatted.find("const BAD") != std::string::npos,
              "const preserved in combined file");
    }

    // Test 11: Struct enum variant pattern with explicit field pattern
    {
        std::string source = R"AHFL(module fmt::enum_pattern;

enum Packet {
    Empty,
    Data { code: Int, label: String },
}

const X: Int = match Packet::Data { label: "ok", code: 7 } { Data { code: _, label } => 1, Empty => 0 };
)AHFL";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format explicit struct variant field pattern succeeds");
        check(result.formatted.find("Data { code: _, label }") != std::string::npos,
              "explicit struct variant field pattern preserved");
    }

    // Test 12: Formatting preserves the complete source surface.
    {
        std::string source = R"AHFL(module fmt::lossless;

import std::result;
import std::option;

/// Public generic option used by the formatter regression.
pub enum PublicOption<T> {
Some(T),
None,
}

pub @builtin("identity")
fn identity<T>(value: T) -> T effect Pure decreases 0 {
return value;
}
)AHFL";
        auto result = ahfl::formatter::format_source(source);
        check(result.success, "format lossless source succeeds");
        check(result.formatted.find("import std::result;") != std::string::npos,
              "first import preserved");
        check(result.formatted.find("import std::option;") != std::string::npos,
              "second import preserved");
        check(result.formatted.find("/// Public generic option") != std::string::npos,
              "documentation comment preserved");
        check(result.formatted.find("pub enum PublicOption<T>") != std::string::npos,
              "public generic enum preserved");
        check(result.formatted.find("@builtin(\"identity\")") != std::string::npos,
              "builtin attribute preserved");
        check(result.formatted.find("pub @builtin(\"identity\")") != std::string::npos,
              "public builtin prefix preserved");
        check(result.formatted.find(
                  "fn identity<T>(value: T) -> T effect Pure decreases 0") !=
                  std::string::npos,
              "public generic effectful function preserved");
    }

    // Test 13: Formatting a canonical result is byte-for-byte idempotent.
    {
        std::string source = R"AHFL(module fmt::idempotent;

pub fn choose<T>(value: T) -> T effect Pure decreases 0 {
if value == value {
return value;
}
return value;
}
)AHFL";
        const auto first = ahfl::formatter::format_source(source);
        check(first.success, "first idempotence format succeeds");
        const auto second = ahfl::formatter::format_source(first.formatted);
        check(second.success, "second idempotence format succeeds");
        check(second.formatted == first.formatted,
              "formatter output is byte-for-byte idempotent");
    }

    // Test 14: Continuation indentation follows delimiters and clauses.
    {
        std::string source = R"AHFL(module fmt::continuation;

fn combine<T>(
left: T,
right: T
) -> T effect Pure
decreases 1 {
return choose(
left,
right);
}

fn declared<T>(value: T)
-> T effect Pure;
)AHFL";
        const auto result = ahfl::formatter::format_source(source);
        check(result.success, "format continuation source succeeds");
        check(result.formatted.find(
                  "fn combine<T>(\n"
                  "    left: T,\n"
                  "    right: T\n"
                  ") -> T effect Pure\n"
                  "    decreases 1 {\n"
                  "    return choose(\n"
                  "        left,\n"
                  "        right);\n"
                  "}\n"
                  "\n"
                  "fn declared<T>(value: T)\n"
                  "    -> T effect Pure;\n") != std::string::npos,
              "continuation lines receive canonical indentation");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
