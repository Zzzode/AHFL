#include "verification/formal/smt_emit.hpp"

#include "ahfl/compiler/ir/identity.hpp"

#include <string>
#include <variant>
#include <vector>

namespace ahfl::formal {

namespace {

// Accumulates the unioned symbol table (first-encounter order across all
// clauses) and the rendered clause bodies before they are written, so symbol
// declarations can precede every assertion in the document.
struct Document {
    std::vector<SmtSymbol> symbols;
    std::vector<std::string> body;

    void merge_symbols(const std::vector<SmtSymbol> &clause_symbols) {
        for (const auto &symbol : clause_symbols) {
            bool seen = false;
            for (const auto &existing : symbols) {
                if (existing.name == symbol.name) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                symbols.push_back(symbol);
            }
        }
    }
};

// The SMT-LIB assertion for one encodable clause. requires/invariant assert the
// predicate directly (an assumption / always-true goal); ensures asserts it as
// a postcondition goal; forbid asserts its negation. The distinction is a
// comment here — the solver seam (slice 4/5) turns these into real proof
// queries; the emit artifact just records the sort-correct encoding.
[[nodiscard]] std::string clause_assertion(ir::ContractClauseKind kind, const std::string &term) {
    if (kind == ir::ContractClauseKind::Forbid) {
        return "(assert (not " + term + "))";
    }
    return "(assert " + term + ")";
}

} // namespace

void emit_program_smt(const ir::Program &program, std::ostream &out,
                      const SmtEncodeOptions &options) {
    Document doc;

    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }
        const auto target = std::string(ir::symbol_canonical_name(contract->target_ref));
        const auto &clauses = contract->clauses;
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            const auto &clause = clauses[i];
            const auto *expr = std::get_if<ir::ExprRef>(&clause.value);
            if (expr == nullptr || clause.kind == ir::ContractClauseKind::Decreases) {
                continue;
            }

            const auto kind_name = std::string(ir::contract_clause_kind_name(clause.kind));
            const auto label =
                "contract " + target + " " + kind_name + "[" + std::to_string(i) + "]";

            const auto encoded = encode_predicate(*expr, options);
            if (!encoded.ok()) {
                doc.body.push_back("; " + label + ": not in verifiable subset (" +
                                   std::string(describe_rejection(encoded.rejection.value_or(
                                       SmtEncodeRejection::UnsupportedNode))) +
                                   ")");
                continue;
            }

            doc.merge_symbols(encoded.symbols);
            doc.body.push_back("; " + label);
            doc.body.push_back(clause_assertion(clause.kind, *encoded.term));
            for (const auto &obligation : encoded.obligations) {
                doc.body.push_back("; " + label + " obligation");
                doc.body.push_back("(assert " + obligation.predicate + ")");
            }
        }
    }

    out << "; AHFL contract SMT-LIB 2 encoding (RFC 0017)\n";
    // QF_NIA: quantifier-free nonlinear integer arithmetic. Covers linear
    // predicates and the nonlinear ones (e.g. `qty * price`) that contract
    // postconditions produce; a pure-linear subset still solves under it.
    out << "(set-logic QF_NIA)\n";

    for (const auto &symbol : doc.symbols) {
        out << "(declare-const " << symbol.name << " " << smt_sort_keyword(symbol.sort) << ")\n";
        if (symbol.int_bounds.has_value()) {
            out << "(assert (and (>= " << symbol.name << " " << symbol.int_bounds->first
                << ") (<= " << symbol.name << " " << symbol.int_bounds->second << ")))\n";
        }
    }

    for (const auto &line : doc.body) {
        out << line << '\n';
    }

    out << "(check-sat)\n";
}

} // namespace ahfl::formal
