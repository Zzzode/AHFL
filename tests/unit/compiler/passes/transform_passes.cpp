#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/ir_equal.hpp"
#include "compiler/passes/expr_canonicalization.hpp"
#include "compiler/passes/pass_manager.hpp"
#include "compiler/passes/temporal_simplification.hpp"
#include "compiler/passes/workflow_simplification.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Direct unit tests for the semantic-IR transformation passes. The sibling TU
// `pass_manager.cpp` locks the *manager* (filtering, analysis invalidation,
// fixpoint scheduling) using synthetic no-op / always-modify passes; it never
// exercises the real transformation semantics. Those were previously locked
// only by the CLI golden gates (`ahflc.passes.*`), which are coarse: one canned
// input/output pair per pass, and unable to express a negative (a
// false-redundancy guard, an operand that must be preserved) or an
// idempotence/fixpoint property.
//
// This TU hand-builds `ir::Program` values and drives each pass directly:
//
//   * WorkflowSimplificationPass — diamond vs. DAG edge cases where an `after`
//     edge is NOT transitively implied (the false-redundancy guard), removal
//     order determinism, single-run idempotence.
//   * TemporalSimplificationPass — nested always/always + not/not collapse,
//     `next` / distinct-operator non-collapse, idempotence under one run,
//     fixpoint reached within a single PassManager run.
//   * ExprCanonicalizationPass — all 8 `&&` / `||` constant rules, operand
//     preservation, double-negation elimination, the arena-embedded expression
//     form inside temporal formulas, idempotence.

