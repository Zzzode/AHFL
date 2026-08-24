#pragma once

// RFC 0017 (BMC Contract Semantics) slice 2: SMT encoding layer.
//
// Encodes the *data-predicate* subset of AHFL contract expressions
// (ir::Expr trees behind requires/ensures/invariant/forbid clauses) into
// SMT-LIB 2 terms. This is a pure, side-effect-free translation: no solver
// is invoked here and no wall clock / pid / host path / randomness enters the
// output, so the same expression always encodes to byte-identical text
// (artifact determinism, per the RFC).
//
// Expressions outside the verifiable subset (String content predicates,
// List/Map quantification, capability-call results, non-pure expressions)
// are reported as a structured rejection rather than silently dropped, so the
// caller can emit formal.NOT_IN_VERIFIED_SUBSET.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::formal {

// Why an expression could not be encoded into the verifiable subset. Maps to
// the user-facing formal.NOT_IN_VERIFIED_SUBSET diagnostic at the call site.
enum class SmtEncodeRejection {
    UnsupportedNode,      // an ir::Expr node kind with no SMT encoding rule
    UnsupportedOperator,  // an operator not in the encodable set
    StringContent,        // a String-valued operand used beyond equality
    UnsupportedType,      // an operand type with no SMT sort mapping
    NullExpr,             // a null ExprRef (malformed contract)
};

// A divide/modulo node emits a `divisor != 0` verification obligation: the
// SMT-LIB predicate that must hold for the expression to be free of a
// runtime divide-by-zero (aligns with the evaluator, which raises an error on
// zero divisor). The obligation is carried alongside the encoded term so the
// BMC engine can assert it as a proof goal.
struct SmtObligation {
    std::string predicate; // SMT-LIB 2 boolean term, e.g. "(not (= d 0))"
};

// SMT-LIB 2 sort a contract symbol maps to. Reals/strings are out of the
// current data-predicate subset, so only the two scalar sorts appear.
enum class SmtSort {
    Bool,
    Int,
};

// A free symbol referenced by an encoded predicate, with the sort it must be
// declared at. `int_bounds` is populated for Int(lo,hi) operands so the BMC
// engine / emit artifact can assert the range constraint. Symbols are
// collected in first-encounter order for deterministic output.
struct SmtSymbol {
    std::string name;
    SmtSort sort{SmtSort::Int};
    std::optional<std::pair<std::int64_t, std::int64_t>> int_bounds;
};

// Result of encoding one contract expression. On success `term` holds the
// SMT-LIB 2 term, `symbols` the free symbols it references (deduplicated, in
// first-encounter order), and `obligations` any divide/modulo divisor-non-zero
// goals. On failure `rejection` explains why the expression left the subset.
struct SmtEncodeResult {
    std::optional<std::string> term;
    std::vector<SmtSymbol> symbols;
    std::vector<SmtObligation> obligations;
    std::optional<SmtEncodeRejection> rejection;

    [[nodiscard]] bool ok() const noexcept {
        return term.has_value() && !rejection.has_value();
    }
};

// Options controlling encoding policy (RFC 0017 resolved Open Questions).
struct SmtEncodeOptions {
    // When true, each integer arithmetic node also contributes an overflow
    // guard obligation (INT64 range). Default off: contract verification
    // targets logical correctness; overflow is an opt-in dimension.
    bool emit_overflow_checks{false};
};

// Encode a single contract data-predicate expression to an SMT-LIB 2 term.
// `expr` is the root of the predicate (e.g. the ensures condition). Pure:
// depends only on the expression tree and options.
[[nodiscard]] SmtEncodeResult encode_predicate(const ir::ExprRef &expr,
                                               const SmtEncodeOptions &options = {});

// Human-readable reason for a rejection, for diagnostic messages.
[[nodiscard]] std::string_view describe_rejection(SmtEncodeRejection rejection) noexcept;

// SMT-LIB 2 sort keyword for a symbol sort ("Bool" / "Int"). Shared by the
// emit artifact and the SMT-BMC engine so the sort spelling lives in one place.
[[nodiscard]] std::string_view smt_sort_keyword(SmtSort sort) noexcept;

} // namespace ahfl::formal
