// RFC 0017 (BMC Contract Semantics) slice 3 unit tests: `emit smt` document
// builder. Checks sort-correct declarations, per-clause assertions, the forbid
// negation, out-of-subset comments, and byte-identical determinism.

#include "verification/formal/smt_emit.hpp"

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl::formal;
namespace ir = ahfl::ir;

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

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

ir::TypeRef int_type() {
    ir::TypeRef t;
    t.kind = ir::TypeRefKind::Int;
    t.display_name = "Int";
    return t;
}

ir::ExprRef int_lit(ir::Program &p, const std::string &spelling) {
    return p.expr_arena.make(ir::IntegerLiteralExpr{.spelling = spelling});
}

ir::ExprRef str_lit(ir::Program &p, const std::string &spelling) {
    return p.expr_arena.make(ir::StringLiteralExpr{.spelling = spelling});
}

ir::ExprRef int_path(ir::Program &p, const std::string &root, std::vector<std::string> members) {
    ir::Path path;
    path.root_kind = ir::PathRootKind::Identifier;
    path.root_name = root;
    path.members = std::move(members);
    return p.expr_arena.make(ir::PathExpr{.path = std::move(path)}, std::nullopt, int_type());
}

ir::ExprRef bin(ir::Program &p, ir::ExprBinaryOp op, ir::ExprRef lhs, ir::ExprRef rhs) {
    return p.expr_arena.make(ir::BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

void add_contract(ir::Program &p, const std::string &target,
                  std::vector<std::pair<ir::ContractClauseKind, ir::ExprRef>> clauses) {
    ir::ContractDecl contract;
    contract.target_ref = ir::SymbolRef{
        .kind = ir::SymbolRefKind::Agent,
        .canonical_name = target,
        .local_name = target,
        .module_name = {},
    };
    for (auto &[kind, expr] : clauses) {
        contract.clauses.push_back(ir::ContractClause{
            .kind = kind,
            .value = std::variant<ir::ExprRef, ir::TemporalExprPtr>{expr},
            .source_range = ahfl::SourceRange{1, 2},
        });
    }
    p.declarations.push_back(std::move(contract));
}

std::string emit(const ir::Program &p) {
    std::ostringstream out;
    emit_program_smt(p, out);
    return out.str();
}

void test_header_and_check_sat() {
    ir::Program p;
    auto doc = emit(p);
    check(contains(doc, "(set-logic QF_NIA)"), "emits QF_NIA logic");
    check(contains(doc, "(check-sat)"), "emits check-sat");
}

void test_declarations_and_assertions() {
    ir::Program p;
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"qty"}),
                       int_lit(p, "0"))}});
    auto doc = emit(p);
    check(contains(doc, "(declare-const input__qty Int)"), "declares Int symbol");
    check(contains(doc, "(assert (> input__qty 0))"), "asserts the requires predicate");
    check(contains(doc, "contract Agent requires[0]"), "labels the clause");
}

void test_forbid_negation() {
    ir::Program p;
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Forbid,
                   bin(p, ir::ExprBinaryOp::Less, int_path(p, "output", {"n"}), int_lit(p, "0"))}});
    auto doc = emit(p);
    check(contains(doc, "(assert (not (< output__n 0)))"), "forbid asserts the negation");
}

void test_out_of_subset_comment() {
    ir::Program p;
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::Equal, str_lit(p, "\"a\""), str_lit(p, "\"b\""))}});
    auto doc = emit(p);
    check(contains(doc, "not in verifiable subset"),
          "out-of-subset clause becomes a comment, not an assertion");
    check(!contains(doc, "(assert (= "), "no assertion emitted for the abstracted clause");
}

void test_determinism() {
    auto build = [](ir::Program &p) {
        add_contract(p, "Agent",
                     {{ir::ContractClauseKind::Requires,
                       bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"a"}),
                           int_lit(p, "0"))},
                      {ir::ContractClauseKind::Ensures,
                       bin(p, ir::ExprBinaryOp::Equal, int_path(p, "output", {"b"}),
                           int_path(p, "input", {"a"}))}});
    };
    ir::Program p1;
    build(p1);
    ir::Program p2;
    build(p2);
    check(emit(p1) == emit(p2), "same program emits byte-identical documents");
}

} // namespace

int main() {
    test_header_and_check_sat();
    test_declarations_and_assertions();
    test_forbid_negation();
    test_out_of_subset_comment();
    test_determinism();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