namespace {

using namespace ahfl::ir;
using namespace ahfl::passes;

// ---------------------------------------------------------------------------
// Expression builders (shared arena, index handles — CLAUDE.md Principle 3).
// ---------------------------------------------------------------------------

[[nodiscard]] ExprRef make_bool(ExprArena &arena, bool value) {
    return arena.make(BoolLiteralExpr{.value = value});
}

[[nodiscard]] ExprRef make_int(ExprArena &arena, std::string spelling) {
    return arena.make(IntegerLiteralExpr{.spelling = std::move(spelling)});
}

[[nodiscard]] ExprRef make_path(ExprArena &arena, std::string root) {
    Path path;
    path.root_kind = PathRootKind::Identifier;
    path.root_name = std::move(root);
    return arena.make(PathExpr{.path = std::move(path)});
}

[[nodiscard]] ExprRef make_call(ExprArena &arena,
                                std::string callee,
                                std::vector<ExprRef> args = {}) {
    return arena.make(CallExpr{.callee = std::move(callee), .arguments = std::move(args)});
}

[[nodiscard]] ExprRef make_unary(ExprArena &arena, ExprUnaryOp op, ExprRef operand) {
    return arena.make(UnaryExpr{.op = op, .operand = operand});
}

[[nodiscard]] ExprRef make_binary(ExprArena &arena, ExprBinaryOp op, ExprRef lhs, ExprRef rhs) {
    return arena.make(BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

// ---------------------------------------------------------------------------
// Temporal-expression builders.
// ---------------------------------------------------------------------------

[[nodiscard]] TemporalExprPtr make_temporal_unary(TemporalUnaryOp op, TemporalExprPtr operand) {
    auto expr = std::make_unique<TemporalExpr>();
    expr->node = TemporalUnaryExpr{.op = op, .operand = std::move(operand)};
    return expr;
}

[[nodiscard]] TemporalExprPtr make_temporal_binary(TemporalBinaryOp op,
                                                   TemporalExprPtr lhs,
                                                   TemporalExprPtr rhs) {
    auto expr = std::make_unique<TemporalExpr>();
    expr->node = TemporalBinaryExpr{.op = op, .lhs = std::move(lhs), .rhs = std::move(rhs)};
    return expr;
}

[[nodiscard]] TemporalExprPtr make_temporal_embedded(ExprRef embedded) {
    auto expr = std::make_unique<TemporalExpr>();
    expr->node = EmbeddedTemporalExpr{.expr = embedded};
    return expr;
}

[[nodiscard]] TemporalExprPtr make_temporal_called(std::string capability) {
    auto expr = std::make_unique<TemporalExpr>();
    expr->node = CalledTemporalExpr{.capability = std::move(capability)};
    return expr;
}

// Structural fingerprint of a temporal tree; operator enum ordinals are folded
// in so a wrong-operator collapse cannot pass by rendering the same shape.
[[nodiscard]] std::string render_temporal(const TemporalExpr &expr) {
    return std::visit(
        ahfl::Overloaded{
            [](const EmbeddedTemporalExpr &value) {
                return std::string{"embedded("} + (value.expr ? "expr" : "null") + ")";
            },
            [](const CalledTemporalExpr &value) { return "called(" + value.capability + ")"; },
            [](const InStateTemporalExpr &value) { return "in_state(" + value.state + ")"; },
            [](const RunningTemporalExpr &value) { return "running(" + value.node + ")"; },
            [](const CompletedTemporalExpr &value) { return "completed(" + value.node + ")"; },
            [](const TemporalUnaryExpr &value) {
                return "U" + std::to_string(static_cast<int>(value.op)) + "(" +
                       render_temporal(*value.operand) + ")";
            },
            [](const TemporalBinaryExpr &value) {
                return "B" + std::to_string(static_cast<int>(value.op)) + "(" +
                       render_temporal(*value.lhs) + "," + render_temporal(*value.rhs) + ")";
            },
        },
        expr.node);
}

// ---------------------------------------------------------------------------
// Program / workflow / contract builders.
// ---------------------------------------------------------------------------

struct NodeSpec {
    std::string name;
    std::vector<std::string> after;
};

[[nodiscard]] WorkflowDecl make_workflow(std::string name, std::vector<NodeSpec> specs) {
    WorkflowDecl wf;
    wf.name = name;
    for (auto &spec : specs) {
        WorkflowNode node;
        node.name = std::move(spec.name);
        node.after = std::move(spec.after);
        wf.nodes.push_back(std::move(node));
    }
    return wf;
}

[[nodiscard]] WorkflowDecl *workflow_in(Program &program) {
    for (auto &decl : program.declarations) {
        if (auto *wf = std::get_if<WorkflowDecl>(&decl)) {
            return wf;
        }
    }
    return nullptr;
}

[[nodiscard]] const WorkflowNode *find_node(const WorkflowDecl &wf, std::string_view name) {
    for (const auto &node : wf.nodes) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

// Direct `after` dependencies of one node, in stored order.
[[nodiscard]] std::vector<std::string> deps_of(const WorkflowDecl &wf, std::string_view name) {
    const auto *node = find_node(wf, name);
    return node == nullptr ? std::vector<std::string>{} : node->after;
}

// Append a clause to the program's contract declaration, creating it on first
// use, and return it. Tests that assert on clause indices therefore see one
// contract holding every clause they added.
ContractDecl &add_contract(Program &program,
                           ContractClauseKind kind,
                           std::variant<ExprRef, TemporalExprPtr> value) {
    for (auto &decl : program.declarations) {
        if (auto *contract = std::get_if<ContractDecl>(&decl)) {
            ContractClause clause;
            clause.kind = kind;
            clause.value = std::move(value);
            contract->clauses.push_back(std::move(clause));
            return *contract;
        }
    }

    ContractDecl contract;
    ContractClause clause;
    clause.kind = kind;
    clause.value = std::move(value);
    contract.clauses.push_back(std::move(clause));
    program.declarations.push_back(std::move(contract));
    return std::get<ContractDecl>(program.declarations.back());
}

[[nodiscard]] ContractDecl &contract_in(Program &program) {
    for (auto &decl : program.declarations) {
        if (auto *contract = std::get_if<ContractDecl>(&decl)) {
            return *contract;
        }
    }
    FAIL("program has no contract declaration");
    // Unreachable; kept for a well-typed return path.
    return std::get<ContractDecl>(program.declarations.at(0));
}

// The ordinary expression carried by a clause, whether written bare or wrapped
// in a temporal formula. For the temporal form this descends through the
// temporal operators to the first embedded payload.
[[nodiscard]] ExprRef first_embedded_expr(const TemporalExpr &expr) {
    return std::visit(
        ahfl::Overloaded{
            [](const EmbeddedTemporalExpr &value) { return value.expr; },
            [](const CalledTemporalExpr &) { return ExprRef{nullptr}; },
            [](const InStateTemporalExpr &) { return ExprRef{nullptr}; },
            [](const RunningTemporalExpr &) { return ExprRef{nullptr}; },
            [](const CompletedTemporalExpr &) { return ExprRef{nullptr}; },
            [](const TemporalUnaryExpr &value) {
                return value.operand ? first_embedded_expr(*value.operand) : ExprRef{nullptr};
            },
            [](const TemporalBinaryExpr &value) {
                if (value.lhs) {
                    if (auto found = first_embedded_expr(*value.lhs)) {
                        return found;
                    }
                }
                return value.rhs ? first_embedded_expr(*value.rhs) : ExprRef{nullptr};
            },
        },
        expr.node);
}

[[nodiscard]] ExprRef clause_expr(ContractDecl &contract, std::size_t index) {
    auto &clause = contract.clauses.at(index);
    if (auto *expr = std::get_if<ExprRef>(&clause.value)) {
        return *expr;
    }
    return first_embedded_expr(*std::get<TemporalExprPtr>(clause.value));
}

[[nodiscard]] const TemporalExpr &clause_temporal(const ContractDecl &contract, std::size_t index) {
    return *std::get<TemporalExprPtr>(contract.clauses.at(index).value);
}

} // namespace

// ===========================================================================
// ExprCanonicalizationPass
// ===========================================================================

TEST_CASE("ExprCanonicalization: all 8 && / || constant rules fold") {
    Program program;
    ExprArena &arena = program.expr_arena;

    // A fresh non-constant operand per rule (identity is irrelevant, the tree
    // shape is what the assertions compare).
    const auto pred = [&] { return make_call(arena, "pkg::ready", {make_path(arena, "input")}); };

    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::And, make_bool(arena, true), pred())); // 0
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::And, pred(), make_bool(arena, true))); // 1
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::And, make_bool(arena, false), pred())); // 2
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::And, pred(), make_bool(arena, false))); // 3
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::Or, make_bool(arena, false), pred())); // 4
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::Or, pred(), make_bool(arena, false))); // 5
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::Or, make_bool(arena, true), pred())); // 6
    add_contract(program, ContractClauseKind::Requires,
                 make_binary(arena, ExprBinaryOp::Or, pred(), make_bool(arena, true))); // 7
    // 8: the reference operand alone, for the "operand preserved" assertions.
    add_contract(program, ContractClauseKind::Requires, pred());

    ExprCanonicalizationPass pass;
    CHECK(pass.run(program));

    auto &contract = contract_in(program);
    REQUIRE(contract.clauses.size() == 9);
    const auto operand = clause_expr(contract, 8);
    REQUIRE(operand);

    // true && p → p ; p && true → p ; false || p → p ; p || false → p
    for (const std::size_t index :
         {std::size_t{0}, std::size_t{1}, std::size_t{4}, std::size_t{5}}) {
        CHECK(exprs_structurally_equal(clause_expr(contract, index), operand));
    }

    // false && p → false ; p && false → false
    for (const std::size_t index : {std::size_t{2}, std::size_t{3}}) {
        const auto result = clause_expr(contract, index);
        REQUIRE(result);
        const auto *literal = std::get_if<BoolLiteralExpr>(&result->node);
        REQUIRE(literal != nullptr);
        CHECK(literal->value == false);
    }

    // true || p → true ; p || true → true
    for (const std::size_t index : {std::size_t{6}, std::size_t{7}}) {
        const auto result = clause_expr(contract, index);
        REQUIRE(result);
        const auto *literal = std::get_if<BoolLiteralExpr>(&result->node);
        REQUIRE(literal != nullptr);
        CHECK(literal->value == true);
    }
}

