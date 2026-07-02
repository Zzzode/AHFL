#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/condition_facts.hpp"
#include "ahfl/compiler/semantics/expression_sema.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
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
    token: Optional<String> = std::option::Option::None;
    payload: Optional<Payload> = std::option::Option::None;
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

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

[[nodiscard]] std::filesystem::path make_temp_project(std::string_view name) {
    const auto root =
        std::filesystem::temp_directory_path() / ("ahfl_flow_condition_" + std::string(name));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

struct ProjectTypeCheckResult {
    std::string source;
    std::optional<ahfl::SourceId> app_source_id;
    ahfl::TypeCheckResult typecheck;
};

[[nodiscard]] ProjectTypeCheckResult typecheck_project_source(std::string_view source,
                                                              std::string_view name) {
    const auto root = make_temp_project(name);
    const auto main_path = root / "app" / "main.ahfl";
    std::string project_source = "module app::main;\n" + std::string{source};
    write_file(main_path, project_source);

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {root, std::filesystem::path{"std"}},
        .inject_prelude = true,
    });
    if (parse_result.has_errors()) {
        std::ostringstream ss;
        parse_result.diagnostics.render(ss);
        FAIL("project parse diagnostics:\n" << ss.str());
    }
    REQUIRE_FALSE(parse_result.has_errors());

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        std::ostringstream ss;
        resolve_result.diagnostics.render(ss);
        FAIL("project resolve diagnostics:\n" << ss.str());
    }
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto typecheck = type_checker.check(parse_result.graph, resolve_result);
    if (typecheck.has_errors()) {
        std::ostringstream ss;
        typecheck.diagnostics.render(ss);
        FAIL("project typecheck diagnostics:\n" << ss.str());
    }
    REQUIRE_FALSE(typecheck.has_errors());

    const auto context_symbol = typecheck.typed_program.find_local_symbol(
        ahfl::SymbolNamespace::Types, "Context", "app::main");
    REQUIRE(context_symbol.has_value());
    REQUIRE(context_symbol->get().source_id.has_value());

    return ProjectTypeCheckResult{
        .source = std::move(project_source),
        .app_source_id = context_symbol->get().source_id,
        .typecheck = std::move(typecheck),
    };
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

[[nodiscard]] const ahfl::TypedExpr *
find_project_expr_by_nth(const ProjectTypeCheckResult &result,
                         std::string_view needle,
                         std::size_t n) {
    return result.typecheck.typed_program.find_expr_by_range(
        range_of_nth(result.source, needle, n), result.app_source_id);
}

void check_std_string_option(ahfl::TypePtr type) {
    REQUIRE(type != nullptr);
    CHECK(type->describe() == "std::option::Option<String>");
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
    // P2 (RFC §6): the ExprSyntax dispatcher routes lambda (and any future
    // unspecialised kind) here. Closures carry no top-level binary condition,
    // so this is a no-op.
    void visit_unknown(const ahfl::ast::ExprSyntax &) {}
};

} // namespace

// ---------------------------------------------------------------------------
// TC1: Optional simple narrow — `if (x != none)` unwraps x in the then-block.
// ---------------------------------------------------------------------------
TEST_CASE("Optional simple narrow unwraps type on then branch") {
    const std::string body =
        "        if (ctx.token != std::option::Option::None) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto legacy_source = render_body(body, kSkeletonString);

    const auto project = typecheck_project_source(legacy_source, "simple_narrow_project");
    const auto &source = project.source;
    const auto &result = project.typecheck;

    // The LHS of the `!= none` comparison is evaluated *before* the narrowing
    // merges, so it must keep the declared std nominal Option<String>.
    const auto cmp_range = range_of(source, "ctx.token != std::option::Option::None");
    const auto cmp_lhs =
        std::find_if(result.typed_program.expressions.begin(),
                     result.typed_program.expressions.end(),
                     [cmp_range, source_id = project.app_source_id](const ahfl::TypedExpr &e) {
                         return e.semantic_name == "ctx.token" &&
                                e.source_id == source_id &&
                                e.range.begin_offset >= cmp_range.begin_offset &&
                                e.range.end_offset <= cmp_range.end_offset;
                     });
    REQUIRE(cmp_lhs != result.typed_program.expressions.end());
    REQUIRE(cmp_lhs->type != nullptr);
    CHECK(cmp_lhs->type->describe() == "std::option::Option<String>");

    // The ctx.token inside the then-block struct literal must be narrowed to String.
    // It is the second source occurrence of `ctx.token`.
    const auto *then_use = find_project_expr_by_nth(project, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());
}

