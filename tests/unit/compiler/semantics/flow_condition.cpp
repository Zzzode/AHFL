#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/condition_facts.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Helpers (namespace scope).
// ---------------------------------------------------------------------------

[[nodiscard]] ahfl::SourceRange range_of(std::string_view source, std::string_view needle,
                                         std::size_t offset = 0) {
    const auto pos = source.find(needle, offset);
    REQUIRE(pos != std::string_view::npos);
    return ahfl::SourceRange{
        .begin_offset = pos,
        .end_offset = pos + needle.size(),
    };
}

// Return the n-th (1-indexed) occurrence of needle in source.
[[nodiscard]] ahfl::SourceRange range_of_nth(std::string_view source, std::string_view needle,
                                             std::size_t n) {
    std::size_t pos = 0;
    std::size_t found = 0;
    while (found < n) {
        const auto next = source.find(needle, pos);
        REQUIRE(next != std::string_view::npos);
        ++found;
        pos = next + 1;
        if (found == n) {
            return ahfl::SourceRange{
                .begin_offset = next,
                .end_offset = next + needle.size(),
            };
        }
    }
    FAIL("n-th occurrence not found");
    return ahfl::SourceRange{};
}

// Minimal AHFL program boilerplate. Two variants:
//   - kSkeletonString:   Response.value = String.  Use when then-block returns an
//                        unwrapped narrowable value (narrowing produces String).
//   - kSkeletonOptional: Response.value = Optional<String>. Use when the return
//                        value is expected to remain Optional (e.g. after
//                        invalidation).
// Both share the same Context.payload structure so &&-chain tests work.
constexpr std::string_view kSkeletonPreamble = R"AHFL(
struct Request {
    fallback: String;
}

struct Payload {
    some_field: Int;
}

struct Context {
    token: Optional<String> = none;
    payload: Optional<Payload> = none;
}
)AHFL";

constexpr std::string_view kSkeletonString = R"AHFL(struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
{body}
    }
}
)AHFL";

constexpr std::string_view kSkeletonOptional = R"AHFL(struct Response {
    value: Optional<String>;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
{body}
    }
}
)AHFL";

[[nodiscard]] std::string render_body(std::string_view body, std::string_view skeleton) {
    std::string tmpl = std::string{kSkeletonPreamble} + std::string{skeleton};
    const std::string needle{"{body}"};
    const auto pos = tmpl.find(needle);
    REQUIRE(pos != std::string::npos);
    return tmpl.substr(0, pos) + std::string{body} + tmpl.substr(pos + needle.size());
}

// Full parse -> resolve -> typecheck pipeline. Always runs REQUIRE checks
// for early stage errors so failures localise to the correct phase.
[[nodiscard]] ahfl::TypeCheckResult typecheck_source(const std::string &source) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("flow_condition.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto result = type_checker.check(*parse_result.program, resolve_result);
    if (result.has_errors()) {
        // Dump the first diagnostic so tests that unexpectedly fail to typecheck
        // give an actionable message rather than a silent boolean.
        if (!result.diagnostics.entries().empty()) {
            const auto &d = result.diagnostics.entries().front();
            MESSAGE("typecheck diagnostic: ", d.message);
            for (const auto &rel : d.related) {
                MESSAGE("  related: ", rel.message);
            }
        }
    }
    return result;
}

// Helpers that find a particular TypedExpr inside a typed_program by source
// text (exact range match).
[[nodiscard]] __attribute__((unused)) const ahfl::TypedExpr *
find_expr_by_text(const std::string &source,
                  const ahfl::TypeCheckResult &result,
                  std::string_view needle,
                  std::size_t offset = 0) {
    return result.typed_program.find_expr_by_range(range_of(source, needle, offset), std::nullopt);
}

[[nodiscard]] const ahfl::TypedExpr *find_expr_by_nth(const std::string &source,
                                                     const ahfl::TypeCheckResult &result,
                                                     std::string_view needle, std::size_t n) {
    return result.typed_program.find_expr_by_range(range_of_nth(source, needle, n), std::nullopt);
}