TEST_CASE("ExprCanonicalization: non-constant operands survive the identity folds") {
    Program program;
    ExprArena &arena = program.expr_arena;

    // `(true && (x == 0)) || (y != 1)` — both comparisons must reach the result
    // tree unchanged; the fold must not collapse the whole clause to a literal.
    const auto comparison_lhs =
        make_binary(arena, ExprBinaryOp::Equal, make_path(arena, "x"), make_int(arena, "0"));
    const auto comparison_rhs =
        make_binary(arena, ExprBinaryOp::NotEqual, make_path(arena, "y"), make_int(arena, "1"));
    add_contract(program,
                 ContractClauseKind::Requires,
                 make_binary(arena,
                             ExprBinaryOp::Or,
                             make_binary(
                                 arena, ExprBinaryOp::And, make_bool(arena, true), comparison_lhs),
                             comparison_rhs));

    ExprCanonicalizationPass pass;
    CHECK(pass.run(program));

    auto &contract = contract_in(program);
    const auto result = clause_expr(contract, 0);
    REQUIRE(result);

    const auto *binary = std::get_if<BinaryExpr>(&result->node);
    REQUIRE(binary != nullptr);
    CHECK(binary->op == ExprBinaryOp::Or);
    CHECK(exprs_structurally_equal(binary->lhs, comparison_lhs));
    CHECK(exprs_structurally_equal(binary->rhs, comparison_rhs));
}

