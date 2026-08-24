// RFC 0017 (BMC Contract Semantics) slice 1 unit tests: verifiable-subset
// eligibility walk over contract data predicates.
//
// Builds programs with contract clauses that are inside the SMT verifiable
// subset (bool/arithmetic predicates) and outside it (string content) and
// asserts analyze_contract_subset_eligibility classifies each correctly,
// carrying clause kind / target / rejection reason for diagnostics.

#include "verification/formal/subset.hpp"

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <cstdlib>
#include <iostream>
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

ir::ExprRef int_lit(ir::Program &p, const std::string &spelling) {
    return p.expr_arena.make(ir::IntegerLiteralExpr{.spelling = spelling});
}

ir::ExprRef str_lit(ir::Program &p, const std::string &spelling) {
    return p.expr_arena.make(ir::StringLiteralExpr{.spelling = spelling});
}

ir::ExprRef bin(ir::Program &p, ir::ExprBinaryOp op, ir::ExprRef lhs, ir::ExprRef rhs) {
    return p.expr_arena.make(ir::BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

// Appends a contract targeting `target` with the given data-predicate clauses.
void add_contract(ir::Program &p,
                  const std::string &target,
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
            .source_range = ahfl::SourceRange{10, 20},
        });
    }
    p.declarations.push_back(std::move(contract));
}

void test_all_eligible() {
    ir::Program p;
    // requires: 1 == 1 ; ensures: 2 > 1
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))},
                  {ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::Greater, int_lit(p, "2"), int_lit(p, "1"))}});

    auto report = analyze_contract_subset_eligibility(p);
    check(report.data_predicate_clauses == 2, "counts both data-predicate clauses");
    check(report.eligible_clauses == 2, "both clauses eligible");
    check(report.all_eligible(), "report reports all-eligible");
    check(report.ineligible.empty(), "no ineligible clauses");
}

void test_string_predicate_ineligible() {
    ir::Program p;
    // ensures: "a" == "b"  -> string content leaves the subset
    add_contract(p, "pkg::StrAgent",
                 {{ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::Equal, str_lit(p, "\"a\""), str_lit(p, "\"b\""))}});

    auto report = analyze_contract_subset_eligibility(p);
    check(report.data_predicate_clauses == 1, "one data-predicate clause examined");
    check(report.eligible_clauses == 0, "no eligible clauses");
    check(!report.all_eligible() && report.ineligible.size() == 1, "one ineligible clause");
    if (report.ineligible.size() == 1) {
        const auto &clause = report.ineligible.front();
        check(clause.kind == ir::ContractClauseKind::Ensures, "ineligible clause kind is ensures");
        check(clause.target_name == "pkg::StrAgent", "ineligible clause carries target name");
        check(clause.clause_index == 0, "ineligible clause index recorded");
        check(clause.rejection == SmtEncodeRejection::StringContent, "rejection reason is string");
        check(clause.source_range.has_value(), "ineligible clause carries source range");
    }
}

void test_mixed_and_decreases_skipped() {
    ir::Program p;
    // requires: 1 == 1 (eligible); forbid: "x" == "y" (ineligible);
    // decreases term is skipped entirely (not a data predicate).
    add_contract(p, "Mixed",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))},
                  {ir::ContractClauseKind::Forbid,
                   bin(p, ir::ExprBinaryOp::Equal, str_lit(p, "\"x\""), str_lit(p, "\"y\""))},
                  {ir::ContractClauseKind::Decreases, int_lit(p, "5")}});

    auto report = analyze_contract_subset_eligibility(p);
    check(report.data_predicate_clauses == 2, "decreases clause not counted as data predicate");
    check(report.eligible_clauses == 1, "one eligible clause");
    check(report.ineligible.size() == 1 &&
              report.ineligible.front().kind == ir::ContractClauseKind::Forbid,
          "forbid clause reported ineligible");
}

void test_empty_program() {
    ir::Program p;
    auto report = analyze_contract_subset_eligibility(p);
    check(report.data_predicate_clauses == 0 && report.all_eligible(),
          "empty program yields empty, all-eligible report");
}

} // namespace

int main() {
    test_all_eligible();
    test_string_predicate_ineligible();
    test_mixed_and_decreases_skipped();
    test_empty_program();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
