#include "compiler/syntax/frontend/error_recovery.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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

void test_edit_distance() {
    using ahfl::frontend::edit_distance;
    check(edit_distance("", "") == 0, "edit_distance.empty");
    check(edit_distance("abc", "abc") == 0, "edit_distance.equal");
    check(edit_distance("abc", "ab") == 1, "edit_distance.delete");
    check(edit_distance("abc", "axc") == 1, "edit_distance.replace");
    check(edit_distance("kitten", "sitting") == 3, "edit_distance.classic");
}

void test_suggest_keyword() {
    ahfl::frontend::AhflErrorStrategy strategy;
    auto dict = ahfl::frontend::AhflErrorStrategy::keyword_dictionary();
    auto suggestions = strategy.compute_suggestions("struc", dict, 3);
    check(!suggestions.empty(), "suggest.not_empty");
    check(suggestions[0].suggested_token == "struct", "suggest.best_is_struct");
    check(suggestions[0].confidence > 0.5, "suggest.confidence_high");
}

void test_best_match() {
    ahfl::frontend::AhflErrorStrategy strategy;
    auto dict = ahfl::frontend::AhflErrorStrategy::keyword_dictionary();
    auto best = strategy.best_match("workflo", dict);
    check(best.suggested_token == "workflow", "best_match.workflow");
}

void test_partial_parse() {
    // Source with a valid struct declaration and some garbage
    std::string source = "struct Foo {\n    value: String;\n}\n\nxyz invalid garbage\n\nstruct Bar {\n    name: String;\n}";
    auto result = ahfl::frontend::parse_with_recovery(source, "test.ahfl");
    check(result.valid_declaration_count >= 2, "partial.found_declarations");
    check(result.has_partial_ast, "partial.has_ast");
}

// KR5.6: parse_with_recovery now drives the real ANTLR-backed front end, so a
// syntactically invalid source still yields multiple structured errors from
// the actual parser (not an ad-hoc re-tokenizer) while the valid declarations
// around the error survive in the partial AST.
void test_real_path_multi_error_and_survival() {
    // Two valid structs bracketing a garbage line — the parser recovers from
    // the extraneous input and keeps both structs.
    std::string source =
        "struct Alpha {\n    a: Int;\n}\n\n@@@ not a declaration @@@\n\nstruct Beta {\n    b: Int;\n}\n";
    auto result = ahfl::frontend::parse_with_recovery(source, "recover.ahfl");
    check(result.error_count >= 1, "real.reports_error");
    check(result.has_partial_ast, "real.partial_ast_survives");
    check(result.valid_declaration_count >= 2, "real.both_structs_survive");
    // Every reported error carries a message sourced from the real parser.
    check(result.error_messages.size() == result.error_count, "real.messages_match_count");
}

} // anonymous namespace

int main() {
    test_edit_distance();
    test_suggest_keyword();
    test_best_match();
    test_partial_parse();
    test_real_path_multi_error_and_survival();
    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