TEST_CASE("ExprCanonicalization: double negation eliminates, including nested pairs") {
    Program program;
    ExprArena &arena = program.expr_arena;

    const auto pred = [&] { return make_call(arena, "pkg::ready"); };
    add_contract(program, ContractClauseKind::Requires,
                 make_unary(arena, ExprUnaryOp::Not, make_unary(arena, ExprUnaryOp::Not, pred())));
    add_contract(program,
                 ContractClauseKind::Requires,
                 make_unary(arena,
                            ExprUnaryOp::Not,
                            make_unary(arena,
                                       ExprUnaryOp::Not,
                                       make_unary(arena, ExprUnaryOp::Not,
                                                  make_unary(arena, ExprUnaryOp::Not, pred())))));

    ExprCanonicalizationPass pass;
    CHECK(pass.run(program));

    auto &contract = contract_in(program);
    for (const std::size_t index : {std::size_t{0}, std::size_t{1}}) {
        const auto result = clause_expr(contract, index);
        REQUIRE(result);
        CHECK(std::holds_alternative<CallExpr>(result->node));
    }
}

TEST_CASE("ExprCanonicalization: a single Not is not eliminated") {
    Program program;
    ExprArena &arena = program.expr_arena;

    add_contract(program, ContractClauseKind::Requires,
                 make_unary(arena, ExprUnaryOp::Not, make_call(arena, "pkg::ready")));

    ExprCanonicalizationPass pass;
    CHECK_FALSE(pass.run(program));

    auto &contract = contract_in(program);
    const auto result = clause_expr(contract, 0);
    REQUIRE(result);
    const auto *unary = std::get_if<UnaryExpr>(&result->node);
    REQUIRE(unary != nullptr);
    CHECK(unary->op == ExprUnaryOp::Not);
}

