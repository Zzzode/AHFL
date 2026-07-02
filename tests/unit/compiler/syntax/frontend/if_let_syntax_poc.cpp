// Wave-19 Lane 3b F2: RFC e-1 Optional narrowing — minimal if-let syntax POC.
//
// Pure syntax-layer validation only: parse succeeds, AST node has the right
// shape, the AST debug-printer + source formatter both accept the node, and
// formatter → re-parse roundtrips cleanly.  Type narrowing / TypedHIR
// semantics / exhaustive-patterns are explicitly out of scope.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "tooling/formatter/formatter.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::ast::NodeKind;
using ahfl::ast::StatementSyntaxKind;

[[nodiscard]] ahfl::ParseResult parse_only(std::string_view filename,
                                           const std::string &source) {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(std::string(filename), source);
    if (parse_result.has_errors()) {
        MESSAGE("parse errors detected");
        for (const auto &entry : parse_result.diagnostics.entries()) {
            MESSAGE("  parse: " << entry.message);
        }
    }
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);
    return parse_result;
}

/// Returns the body block of the first `fn` declaration in a program.
/// All fixtures are wrapped as `fn test() { ... }` so the statement rule is
/// exercised without needing a full agent / flow scaffolding.
[[nodiscard]] const ahfl::ast::BlockSyntax *
first_fn_body(const ahfl::ast::Program &program) {
    for (const auto &decl : program.declarations) {
        if (decl->kind != NodeKind::FnDecl) {
            continue;
        }
        const auto *fn = static_cast<const ahfl::ast::FnDecl *>(decl.get());
        return fn->body.get();
    }
    return nullptr;
}

[[nodiscard]] const ahfl::ast::StatementSyntax *
first_stmt_of_first_fn(const ahfl::ast::Program &program) {
    const auto *block = first_fn_body(program);
    if (block == nullptr || block->statements.empty()) {
        return nullptr;
    }
    return block->statements[0].get();
}