TEST_CASE("Optional flow narrowing unwraps std nominal Option") {
    ahfl::TypeContext types;
    ahfl::TypeEnvironment environment;

    const auto string_type = types.string();
    const auto option_type = types.enum_type(
        "std::option::Option", std::optional<ahfl::SymbolId>{}, std::vector{string_type});
    const ahfl::Place token_place{.root = "ctx", .members = {"token"}};

    ahfl::FlowFacts facts;
    facts.add(ahfl::TypeFact{.place = token_place, .kind = ahfl::TypeFactKind::IsNotNone});

    const auto narrowed = ahfl::apply_expression_flow_narrowing(
        option_type, token_place, facts, environment, types);
    REQUIRE(narrowed != nullptr);
    CHECK(narrowed->holds<ahfl::types::StringT>());
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
        "        if (ctx.token != std::option::Option::None && true) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto legacy_source = render_body(body, kSkeletonString);

    const auto project = typecheck_project_source(legacy_source, "and_chain_narrow_project");

    // Second text occurrence of `ctx.token`: the then-block value. It must
    // have been narrowed to String (i.e. the && chain didn't drop the
    // when_true narrowing facts from the first conjunct).
    const auto *then_use = find_project_expr_by_nth(project, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());

    // Sanity: first occurrence (LHS of `!= none`) is still Optional because
    // the narrowing hasn't merged yet when the comparison's LHS is evaluated.
    const auto *cmp_lhs = find_project_expr_by_nth(project, "ctx.token", 1);
    REQUIRE(cmp_lhs != nullptr);
    check_std_string_option(cmp_lhs->type);
}

TEST_CASE("Optional narrowing applies inside nested if conditions") {
    const std::string body =
        "        if (ctx.token != std::option::Option::None) {\n"
        "            if (ctx.token == input.fallback) {\n"
        "                return Response { value: ctx.token };\n"
        "            }\n"
        "        }\n"
        "        return Response { value: input.fallback };\n";
    const auto legacy_source = render_body(body, kSkeletonString);

    const auto project = typecheck_project_source(legacy_source, "nested_if_narrow_project");

    const auto *inner_condition_use = find_project_expr_by_nth(project, "ctx.token", 2);
    REQUIRE(inner_condition_use != nullptr);
    REQUIRE(inner_condition_use->type != nullptr);
    CHECK(inner_condition_use->type->holds<ahfl::types::StringT>());
}

// ---------------------------------------------------------------------------
// TC3: Negated condition must NOT narrow on the then branch.
// ---------------------------------------------------------------------------
TEST_CASE("Negated condition does not incorrectly narrow on then branch") {
    const std::string body =
        "        if (!(ctx.token != std::option::Option::None)) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: std::option::Option::Some(input.fallback) };\n"
        "        }\n";
    const auto legacy_source = render_body(body, kSkeletonOptional);

    const auto project = typecheck_project_source(legacy_source, "negated_no_narrow_project");
    const auto &result = project.typecheck;

    // Every textual use of `ctx.token` must remain Optional<String>. The use
    // inside the then-block (after negation) must NOT have been unwrapped;
    // `!(x != none)` triggers the complement of the inner comparison, and the
    // checker must conservatively avoid unwrapping.
    for (const auto &expr : result.typed_program.expressions) {
        if (expr.semantic_name == "ctx.token" && expr.source_id == project.app_source_id) {
            INFO("ctx.token at offset " << expr.range.begin_offset);
            check_std_string_option(expr.type);
        }
    }
}