TEST_CASE("ExprCanonicalization: reaches expressions embedded inside temporal formulas") {
    Program program;
    ExprArena &arena = program.expr_arena;

    // An ordinary expression wrapped by a temporal operator must be
    // canonicalized exactly like a bare clause expression. Before this was
    // covered the two forms diverged: `requires: true && p` folded while
    // `invariant: always (true && p)` kept its redundant conjunct, because the
    // pass only visited the clause-level `ExprRef` alternative.
    add_contract(program,
                 ContractClauseKind::Invariant,
                 make_temporal_unary(
                     TemporalUnaryOp::Always,
                     make_temporal_embedded(make_binary(
                         arena,
                         ExprBinaryOp::And,
                         make_bool(arena, true),
                         make_call(arena, "pkg::ready")))));

    // The same payload nested under a binary temporal operator, so the walk is
    // exercised through both operator shapes.
    add_contract(program,
                 ContractClauseKind::Ensures,
                 make_temporal_binary(
                     TemporalBinaryOp::Until,
                     make_temporal_called("pkg::Call"),
                     make_temporal_embedded(make_binary(
                         arena,
                         ExprBinaryOp::Or,
                         make_call(arena, "pkg::other"),
                         make_bool(arena, false)))));

    ExprCanonicalizationPass pass;
    CHECK(pass.run(program));

    auto &contract = contract_in(program);
    REQUIRE(std::holds_alternative<TemporalExprPtr>(contract.clauses.at(0).value));
    REQUIRE(std::holds_alternative<TemporalExprPtr>(contract.clauses.at(1).value));

    // The temporal wrapper is preserved; only the embedded payload folds.
    const auto *unary = std::get_if<TemporalUnaryExpr>(&clause_temporal(contract, 0).node);
    REQUIRE(unary != nullptr);
    CHECK(unary->op == TemporalUnaryOp::Always);

    const auto expr0 = clause_expr(contract, 0);
    REQUIRE(expr0);
    CHECK(std::holds_alternative<CallExpr>(expr0->node));

    const auto expr1 = clause_expr(contract, 1);
    REQUIRE(expr1);
    CHECK(std::holds_alternative<CallExpr>(expr1->node));
}

TEST_CASE("ExprCanonicalization: idempotent, and a no-op on canonical input") {
    Program program;
    ExprArena &arena = program.expr_arena;

    const auto payload = make_binary(
        arena,
        ExprBinaryOp::And,
        make_binary(arena, ExprBinaryOp::Or, make_call(arena, "pkg::a"), make_bool(arena, false)),
        make_bool(arena, true));
    add_contract(program, ContractClauseKind::Requires, payload);
    add_contract(program, ContractClauseKind::Invariant,
                 make_temporal_unary(TemporalUnaryOp::Always, make_temporal_embedded(payload)));
    add_contract(program, ContractClauseKind::Ensures,
                 make_temporal_unary(TemporalUnaryOp::Always, make_temporal_called("pkg::Call")));

    ExprCanonicalizationPass pass;
    CHECK(pass.run(program));
    // Second run must be a no-op: the canonical form is a fixpoint.
    CHECK_FALSE(pass.run(program));
}

// ===========================================================================
// TemporalSimplificationPass
// ===========================================================================

namespace {

// Run the pass over a program whose first contract holds one temporal clause
// and return the rendered result.
[[nodiscard]] std::string run_temporal_on_single_clause(Program &program) {
    TemporalSimplificationPass pass;
    static_cast<void>(pass.run(program));
    auto &contract = contract_in(program);
    return render_temporal(clause_temporal(contract, 0));
}

} // namespace

TEST_CASE("TemporalSimplification: always/always and eventually/eventually collapse") {
    {
        Program program;
        add_contract(program, ContractClauseKind::Invariant,
                     make_temporal_unary(TemporalUnaryOp::Always,
                                         make_temporal_unary(TemporalUnaryOp::Always,
                                                             make_temporal_called("pkg::Call"))));
        CHECK(run_temporal_on_single_clause(program) == "U0(called(pkg::Call))");
    }
    {
        Program program;
        add_contract(program, ContractClauseKind::Invariant,
                     make_temporal_unary(TemporalUnaryOp::Eventually,
                                         make_temporal_unary(TemporalUnaryOp::Eventually,
                                                             make_temporal_called("pkg::Call"))));
        CHECK(run_temporal_on_single_clause(program) == "U1(called(pkg::Call))");
    }
}

TEST_CASE("TemporalSimplification: one run fully collapses a 3-deep always nest") {
    // `simplify_temporal` recurses before collapsing, so a single run reaches
    // the fixpoint for arbitrarily deep replications of one operator.
    Program program;
    add_contract(program, ContractClauseKind::Invariant,
                 make_temporal_unary(
                     TemporalUnaryOp::Always,
                     make_temporal_unary(TemporalUnaryOp::Always,
                                         make_temporal_unary(TemporalUnaryOp::Always,
                                                             make_temporal_called("pkg::Call")))));
    CHECK(run_temporal_on_single_clause(program) == "U0(called(pkg::Call))");
}