// Deep recursive AST walker that finds the top-level binary NotEqual node
// inside an expression subtree. Lives at namespace scope (local classes may
// not contain templates, but the visitor overloads are non-template member
// functions — being at namespace scope is still cleaner).
struct NoneComparisonFinder {
    const ahfl::ast::ExprSyntax **out;

    void visit_binary(const ahfl::ast::ExprSyntax &expr) {
        const auto &node = expr.as<ahfl::ast::BinaryExpr>();
        if (node.op == ahfl::ast::ExprBinaryOp::NotEqual && *out == nullptr) {
            *out = &expr;
        }
        if (node.lhs) {
            ahfl::ast::visit_expr_syntax(*node.lhs, *this);
        }
        if (node.rhs) {
            ahfl::ast::visit_expr_syntax(*node.rhs, *this);
        }
    }
    void visit_group(const ahfl::ast::ExprSyntax &expr) {
        const auto &inner = expr.as<ahfl::ast::GroupExpr>().inner;
        if (inner) {
            ahfl::ast::visit_expr_syntax(*inner, *this);
        }
    }
    void visit_unary(const ahfl::ast::ExprSyntax &expr) {
        const auto &operand = expr.as<ahfl::ast::UnaryExpr>().operand;
        if (operand) {
            ahfl::ast::visit_expr_syntax(*operand, *this);
        }
    }
    void visit_member_access(const ahfl::ast::ExprSyntax &expr) {
        const auto &base = expr.as<ahfl::ast::MemberAccessExpr>().base;
        if (base) {
            ahfl::ast::visit_expr_syntax(*base, *this);
        }
    }
    void visit_index_access(const ahfl::ast::ExprSyntax &expr) {
        const auto &node = expr.as<ahfl::ast::IndexAccessExpr>();
        if (node.base) {
            ahfl::ast::visit_expr_syntax(*node.base, *this);
        }
        if (node.index) {
            ahfl::ast::visit_expr_syntax(*node.index, *this);
        }
    }
    void visit_call(const ahfl::ast::ExprSyntax &expr) {
        for (const auto &arg : expr.as<ahfl::ast::CallExpr>().arguments) {
            if (arg) {
                ahfl::ast::visit_expr_syntax(*arg, *this);
            }
        }
    }
    void visit_struct_literal(const ahfl::ast::ExprSyntax &expr) {
        for (const auto &field : expr.as<ahfl::ast::StructLiteralExpr>().fields) {
            if (field->value) {
                ahfl::ast::visit_expr_syntax(*field->value, *this);
            }
        }
    }
    void visit_list_literal(const ahfl::ast::ExprSyntax &expr) {
        for (const auto &elem : expr.as<ahfl::ast::ListLiteralExpr>().items) {
            if (elem) {
                ahfl::ast::visit_expr_syntax(*elem, *this);
            }
        }
    }
    void visit_set_literal(const ahfl::ast::ExprSyntax &expr) {
        for (const auto &elem : expr.as<ahfl::ast::SetLiteralExpr>().items) {
            if (elem) {
                ahfl::ast::visit_expr_syntax(*elem, *this);
            }
        }
    }
    void visit_map_literal(const ahfl::ast::ExprSyntax &expr) {
        for (const auto &entry : expr.as<ahfl::ast::MapLiteralExpr>().entries) {
            if (entry->key) {
                ahfl::ast::visit_expr_syntax(*entry->key, *this);
            }
            if (entry->value) {
                ahfl::ast::visit_expr_syntax(*entry->value, *this);
            }
        }
    }
    void visit_some(const ahfl::ast::ExprSyntax &expr) {
        const auto &value = expr.as<ahfl::ast::SomeExpr>().value;
        if (value) {
            ahfl::ast::visit_expr_syntax(*value, *this);
        }
    }
    // Leaf nodes — no children.
    void visit_bool_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_integer_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_float_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_decimal_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_string_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_duration_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_none_literal(const ahfl::ast::ExprSyntax &) {}
    void visit_path(const ahfl::ast::ExprSyntax &) {}
    void visit_qualified_value(const ahfl::ast::ExprSyntax &) {}
    // P1 (ADT): match expressions are walked for nested conditions in the
    // scrutinee; arm patterns/guards/bodies are not conditions themselves.
    void visit_match(const ahfl::ast::ExprSyntax &expr) {
        const auto &scrutinee = expr.as<ahfl::ast::MatchExpr>().scrutinee;
        if (scrutinee) {
            ahfl::ast::visit_expr_syntax(*scrutinee, *this);
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// TC1: Optional simple narrow — `if (x != none)` unwraps x in the then-block.
// ---------------------------------------------------------------------------
TEST_CASE("Optional simple narrow unwraps type on then branch") {
    const std::string body =
        "        if (ctx.token != none) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonString);

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    // The LHS of the `!= none` comparison is evaluated *before* the narrowing
    // merges, so it must keep the declared type Optional<String>.
    const auto cmp_range = range_of(source, "ctx.token != none");
    const auto cmp_lhs =
        std::find_if(result.typed_program.expressions.begin(),
                     result.typed_program.expressions.end(),
                     [cmp_range](const ahfl::TypedExpr &e) {
                         return e.semantic_name == "ctx.token" &&
                                e.range.begin_offset >= cmp_range.begin_offset &&
                                e.range.end_offset <= cmp_range.end_offset;
                     });
    REQUIRE(cmp_lhs != result.typed_program.expressions.end());
    REQUIRE(cmp_lhs->type != nullptr);
    CHECK(cmp_lhs->type->holds<ahfl::types::OptionalT>());

    // The ctx.token inside the then-block struct literal must be narrowed to String.
    // It is the second source occurrence of `ctx.token`.
    const auto *then_use = find_expr_by_nth(source, result, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());
}

// ---------------------------------------------------------------------------
// TC2: && chain narrowing propagates across conjuncts.
// ---------------------------------------------------------------------------
TEST_CASE("Optional &&-chain narrow propagates into second conjunct") {
    // `extract_condition_facts` recurses through `ExprBinaryOp::And` and
    // merges the collected narrowing facts from each conjunct into a single
    // `when_true` set. The if-statement then merges those into the then-block
    // context. This test exercises the &&-chain specifically (by adding a
    // trivially-true second conjunct) and verifies the then-block sees the
    // exact same narrowing facts it would see for a bare `x != none`
    // condition.
    const std::string body =
        "        if (ctx.token != none && true) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonString);

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    // Second text occurrence of `ctx.token`: the then-block value. It must
    // have been narrowed to String (i.e. the && chain didn't drop the
    // when_true narrowing facts from the first conjunct).
    const auto *then_use = find_expr_by_nth(source, result, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());
    CHECK_FALSE(then_use->type->holds<ahfl::types::OptionalT>());

    // Sanity: first occurrence (LHS of `!= none`) is still Optional because
    // the narrowing hasn't merged yet when the comparison's LHS is evaluated.
    const auto *cmp_lhs = find_expr_by_nth(source, result, "ctx.token", 1);
    REQUIRE(cmp_lhs != nullptr);
    REQUIRE(cmp_lhs->type != nullptr);
    CHECK(cmp_lhs->type->holds<ahfl::types::OptionalT>());
}

// ---------------------------------------------------------------------------
// TC3: Negated condition must NOT narrow on the then branch.
// ---------------------------------------------------------------------------
TEST_CASE("Negated condition does not incorrectly narrow on then branch") {
    const std::string body =
        "        if (!(ctx.token != none)) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: some(input.fallback) };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonOptional);

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    // Every textual use of `ctx.token` must remain Optional<String>. The use
    // inside the then-block (after negation) must NOT have been unwrapped;
    // `!(x != none)` triggers the complement of the inner comparison, and the
    // checker must conservatively avoid unwrapping.
    for (const auto &expr : result.typed_program.expressions) {
        if (expr.semantic_name == "ctx.token") {
            REQUIRE(expr.type != nullptr);
            INFO("ctx.token at offset " << expr.range.begin_offset
                                         << " has type " << expr.type->describe());
            CHECK(expr.type->holds<ahfl::types::OptionalT>());
        }
    }
}

// ---------------------------------------------------------------------------
// TC4: Assignment to a place invalidates narrowing facts for it.
// ---------------------------------------------------------------------------
TEST_CASE("Assignment invalidates earlier narrowing for the same place") {
    const std::string body =
        "        if (ctx.token != none) {\n"
        "            ctx.token = none;\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: some(input.fallback) };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonOptional);

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    // Uses of ctx.token, in source order:
    //   1) LHS of `!= none`              => Optional<String> (before narrowing)
    //   2) Target path in `= none`       => Optional<String>
    //   3) Value `ctx.token` in the return struct literal AFTER assignment
    //        => must be Optional<String> again (invalidation cleared narrowing).
    const auto *post_assign = find_expr_by_nth(source, result, "ctx.token", 3);
    REQUIRE(post_assign != nullptr);
    REQUIRE(post_assign->type != nullptr);
    CHECK(post_assign->type->holds<ahfl::types::OptionalT>());
    CHECK_FALSE(post_assign->type->holds<ahfl::types::StringT>());

    // Sanity: at least two Optional<String> occurrences (LHS and post-assign).
    const auto optional_tokens =
        std::count_if(result.typed_program.expressions.begin(),
                      result.typed_program.expressions.end(),
                      [](const ahfl::TypedExpr &e) {
                          return e.semantic_name == "ctx.token" &&
                                 e.type && e.type->holds<ahfl::types::OptionalT>();
                      });
    CHECK(optional_tokens >= 2);
}

// ---------------------------------------------------------------------------
// TC5: ConditionFacts records complementary when_true / when_false edges.
// ---------------------------------------------------------------------------
TEST_CASE("ConditionFacts records complementary then/else facts") {
    const std::string body =
        "        if (ctx.token != none) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonString);

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("condition_facts.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    // Walk all flows (FlowDecl) / state handlers / blocks / if-statements, and
    // locate the first if-condition AST node. The NoneComparisonFinder then
    // drills down to the top-level NotEqual binary expression.
    const ahfl::ast::ExprSyntax *condition = nullptr;
    REQUIRE(parse_result.program != nullptr);
    for (const auto &decl : parse_result.program->declarations) {
        if (decl->kind != ahfl::ast::NodeKind::FlowDecl) {
            continue;
        }
        const auto *flow = dynamic_cast<const ahfl::ast::FlowDecl *>(decl.get());
        if (flow == nullptr) {
            continue;
        }
        for (const auto &handler : flow->state_handlers) {
            if (!handler->body) {
                continue;
            }
            for (const auto &stmt : handler->body->statements) {
                if (stmt->kind != ahfl::ast::StatementSyntaxKind::If || !stmt->if_stmt) {
                    continue;
                }
                if (stmt->if_stmt->condition == nullptr) {
                    continue;
                }
                NoneComparisonFinder walker{.out = &condition};
                ahfl::ast::visit_expr_syntax(*stmt->if_stmt->condition, walker);
            }
        }
    }
    REQUIRE(condition != nullptr);

    const auto facts = ahfl::extract_condition_facts(*condition);

    const ahfl::Place token_place{.root = "ctx", .members = {"token"}};
    CHECK(facts.when_true.has_fact(token_place, ahfl::TypeFactKind::IsNotNone));
    CHECK(facts.when_false.has_fact(token_place, ahfl::TypeFactKind::IsNone));
    CHECK_FALSE(facts.when_true.has_fact(token_place, ahfl::TypeFactKind::IsNone));
    CHECK_FALSE(facts.when_false.has_fact(token_place, ahfl::TypeFactKind::IsNotNone));
}

// ---------------------------------------------------------------------------
// TC6: Symmetric `none != x` behaves just like `x != none`.
// ---------------------------------------------------------------------------
TEST_CASE("Reversed none comparison (none != x) narrows symmetrically") {
    const std::string body =
        "        if (none != ctx.token) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonString);

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    // Only source occurrence of `ctx.token` inside body is the RHS of the
    // comparison (evaluated before narrowing merges, so still Optional) and
    // the second occurrence inside the then-block return which must be narrowed
    // to String.
    const auto *then_use = find_expr_by_nth(source, result, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());
    CHECK_FALSE(then_use->type->holds<ahfl::types::OptionalT>());

    // Sanity: at least one narrowed String occurrence in the whole program.
    const auto narrowed_tokens =
        std::count_if(result.typed_program.expressions.begin(),
                      result.typed_program.expressions.end(),
                      [](const ahfl::TypedExpr &e) {
                          return e.semantic_name == "ctx.token" &&
                                 e.type && e.type->holds<ahfl::types::StringT>();
                      });
    CHECK(narrowed_tokens >= 1);
}

// ---------------------------------------------------------------------------
// TC7: IsVariant fact extraction from `x == Enum::Variant` condition.
// ---------------------------------------------------------------------------
TEST_CASE("IsVariant fact extracted from enum equality condition") {
    // Program with an enum type and a context field of that enum type.
    const std::string source = R"AHFL(
enum Priority {
    Low,
    High,
}

struct Request {
    fallback: String;
}

struct Context {
    level: Priority = Priority::Low;
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if ctx.level == Priority::High {
            return Response { value: "high" };
        } else {
            return Response { value: "low" };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("variant_facts.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    // Walk the AST to locate the if-condition.
    const ahfl::ast::ExprSyntax *condition = nullptr;
    for (const auto &decl : parse_result.program->declarations) {
        if (decl->kind != ahfl::ast::NodeKind::FlowDecl) {
            continue;
        }
        const auto *flow = dynamic_cast<const ahfl::ast::FlowDecl *>(decl.get());
        if (flow == nullptr) {
            continue;
        }
        for (const auto &handler : flow->state_handlers) {
            if (!handler->body) {
                continue;
            }
            for (const auto &stmt : handler->body->statements) {
                if (stmt->kind != ahfl::ast::StatementSyntaxKind::If || !stmt->if_stmt) {
                    continue;
                }
                condition = stmt->if_stmt->condition.get();
            }
        }
    }
    REQUIRE(condition != nullptr);

    const auto facts = ahfl::extract_condition_facts(*condition);

    const ahfl::Place level_place{.root = "ctx", .members = {"level"}};

    // when_true should have an IsVariant fact for Priority::High.
    CHECK(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsVariant));
    CHECK(facts.when_true.has_variant_fact(level_place, "Priority", "High"));

    // Negative checks: it's not an IsNone/IsNotNone fact.
    CHECK_FALSE(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsNone));
    CHECK_FALSE(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsNotNone));

    // when_false should carry the exact complement.
    CHECK_FALSE(facts.when_false.has_fact(level_place, ahfl::TypeFactKind::IsVariant));
    CHECK(facts.when_false.has_fact(level_place, ahfl::TypeFactKind::IsNotVariant));
    CHECK(facts.when_false.has_not_variant_fact(level_place, "Priority", "High"));
}

// ---------------------------------------------------------------------------
// TC7b: IsNotVariant fact extraction from `x != Enum::Variant` condition.
// ---------------------------------------------------------------------------
TEST_CASE("IsNotVariant fact extracted from enum inequality condition") {
    const std::string source = R"AHFL(
enum Priority {
    Low,
    High,
}

struct Request {
    fallback: String;
}

struct Context {
    level: Priority = Priority::Low;
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if ctx.level != Priority::High {
            return Response { value: "not-high" };
        } else {
            return Response { value: "high" };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("variant_not_facts.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::ast::ExprSyntax *condition = nullptr;
    for (const auto &decl : parse_result.program->declarations) {
        if (decl->kind != ahfl::ast::NodeKind::FlowDecl) {
            continue;
        }
        const auto *flow = dynamic_cast<const ahfl::ast::FlowDecl *>(decl.get());
        if (flow == nullptr) {
            continue;
        }
        for (const auto &handler : flow->state_handlers) {
            if (!handler->body) {
                continue;
            }
            for (const auto &stmt : handler->body->statements) {
                if (stmt->kind != ahfl::ast::StatementSyntaxKind::If || !stmt->if_stmt) {
                    continue;
                }
                condition = stmt->if_stmt->condition.get();
            }
        }
    }
    REQUIRE(condition != nullptr);

    const auto facts = ahfl::extract_condition_facts(*condition);
    const ahfl::Place level_place{.root = "ctx", .members = {"level"}};

    CHECK(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsNotVariant));
    CHECK(facts.when_true.has_not_variant_fact(level_place, "Priority", "High"));
    CHECK_FALSE(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsVariant));
    CHECK(facts.when_false.has_fact(level_place, ahfl::TypeFactKind::IsVariant));
    CHECK(facts.when_false.has_variant_fact(level_place, "Priority", "High"));
}

// ---------------------------------------------------------------------------
// TC7c: Enum equality narrows the place to an enum-variant singleton type.
// ---------------------------------------------------------------------------
TEST_CASE("Enum equality narrows then-branch expression to variant singleton type") {
    const std::string source = R"AHFL(
enum Priority {
    Low,
    High,
}

struct Request {
    fallback: String;
}

struct Context {
    level: Priority = Priority::Low;
}

struct Response {
    value: Priority;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if ctx.level == Priority::High {
            return Response { value: ctx.level };
        } else {
            return Response { value: Priority::Low };
        }
    }
}
)AHFL";

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    const auto *then_use = find_expr_by_nth(source, result, "ctx.level", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);

    const auto *variant = then_use->type->get_if<ahfl::types::EnumVariantT>();
    REQUIRE(variant != nullptr);
    CHECK(variant->canonical_name == "Priority");
    CHECK(variant->variant_name == "High");
}

// ---------------------------------------------------------------------------
// TC7d: In a two-variant enum, `x != Enum::A` leaves exactly one variant.
// ---------------------------------------------------------------------------
TEST_CASE("Enum inequality narrows two-variant then branch to remaining singleton type") {
    const std::string source = R"AHFL(
enum Priority {
    Low,
    High,
}

struct Request {
    fallback: String;
}

struct Context {
    level: Priority = Priority::Low;
}

struct Response {
    value: Priority;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if ctx.level != Priority::High {
            return Response { value: ctx.level };
        } else {
            return Response { value: Priority::High };
        }
    }
}
)AHFL";

    const auto result = typecheck_source(source);
    REQUIRE_FALSE(result.has_errors());

    const auto *then_use = find_expr_by_nth(source, result, "ctx.level", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);

    const auto *variant = then_use->type->get_if<ahfl::types::EnumVariantT>();
    REQUIRE(variant != nullptr);
    CHECK(variant->canonical_name == "Priority");
    CHECK(variant->variant_name == "Low");
}

// ---------------------------------------------------------------------------
// TC8: IsVariant fact with reversed operands: `Enum::Variant == x`.
// ---------------------------------------------------------------------------
TEST_CASE("IsVariant fact extracted with reversed operand order") {
    const std::string source = R"AHFL(
enum Priority {
    Low,
    High,
}

struct Request {
    fallback: String;
}

struct Context {
    level: Priority = Priority::Low;
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if Priority::Low == ctx.level {
            return Response { value: "low" };
        } else {
            return Response { value: "high" };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("variant_facts_rev.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    // Walk the AST to locate the if-condition.
    const ahfl::ast::ExprSyntax *condition = nullptr;
    for (const auto &decl : parse_result.program->declarations) {
        if (decl->kind != ahfl::ast::NodeKind::FlowDecl) {
            continue;
        }
        const auto *flow = dynamic_cast<const ahfl::ast::FlowDecl *>(decl.get());
        if (flow == nullptr) {
            continue;
        }
        for (const auto &handler : flow->state_handlers) {
            if (!handler->body) {
                continue;
            }
            for (const auto &stmt : handler->body->statements) {
                if (stmt->kind != ahfl::ast::StatementSyntaxKind::If || !stmt->if_stmt) {
                    continue;
                }
                condition = stmt->if_stmt->condition.get();
            }
        }
    }
    REQUIRE(condition != nullptr);

    const auto facts = ahfl::extract_condition_facts(*condition);

    const ahfl::Place level_place{.root = "ctx", .members = {"level"}};

    // Reversed order should still produce the same IsVariant fact.
    CHECK(facts.when_true.has_fact(level_place, ahfl::TypeFactKind::IsVariant));
    CHECK(facts.when_true.has_variant_fact(level_place, "Priority", "Low"));
}
