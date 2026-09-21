#pragma once

// ---------------------------------------------------------------------------
// Core-IR layered IR-JSON projection (RFC 0026 P9 / KR6.9)
// ---------------------------------------------------------------------------
//
// The execution layer (`CoreProgram`, `include/ahfl/compiler/ir/core_ir.hpp`)
// gains a machine-readable JSON projection with format identity
// `CoreProgram::format_version` (default `"ahfl.core.v1"`). This is the
// executable-layer counterpart of the single-layer `ir::Program` projection
// (`print_program_ir_json` / `parse_program_ir_json`, `ir_json.cpp`), whose
// format is `"ahfl.ir.v2"`.
//
// The complete contract — envelope, per-table schema, arena canonicalization /
// index-remapping rules, the byte-exact round-trip obligations R1/R2, and the
// admission model — is fixed by the APPROVED design
// `docs/design/core-ir-p9-layered-json.zh.md` (KR6.9-B0). That document is
// normative; this header names its decisions so a reader does not have to
// reconstruct them:
//
//   * ONE bundled envelope per program (§1) — `format_version`, a fixed
//     `layer: "core"` discriminator, then the program-global tables (`types`,
//     `value_types`, `capabilities`, `agents`, `flows`, `workflows`,
//     `instances`) in that fixed order.
//   * The lexical rules mirror the single-layer streaming writer (§2): 2-space
//     indent, `": "` key separator, no trailing comma, one trailing `'\n'`.
//   * The program-global `value_types` arena is a true hash-cons; it is written
//     in arena order (§6.1). The READER rebuilds it by interning through
//     `ValueTypeArena` and asserts the freshly minted id for serialized slot
//     `i` is `i` (§6.2), so a reordered-but-structurally-valid document is
//     rejected rather than silently accepted.
//
// **R1** — byte-exact re-emit: `print(parse(print(p))) == print(p)`.
// **R2** — structural identity: `core_program_equal(p, parse(print(p)))`.
// R2 is not implied by R1 for a jointly-wrong writer/reader pair, so it is a
// separate, required obligation owned here.

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/expr.hpp" // ir::SourceRangeOpt

namespace ahfl::ir::core {

/// Print a Core-IR program as its layered JSON projection (RFC 0026 P9 §1-§5),
/// through the shared 2-space streaming writer base. Deterministic and pure:
/// the envelope carries no wall-clock / pid / host path / environment value
/// (§1 determinism boundary). Emits exactly one trailing `'\n'`.
///
/// The writer is a fail-closed artifact boundary: a REQUIRED-valid id left
/// `kInvalid` (a lowering-ERROR / pre-verify state that a verifier-clean program
/// never carries) is a writer error, reported through `error` when non-null and
/// suppressing the rest of the document rather than emitting the `kInvalid`
/// sentinel. The ONE legal serialized `kInvalid` is the sparse, navigation-only
/// `CoreTypeDecl::field_nominal_types` slot, encoded as JSON `null` (§4).
/// Callers that must not publish a malformed artifact pass an `error` pointer
/// and check it.
void print_core_program_json(const CoreProgram &program, std::ostream &out,
                             std::string *error = nullptr);

/// A typed parse failure (RFC 0026 P9 §7): a stable code, a human-readable
/// actionable message, and the document's source range where it supplies one.
/// `SourceRangeOpt` (not `SourceRange`) is used because an envelope-level
/// failure has no range in the document.
struct CoreJsonDiagnostic {
    /// Stable code, e.g. `"core.json.NONCANONICAL_ARENA"` or
    /// `"core.verify.*"` when the failure is a structural violation the Core
    /// verifier would also report.
    std::string code;
    /// Human-readable, actionable message (Principle 5).
    std::string message;
    SourceRangeOpt range;
};

/// The Core-IR JSON reader's result. `program` is engaged iff the whole document
/// was admitted; there is never a partially populated program (§9). The typed
/// struct — not a bare `optional` — lets a caller distinguish a wrong-`layer`
/// document from a forward value-type reference, which `nullopt` cannot.
struct CoreJsonParseResult {
    std::optional<CoreProgram> program;
    std::vector<CoreJsonDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return program.has_value(); }
};

/// Parse a Core-IR JSON document (as produced by `print_core_program_json`) into
/// a `CoreProgram`. A hard admission boundary (§9): a document is accepted only
/// if it passes the envelope checks, has no unknown `kind` and no missing /
/// extra per-kind field, rebuilds the program-global value-type arena to the
/// identity remap (§6.2), is within the region-nesting depth bound, and passes a
/// final `verify_core_program` (so every accepted parse is verifier-clean).
///
/// The reader never trusts a serialized `CoreValueTypeId`: it re-interns the
/// arena and rewrites every reference through the remap, so a hand-edited or
/// reordered document cannot point at the wrong slot.
[[nodiscard]] CoreJsonParseResult parse_core_program_json(std::string_view json);

/// Canonical program-level structural equality (RFC 0026 P9 §7 R2). `CoreProgram`
/// has no `operator==` of its own; this is the ONE comparison the R2 round-trip
/// obligation uses. It compares EVERY table and body component-wise, in table
/// order, so a dropped or reordered element is a difference. Because
/// `CoreValueTypeId` equality is same-owner-arena equality (see
/// `CoreInstanceDecl::operator==`), both sides must come from the same
/// `value_types` arena numbering — which `parse(print(p))` guarantees (§6.2's
/// identity remap pins the parsed arena to the serialized order).
[[nodiscard]] bool core_program_equal(const CoreProgram &a, const CoreProgram &b) noexcept;

} // namespace ahfl::ir::core