TEST_CASE("TemporalSimplification: not/not collapses") {
    Program program;
    add_contract(program, ContractClauseKind::Invariant,
                 make_temporal_unary(TemporalUnaryOp::Not,
                                     make_temporal_unary(TemporalUnaryOp::Not,
                                                         make_temporal_called("pkg::Call"))));
    CHECK(run_temporal_on_single_clause(program) == "called(pkg::Call)");
}

TEST_CASE("TemporalSimplification: distinct or idempotent-unsafe operators are NOT collapsed") {
    // always(eventually(p)) and next(next(p)) are semantically distinct from
    // their single-operator forms, and not(always(p)) must keep the negation.
    // This is the guard against an over-eager "collapse any nested unary".
    {
        Program program;
        add_contract(program, ContractClauseKind::Invariant,
                     make_temporal_unary(TemporalUnaryOp::Always,
                                         make_temporal_unary(TemporalUnaryOp::Eventually,
                                                             make_temporal_called("pkg::Call"))));
        CHECK(run_temporal_on_single_clause(program) == "U0(U1(called(pkg::Call)))");
    }
    {
        Program program;
        add_contract(program, ContractClauseKind::Invariant,
                     make_temporal_unary(TemporalUnaryOp::Next,
                                         make_temporal_unary(TemporalUnaryOp::Next,
                                                             make_temporal_called("pkg::Call"))));
        CHECK(run_temporal_on_single_clause(program) == "U2(U2(called(pkg::Call)))");
    }
    {
        Program program;
        add_contract(program, ContractClauseKind::Invariant,
                     make_temporal_unary(TemporalUnaryOp::Not,
                                         make_temporal_unary(TemporalUnaryOp::Always,
                                                             make_temporal_called("pkg::Call"))));
        CHECK(run_temporal_on_single_clause(program) == "U3(U0(called(pkg::Call)))");
    }
}

TEST_CASE("TemporalSimplification: collapses through binary temporal operators") {
    Program program;
    add_contract(program,
                 ContractClauseKind::Invariant,
                 make_temporal_binary(
                     TemporalBinaryOp::Until,
                     make_temporal_unary(TemporalUnaryOp::Always,
                                         make_temporal_unary(TemporalUnaryOp::Always,
                                                             make_temporal_called("pkg::A"))),
                     make_temporal_unary(TemporalUnaryOp::Not,
                                         make_temporal_unary(TemporalUnaryOp::Not,
                                                             make_temporal_called("pkg::B")))));
    CHECK(run_temporal_on_single_clause(program) == "B3(U0(called(pkg::A)),called(pkg::B))");
}

TEST_CASE("TemporalSimplification: simplifies workflow safety and liveness properties") {
    Program program;
    auto wf = make_workflow("W", {{"run", {}}});
    wf.safety.push_back(make_temporal_unary(
        TemporalUnaryOp::Always,
        make_temporal_unary(TemporalUnaryOp::Always,
                            make_temporal_unary(TemporalUnaryOp::Not,
                                                make_temporal_called("pkg::A")))));
    wf.liveness.push_back(make_temporal_unary(TemporalUnaryOp::Eventually,
                                             make_temporal_unary(TemporalUnaryOp::Eventually,
                                                                 make_temporal_called("pkg::B"))));
    program.declarations.push_back(std::move(wf));

    TemporalSimplificationPass pass;
    CHECK(pass.run(program));

    auto *workflow = workflow_in(program);
    REQUIRE(workflow != nullptr);
    CHECK(render_temporal(*workflow->safety.at(0)) == "U0(U3(called(pkg::A)))");
    CHECK(render_temporal(*workflow->liveness.at(0)) == "U1(called(pkg::B))");
}

TEST_CASE("TemporalSimplification: idempotent — no change on a second run") {
    Program program;
    add_contract(program, ContractClauseKind::Invariant,
                 make_temporal_unary(TemporalUnaryOp::Always,
                                     make_temporal_unary(TemporalUnaryOp::Always,
                                                         make_temporal_called("pkg::Call"))));

    TemporalSimplificationPass pass;
    CHECK(pass.run(program));
    CHECK_FALSE(pass.run(program));
}