/// Wrap a statement body in a function decl so the source is parseable as a
/// top-level program fragment.
std::string wrap_with_fn(std::string_view body) {
    std::ostringstream out;
    out << "fn example() {\n" << body;
    if (!body.empty() && body.back() != '\n') {
        out << '\n';
    }
    out << "}\n";
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Case 1: two-line `if let Some(x) = e { a; } else { b; }` — variant with
// single binding + else block.  Exhaustive shape assertion so AST roundtrips
// produce the same high-level structure when walked.
// ---------------------------------------------------------------------------

TEST_CASE("if let Some(x) = e with then and else blocks") {
    const std::string source = wrap_with_fn(
        "    if let Some(x) = payload {\n"
        "        consume(x);\n"
        "    } else {\n"
        "        missing();\n"
        "    }\n");

    const auto parse_result = parse_only("if_let_then_else.ahfl", source);
    const auto *program = parse_result.program.get();

    const auto *stmt = first_stmt_of_first_fn(*program);
    REQUIRE(stmt != nullptr);
    CHECK(stmt->kind == StatementSyntaxKind::IfLet);

    const auto *ifl = stmt->if_let_stmt.get();
    REQUIRE(ifl != nullptr);
    REQUIRE(ifl->pattern != nullptr);
    CHECK(ifl->pattern->variant_name == "Some");
    REQUIRE(ifl->pattern->bindings.size() == 1);
    CHECK(ifl->pattern->bindings[0] == "x");

    REQUIRE(ifl->scrutinee != nullptr);

    REQUIRE(ifl->then_block != nullptr);
    CHECK(ifl->then_block->statements.size() == 1);
    CHECK(ifl->then_block->statements[0]->kind == StatementSyntaxKind::Expr);

    REQUIRE(ifl->else_block != nullptr);
    CHECK(ifl->else_block->statements.size() == 1);
    CHECK(ifl->else_block->statements[0]->kind == StatementSyntaxKind::Expr);

    // Ast-printer outline contains the if_let + pattern shape keywords.
    std::ostringstream printer;
    ahfl::dump_program_outline(*program, printer);
    const std::string outline = printer.str();
    CHECK(outline.find("if_let") != std::string::npos);
    CHECK(outline.find("pattern Some(x)") != std::string::npos);

    // Formatter succeeds on the source and preserves the if-let shape.
    const auto fmt = ahfl::formatter::format_source(source);
    CHECK(fmt.success);
    CHECK(fmt.formatted.find("if let Some(x) = ") != std::string::npos);
    CHECK(fmt.formatted.find("} else {") != std::string::npos);

    // Formatter output must still re-parse cleanly (roundtrip).
    const ahfl::Frontend frontend;
    const auto reparsed = frontend.parse_text("if_let_then_else_roundtrip.ahfl", fmt.formatted);
    if (reparsed.has_errors()) {
        for (const auto &entry : reparsed.diagnostics.entries()) {
            MESSAGE("  reparsed error: " << entry.message);
        }
    }
    CHECK_FALSE(reparsed.has_errors());
}

// ---------------------------------------------------------------------------
// Case 2: three-variable binding — `if let Ok(a, b, c) = expr { ... }`.
// Exercises the variadic binding list and verifies no-else parses correctly.
// ---------------------------------------------------------------------------

TEST_CASE("if let Ok(a, b, c) — three bindings, no else") {
    const std::string source = wrap_with_fn(
        "    if let Ok(a, b, c) = parse(input) {\n"
        "        accept(a);\n"
        "        accept(b);\n"
        "        accept(c);\n"
        "    }\n");

    const auto parse_result = parse_only("if_let_three_bindings.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *stmt = first_stmt_of_first_fn(*program);
    REQUIRE(stmt != nullptr);
    CHECK(stmt->kind == StatementSyntaxKind::IfLet);

    const auto *ifl = stmt->if_let_stmt.get();
    REQUIRE(ifl != nullptr);
    REQUIRE(ifl->pattern != nullptr);
    CHECK(ifl->pattern->variant_name == "Ok");
    REQUIRE(ifl->pattern->bindings.size() == 3);
    CHECK(ifl->pattern->bindings[0] == "a");
    CHECK(ifl->pattern->bindings[1] == "b");
    CHECK(ifl->pattern->bindings[2] == "c");
    CHECK(ifl->else_block == nullptr);
    REQUIRE(ifl->then_block != nullptr);
    CHECK(ifl->then_block->statements.size() == 3);

    // Formatter roundtrip.
    const auto fmt = ahfl::formatter::format_source(source);
    CHECK(fmt.success);
    CHECK(fmt.formatted.find("if let Ok(a, b, c) = ") != std::string::npos);

    const ahfl::Frontend frontend;
    const auto reparsed =
        frontend.parse_text("if_let_three_bindings_roundtrip.ahfl", fmt.formatted);
    CHECK_FALSE(reparsed.has_errors());
}

// ---------------------------------------------------------------------------
// Case 3: nesting — outer `if let Some(a) = fetch()` with an inner
// `if let Some(b) = Some(3)` inside its then-block.  Verifies the AST tree
// contains StatementSyntaxKind::IfLet nested within the outer then-block's
// first child (i.e., nesting at the AST level), which is what the task asks
// for.  The scrutinee is `Some(3)` so the variant-call expression shape is
// also parsed and walked.
// ---------------------------------------------------------------------------

TEST_CASE("nested if let — outer then-block contains another if let") {
    const std::string source = wrap_with_fn(
        "    if let Some(a) = fetch() {\n"
        "        if let Some(b) = Some(3) {\n"
        "            combine(a, b);\n"
        "        }\n"
        "    }\n");

    const auto parse_result = parse_only("if_let_nested.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *outer = first_stmt_of_first_fn(*program);
    REQUIRE(outer != nullptr);
    CHECK(outer->kind == StatementSyntaxKind::IfLet);

    const auto *outer_ifl = outer->if_let_stmt.get();
    REQUIRE(outer_ifl != nullptr);
    REQUIRE(outer_ifl->pattern != nullptr);
    CHECK(outer_ifl->pattern->variant_name == "Some");
    REQUIRE(outer_ifl->pattern->bindings.size() == 1);
    CHECK(outer_ifl->pattern->bindings[0] == "a");

    REQUIRE(outer_ifl->then_block != nullptr);
    REQUIRE_FALSE(outer_ifl->then_block->statements.empty());
    const auto *inner = outer_ifl->then_block->statements[0].get();
    REQUIRE(inner != nullptr);
    CHECK(inner->kind == StatementSyntaxKind::IfLet);

    const auto *inner_ifl = inner->if_let_stmt.get();
    REQUIRE(inner_ifl != nullptr);
    REQUIRE(inner_ifl->pattern != nullptr);
    CHECK(inner_ifl->pattern->variant_name == "Some");
    REQUIRE(inner_ifl->pattern->bindings.size() == 1);
    CHECK(inner_ifl->pattern->bindings[0] == "b");

    // Formatter roundtrip for the nested shape.
    const auto fmt = ahfl::formatter::format_source(source);
    CHECK(fmt.success);

    const ahfl::Frontend frontend;
    const auto reparsed = frontend.parse_text("if_let_nested_roundtrip.ahfl", fmt.formatted);
    REQUIRE_FALSE(reparsed.has_errors());
    const auto *reparsed_outer = first_stmt_of_first_fn(*reparsed.program);
    REQUIRE(reparsed_outer != nullptr);
    CHECK(reparsed_outer->kind == StatementSyntaxKind::IfLet);
    REQUIRE(reparsed_outer->if_let_stmt != nullptr);
    REQUIRE(reparsed_outer->if_let_stmt->then_block != nullptr);
    REQUIRE_FALSE(reparsed_outer->if_let_stmt->then_block->statements.empty());
    CHECK(reparsed_outer->if_let_stmt->then_block->statements[0]->kind ==
          StatementSyntaxKind::IfLet);
}