// ---------------------------------------------------------------------------
// TC4: Assignment to a place invalidates narrowing facts for it.
// ---------------------------------------------------------------------------
TEST_CASE("Assignment invalidates earlier narrowing for the same place") {
    const std::string body =
        "        if (ctx.token != std::option::Option::None) {\n"
        "            ctx.token = std::option::Option::None;\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: std::option::Option::Some(input.fallback) };\n"
        "        }\n";
    const auto legacy_source = render_body(body, kSkeletonOptional);

    const auto project = typecheck_project_source(legacy_source, "assignment_invalidation_project");
    const auto &result = project.typecheck;

    // Uses of ctx.token, in source order:
    //   1) LHS of `!= none`              => Optional<String> (before narrowing)
    //   2) Target path in `= none`       => Optional<String>
    //   3) Value `ctx.token` in the return struct literal AFTER assignment
    //        => must be Optional<String> again (invalidation cleared narrowing).
    const auto *post_assign = find_project_expr_by_nth(project, "ctx.token", 3);
    REQUIRE(post_assign != nullptr);
    check_std_string_option(post_assign->type);
    CHECK_FALSE(post_assign->type->holds<ahfl::types::StringT>());

    // Sanity: at least two Optional<String> occurrences (LHS and post-assign).
    const auto optional_tokens =
        std::count_if(result.typed_program.expressions.begin(),
                      result.typed_program.expressions.end(),
                      [source_id = project.app_source_id](const ahfl::TypedExpr &e) {
                          return e.semantic_name == "ctx.token" &&
                                 e.source_id == source_id &&
                                 e.type &&
                                 e.type->describe() == "std::option::Option<String>";
                      });
    CHECK(optional_tokens >= 2);
}