TEST_CASE("TemporalSimplification: PassManager::run reaches the fixpoint in one pass") {
    Program program;
    add_contract(program, ContractClauseKind::Invariant,
                 make_temporal_unary(
                     TemporalUnaryOp::Always,
                     make_temporal_unary(TemporalUnaryOp::Always,
                                         make_temporal_unary(TemporalUnaryOp::Always,
                                                             make_temporal_called("pkg::Call")))));

    PassManager pm;
    pm.add_pass(std::make_unique<TemporalSimplificationPass>());
    const auto result = pm.run(program);

    CHECK(result.any_modified);
    auto &contract = contract_in(program);
    CHECK(render_temporal(clause_temporal(contract, 0)) == "U0(called(pkg::Call))");
}

// ===========================================================================
// WorkflowSimplificationPass
// ===========================================================================

TEST_CASE("WorkflowSimplification: diamond — the transitive edge is removed") {
    Program program;
    program.declarations.push_back(
        make_workflow("W", {{"first", {}}, {"second", {"first"}}, {"third", {"first", "second"}}}));

    WorkflowSimplificationPass pass;
    CHECK(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "third") == std::vector<std::string>{"second"});
    CHECK(deps_of(*wf, "second") == std::vector<std::string>{"first"});
    CHECK(deps_of(*wf, "first").empty());
}

TEST_CASE("WorkflowSimplification: false-redundancy guard — unrelated deps are kept") {
    // `third` depends on `first` and `second`; neither implies the other, so
    // both are real ordering constraints and MUST survive.
    Program program;
    program.declarations.push_back(
        make_workflow("W", {{"first", {}}, {"second", {}}, {"third", {"first", "second"}}}));

    WorkflowSimplificationPass pass;
    CHECK_FALSE(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "third") == std::vector<std::string>({"first", "second"}));
}

TEST_CASE("WorkflowSimplification: false-redundancy guard — a shared predecessor implies nothing") {
    // `left` and `right` both depend on `base`, but sharing a predecessor is not
    // reachability: neither reaches the other, so `sink` keeps both edges.
    Program program;
    program.declarations.push_back(make_workflow(
        "W", {{"base", {}}, {"left", {"base"}}, {"right", {"base"}}, {"sink", {"left", "right"}}}));

    WorkflowSimplificationPass pass;
    CHECK_FALSE(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "sink") == std::vector<std::string>({"left", "right"}));
}

TEST_CASE("WorkflowSimplification: reachability is directional — an implied reverse edge is not "
          "invented") {
    // `alpha` reaches `beta`, but `beta` does NOT reach `alpha`. A node listing
    // both keeps `beta`'s edge on alpha only when some other direct dep reaches
    // `alpha`; here nothing does, so the direct edge on `alpha` is required.
    Program program;
    program.declarations.push_back(make_workflow(
        "W",
        {{"alpha", {}}, {"beta", {"alpha"}}, {"gamma", {"beta"}}, {"top", {"alpha", "gamma"}}}));

    WorkflowSimplificationPass pass;
    static_cast<void>(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    // gamma reaches alpha (gamma -> beta -> alpha), so top's alpha edge is
    // transitively implied and dropped; gamma stays.
    CHECK(deps_of(*wf, "top") == std::vector<std::string>{"gamma"});
    CHECK(deps_of(*wf, "beta") == std::vector<std::string>{"alpha"});
}

TEST_CASE("WorkflowSimplification: a longer chain is reduced to its immediate predecessor") {
    Program program;
    program.declarations.push_back(make_workflow(
        "W",
        {{"first", {}},
         {"second", {"first"}},
         {"third", {"second"}},
         {"fourth", {"first", "second", "third"}}}));

    WorkflowSimplificationPass pass;
    CHECK(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "fourth") == std::vector<std::string>{"third"});
}

