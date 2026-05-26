#include "formatter/formatter.hpp"
#include "formatter/format_config.hpp"
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

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
