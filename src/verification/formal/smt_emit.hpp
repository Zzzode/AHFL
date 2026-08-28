#pragma once

// RFC 0017 (BMC Contract Semantics) slice 3: `ahflc emit smt` artifact.
//
// Renders a program's contract data predicates into a single SMT-LIB 2
// document: sort-correct symbol declarations, the per-clause assertions
// (requires/ensures/invariant/forbid), the divide/modulo divisor obligations,
// and a trailing (check-sat). No solver is invoked — this is the artifact the
// solver seam (slice 4) will consume. Clauses outside the verifiable subset
// are emitted as structured comments rather than dropped.
//
// Determinism: symbols are declared in first-encounter order and no wall
// clock / pid / host path enters the output, so the same program always emits
// byte-identical text.

#include <ostream>

#include "ahfl/compiler/ir/ir.hpp"
#include "verification/formal/smt_encode.hpp"

namespace ahfl::formal {

// Emit the SMT-LIB 2 document for `program`'s contracts to `out`.
//
// RFC 0026 (KR6.3): the SMT-BMC verification backend consumes the AHFL-IR
// (verification / orchestration) layer of the IR tower. `ir::AhflIr` is that
// layer; today it aliases `ir::Program`, so this is a zero-behavior-change
// boundary annotation.
void emit_program_smt(const ir::AhflIr &program, std::ostream &out,
                      const SmtEncodeOptions &options = {});

} // namespace ahfl::formal