TEST_CASE("WorkflowSimplification: a multi-hop implied edge is removed") {
    Program program;
    program.declarations.push_back(make_workflow(
        "W",
        {{"first", {}},
         {"second", {"first"}},
         {"third", {"second"}},
         {"fourth", {"third", "first"}}}));

    WorkflowSimplificationPass pass;
    CHECK(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "fourth") == std::vector<std::string>{"third"});
}

TEST_CASE("WorkflowSimplification: edge removal is deterministic and input-ordered") {
    // `delta` lists alpha, beta, gamma where only gamma is irreducible. The
    // surviving edge keeps its position in the original list, and the result is
    // identical across repeated runs (no hash-order dependence).
    std::vector<std::string> reference;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Program program;
        program.declarations.push_back(make_workflow(
            "W",
            {{"alpha", {}},
             {"beta", {"alpha"}},
             {"gamma", {"beta"}},
             {"delta", {"alpha", "beta", "gamma"}}}));

        WorkflowSimplificationPass pass;
        CHECK(pass.run(program));

        auto *wf = workflow_in(program);
        REQUIRE(wf != nullptr);
        const auto deps = deps_of(*wf, "delta");
        CHECK(deps == std::vector<std::string>{"gamma"});
        if (reference.empty()) {
            reference = deps;
        }
        CHECK(deps == reference);
    }
}

TEST_CASE("WorkflowSimplification: single-run idempotence") {
    Program program;
    program.declarations.push_back(
        make_workflow("W", {{"first", {}}, {"second", {"first"}}, {"third", {"first", "second"}}}));

    WorkflowSimplificationPass pass;
    CHECK(pass.run(program));
    CHECK_FALSE(pass.run(program));

    auto *wf = workflow_in(program);
    REQUIRE(wf != nullptr);
    CHECK(deps_of(*wf, "third") == std::vector<std::string>{"second"});
}

TEST_CASE("WorkflowSimplification: a single-node workflow and a fully-ordered DAG are left alone") {
    {
        Program program;
        program.declarations.push_back(make_workflow("W", {{"only", {}}}));
        WorkflowSimplificationPass pass;
        CHECK_FALSE(pass.run(program));
        auto *wf = workflow_in(program);
        REQUIRE(wf != nullptr);
        REQUIRE(wf->nodes.size() == 1);
        CHECK(wf->nodes.at(0).after.empty());
    }
    {
        Program program;
        program.declarations.push_back(
            make_workflow("W", {{"a", {}}, {"b", {"a"}}, {"c", {"b"}}, {"d", {"c"}}}));
        WorkflowSimplificationPass pass;
        CHECK_FALSE(pass.run(program));
    }
    {
        // A DAG with no workflow at all must be untouched.
        Program program;
        WorkflowSimplificationPass pass;
        CHECK_FALSE(pass.run(program));
    }
}

// ===========================================================================
// Cross-pass composition (the regression this slice closes)
// ===========================================================================

TEST_CASE("default pipeline: expr canonicalization reaches embedded temporal expressions") {
    // The CLI golden gates (`ahflc.passes.*`) lock only the bare-clause fold.
    // This locks the embedded-in-temporal form on the real default pipeline so
    // the two forms cannot silently diverge again.
    Program program;
    ExprArena &arena = program.expr_arena;

    auto wf = make_workflow("W", {{"run", {}}});
    wf.safety.push_back(make_temporal_unary(
        TemporalUnaryOp::Always,
        make_temporal_unary(
            TemporalUnaryOp::Always,
            make_temporal_embedded(make_binary(
                arena,
                ExprBinaryOp::And,
                make_bool(arena, true),
                make_call(arena, "pkg::ready"))))));
    program.declarations.push_back(std::move(wf));

    auto pm = create_default_pipeline();
    const auto result = pm->run(program);
    CHECK(result.any_modified);

    auto *workflow = workflow_in(program);
    REQUIRE(workflow != nullptr);
    REQUIRE(workflow->safety.size() == 1);
    // Both passes applied: the embedded conjunct folded, then the double
    // `always` collapsed.
    CHECK(render_temporal(*workflow->safety.at(0)) == "U0(embedded(expr))");
}