// ---------------------------------------------------------------------------
// TC5: ConditionFacts records complementary when_true / when_false edges.
// ---------------------------------------------------------------------------
TEST_CASE("ConditionFacts records complementary then/else facts") {
    const std::string body =
        "        if (ctx.token != std::option::Option::None) {\n"
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

TEST_CASE("ConditionFacts treats qualified None value as Optional none comparison") {
    const std::string body =
        "        if (ctx.token != std::option::Option::None) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto source = render_body(body, kSkeletonString);

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("qualified_none_condition_facts.ahfl", source);
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
                if (stmt->kind != ahfl::ast::StatementSyntaxKind::If || !stmt->if_stmt ||
                    stmt->if_stmt->condition == nullptr) {
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
}

// ---------------------------------------------------------------------------
// TC6: Symmetric `none != x` behaves just like `x != none`.
// ---------------------------------------------------------------------------
TEST_CASE("Reversed none comparison (none != x) narrows symmetrically") {
    const std::string body =
        "        if (std::option::Option::None != ctx.token) {\n"
        "            return Response { value: ctx.token };\n"
        "        } else {\n"
        "            return Response { value: input.fallback };\n"
        "        }\n";
    const auto legacy_source = render_body(body, kSkeletonString);

    const auto project = typecheck_project_source(legacy_source, "reversed_none_narrow_project");
    const auto &result = project.typecheck;

    // Only source occurrence of `ctx.token` inside body is the RHS of the
    // comparison (evaluated before narrowing merges, so still Optional) and
    // the second occurrence inside the then-block return which must be narrowed
    // to String.
    const auto *cmp_rhs = find_project_expr_by_nth(project, "ctx.token", 1);
    REQUIRE(cmp_rhs != nullptr);
    check_std_string_option(cmp_rhs->type);

    const auto *then_use = find_project_expr_by_nth(project, "ctx.token", 2);
    REQUIRE(then_use != nullptr);
    REQUIRE(then_use->type != nullptr);
    CHECK(then_use->type->holds<ahfl::types::StringT>());

    // Sanity: at least one narrowed String occurrence in the whole program.
    const auto narrowed_tokens =
        std::count_if(result.typed_program.expressions.begin(),
                      result.typed_program.expressions.end(),
                      [source_id = project.app_source_id](const ahfl::TypedExpr &e) {
                          return e.semantic_name == "ctx.token" &&
                                 e.source_id == source_id &&
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

// ===========================================================================
// P3a (RFC corelib-rfc.zh.md §3.2.2 / type-system §1.3 / §1.4)
// trait + impl: grammar / AST / parser surface.
//
// P3a is purely additive — it only needs the AST surface to parse and the
// resolver/typecheck passes to walk it without crashing. Symbol registration,
// trait resolution, coherence, signature matching and associated-type
// defaulting land in P3b. These tests therefore assert only the surface
// P3a commits to:
//
//   - a trait with a super-trait, generic params, a method signature and an
//     associated type parses into a TraitDecl with the expected item shape;
//   - an inherent impl and a trait impl parse into ImplDecl nodes with the
//     expected method / assoc-item counts and the inherent-vs-trait split;
//   - the resolver and typechecker treat trait/impl declarations as no-ops
//     (P3a) so a program containing them resolves and typechecks clean.
// ===========================================================================

namespace {

[[nodiscard]] const ahfl::ast::TraitDecl *
find_trait_decl(const ahfl::ast::Program &program, std::string_view name) {
    for (const auto &decl : program.declarations) {
        if (decl->kind != ahfl::ast::NodeKind::TraitDecl) {
            continue;
        }
        const auto *trait = static_cast<const ahfl::ast::TraitDecl *>(decl.get());
        if (trait->name == name) {
            return trait;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t count_impl_decls(const ahfl::ast::Program &program) {
    std::size_t count = 0;
    for (const auto &decl : program.declarations) {
        if (decl->kind == ahfl::ast::NodeKind::ImplDecl) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] const ahfl::ast::ImplDecl *
first_impl_decl(const ahfl::ast::Program &program) {
    for (const auto &decl : program.declarations) {
        if (decl->kind == ahfl::ast::NodeKind::ImplDecl) {
            return static_cast<const ahfl::ast::ImplDecl *>(decl.get());
        }
    }
    return nullptr;
}

// Parse-only helper: surfaces the first parse diagnostic via MESSAGE so a
// parse failure is actionable rather than a silent boolean. The ParseResult
// is returned by value so the caller owns the program for the test's lifetime.
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

} // namespace

TEST_CASE("Trait declaration parses with super-trait, method and assoc type") {
    const std::string source = R"AHFL(
trait Ord: Eq {
    fn compare(other: Self) -> Int;
    type Item;
}
)AHFL";

    const auto parse_result = parse_only("trait_ord.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *trait = find_trait_decl(*program, "Ord");
    REQUIRE(trait != nullptr);
    CHECK(trait->name == "Ord");
    CHECK(trait->super_traits.size() == 1);
    CHECK(trait->super_traits[0]->spelling() == "Eq");
    CHECK(trait->items.size() == 2);

    // Item ordering follows source order: method first, assoc type second.
    CHECK(trait->items[0]->kind == ahfl::ast::TraitItemKind::Fn);
    CHECK(trait->items[0]->name == "compare");
    REQUIRE_FALSE(trait->items[0]->params.empty());
    CHECK(trait->items[0]->params[0]->name == "other");
    CHECK(trait->items[0]->return_type->spelling() == "Int");

    CHECK(trait->items[1]->kind == ahfl::ast::TraitItemKind::AssocType);
    REQUIRE(trait->items[1]->assoc_type != nullptr);
    CHECK(trait->items[1]->assoc_type->name == "Item");
}

TEST_CASE("Generic trait parses type params and default assoc type") {
    const std::string source = R"AHFL(
trait Foldable<T> {
    fn fold<U>(acc: U, item: T) -> U;
    type Element = T;
}
)AHFL";

    const auto parse_result = parse_only("trait_foldable.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *trait = find_trait_decl(*program, "Foldable");
    REQUIRE(trait != nullptr);
    REQUIRE(trait->type_params.size() == 1);
    CHECK(trait->type_params[0]->name == "T");

    REQUIRE(trait->items.size() == 2);
    CHECK(trait->items[0]->kind == ahfl::ast::TraitItemKind::Fn);
    CHECK(trait->items[0]->name == "fold");
    REQUIRE(trait->items[0]->type_params.size() == 1);
    CHECK(trait->items[0]->type_params[0]->name == "U");

    CHECK(trait->items[1]->kind == ahfl::ast::TraitItemKind::AssocType);
    REQUIRE(trait->items[1]->assoc_type != nullptr);
    CHECK(trait->items[1]->assoc_type->name == "Element");
    CHECK(trait->items[1]->assoc_type->default_type->spelling() == "T");
}

TEST_CASE("Inherent impl and trait impl parse with methods and assoc items") {
    const std::string source = R"AHFL(
struct Pair {
    a: Int;
    b: Int;
}

impl Pair {
    fn sum() -> Int {
        return 0;
    }
}

impl<T> Display for List<T> {
    fn fmt() -> String {
        return "";
    }
    type Output = T;
}
)AHFL";

    const auto parse_result = parse_only("impl_pair.ahfl", source);
    const auto *program = parse_result.program.get();
    CHECK(count_impl_decls(*program) == 2);

    // First impl: inherent (no trait_ref).
    const auto *inherent = first_impl_decl(*program);
    REQUIRE(inherent != nullptr);
    CHECK(inherent->trait_ref == nullptr);
    CHECK(inherent->target_type->spelling() == "Pair");
    CHECK(inherent->methods.size() == 1);
    CHECK(inherent->methods[0]->name == "sum");
    REQUIRE(inherent->methods[0]->body != nullptr);
    CHECK(inherent->assoc_items.empty());

    // Second impl: trait impl (trait_ref present, generic params, assoc item).
    const ahfl::ast::ImplDecl *trait_impl = nullptr;
    for (const auto &decl : program->declarations) {
        if (decl->kind != ahfl::ast::NodeKind::ImplDecl) {
            continue;
        }
        const auto *impl = static_cast<const ahfl::ast::ImplDecl *>(decl.get());
        if (impl->trait_ref != nullptr) {
            trait_impl = impl;
            break;
        }
    }
    REQUIRE(trait_impl != nullptr);
    REQUIRE(trait_impl->type_params.size() == 1);
    CHECK(trait_impl->type_params[0]->name == "T");
    CHECK(trait_impl->trait_ref->spelling() == "Display");
    CHECK(trait_impl->target_type->spelling() == "List<T>");
    CHECK(trait_impl->methods.size() == 1);
    CHECK(trait_impl->methods[0]->name == "fmt");
    REQUIRE(trait_impl->assoc_items.size() == 1);
    CHECK(trait_impl->assoc_items[0]->name == "Output");
    CHECK(trait_impl->assoc_items[0]->type->spelling() == "T");
}

TEST_CASE("Trait and impl declarations resolve and typecheck as no-ops (P3a)") {
    const std::string source = R"AHFL(
trait Describe {
    fn describe() -> String;
}

struct Widget {
    id: Int;
}

impl Describe for Widget {
    fn describe() -> String {
        return "widget";
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("trait_describe.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto typecheck_result =
        type_checker.check(*parse_result.program, resolve_result);
    CHECK_FALSE(typecheck_result.has_errors());
}
