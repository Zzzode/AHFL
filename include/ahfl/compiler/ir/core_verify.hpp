#pragma once

// ---------------------------------------------------------------------------
// Core-IR structural verifier (RFC 0026 P3 / KR6.4)
// ---------------------------------------------------------------------------
//
// The verifier turns the Core-IR structural invariants — until now enforced only
// by convention inside the lowerer — into an automatic, fail-closed gate. It is
// the execution-layer analogue of a bytecode/SIL verifier (JVM verifier, Swift
// SIL verifier, Rust MIR validator): a well-formed `CoreProgram` is one every
// backend may consume without re-deriving these facts.
//
// It is wired in two places (Principle: verify at every boundary):
//   * `lower_ahfl_to_core` runs it automatically on a clean candidate program
//     and merges any verifier Error into the lowering result — a program is
//     executable iff BOTH the lowering AND the verifier are error-free.
//   * `verify_core_program` is exposed standalone so IR-JSON round-trip and
//     future backends can re-check a program at the consumption boundary.
//
// What it checks (fail-closed on every violation):
//   * type-table self-consistency: struct/enum parallel arrays are in sync
//     (field_types == fields == field_has_default; variant_payloads == variants),
//     a struct carries no enum metadata and vice versa, each variant payload's
//     kind agrees with its vectors, and every payload slot type id is in range —
//     a dedicated core.verify.TYPE_TABLE_SHAPE_INVALID, so downstream arity /
//     field-domain checks cannot be bypassed by a malformed table;
//   * typed-ID bounds: every CoreTypeId / CoreAgentId / CoreCapabilityId /
//     CoreStateId / CoreExprId / CoreValueId is in range for its store;
//   * projection self-containment: root_type is a valid struct, the step chain
//     is continuous (step[i].owner == step[i-1].result, step[0].owner == root),
//     each field id is in range and its result_type equals the owner's declared
//     field type, and a primitive/leaf step (kInvalid result) is the last step;
//   * value discipline: SSA single-definition is FLOW-GLOBAL (a value id is
//     defined at most once across all states and branches), and def-before-use
//     is region-local with branch-local scope (a value defined in one branch is
//     not visible to a sibling branch or after the `if`);
//   * the pure-expression arena is ACYCLIC (a self/mutually-referential expr
//     would make recursive codegen diverge); ALL arena exprs are checked, not
//     only statement-reachable ones;
//   * construct legality (TRANSITIONAL contract — see slice ②b): resolved
//     type/variant/field ids in range, no duplicate field ids, every REQUIRED
//     struct field (one with no source-level default) is assigned, and an
//     enum-variant constructor's payload-slot count matches the variant's
//     declared arity. A defaulted field MAY currently be omitted (the lowerer
//     does not yet materialize omitted defaults). Slice ②b will materialize
//     defaults at each construct site and tighten this to "every materialized
//     field/slot exactly once", after which a backend never understands
//     defaults. (Builtin enums registered by fallback carry payload arity from
//     the SSOT, so std Option/Result are not a blind spot.)
//   * flow wiring: flow target agent, handler state, and goto targets in range;
//   * capability-call arity matches the import signature;
//   * an executable program contains NO CoreUnsupportedExpr;
//   * no executable statement follows a terminator (goto/return) in a region;
//   * the agent typed shell: input/output must be a valid Struct; context is a
//     valid Struct (context_is_struct) or the Unit default (kInvalid); any other
//     combination — a required shell left kInvalid, or pointing at a non-struct
//     — is an error.
//
// Diagnostics reuse `CoreLowerDiagnostic` (severity + stable code + range) and
// carry `core.verify.*` codes from the `verify` catalogue below.

#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core {

/// Stable diagnostic codes emitted by the Core-IR verifier. Named constants so
/// the verifier and its tests share one source of truth (Principle 5).
namespace verify {
inline constexpr std::string_view kTypeIdOutOfRange = "core.verify.TYPE_ID_OUT_OF_RANGE";
inline constexpr std::string_view kTypeTableShapeInvalid = "core.verify.TYPE_TABLE_SHAPE_INVALID";
inline constexpr std::string_view kAgentStateInvalid = "core.verify.AGENT_STATE_INVALID";
inline constexpr std::string_view kCapabilityIdOutOfRange = "core.verify.CAPABILITY_ID_OUT_OF_RANGE";
inline constexpr std::string_view kStateIdOutOfRange = "core.verify.STATE_ID_OUT_OF_RANGE";
inline constexpr std::string_view kExprIdOutOfRange = "core.verify.EXPR_ID_OUT_OF_RANGE";
inline constexpr std::string_view kExprCycle = "core.verify.EXPR_CYCLE";
inline constexpr std::string_view kValueIdOutOfRange = "core.verify.VALUE_ID_OUT_OF_RANGE";
inline constexpr std::string_view kValueUseBeforeDef = "core.verify.VALUE_USE_BEFORE_DEF";
inline constexpr std::string_view kValueRedefined = "core.verify.VALUE_REDEFINED";
inline constexpr std::string_view kProjectionRootMismatch = "core.verify.PROJECTION_ROOT_MISMATCH";
inline constexpr std::string_view kProjectionDiscontinuity = "core.verify.PROJECTION_DISCONTINUITY";
inline constexpr std::string_view kProjectionFieldInvalid = "core.verify.PROJECTION_FIELD_INVALID";
inline constexpr std::string_view kProjectionOwnerNotStruct = "core.verify.PROJECTION_OWNER_NOT_STRUCT";
inline constexpr std::string_view kProjectionPrimitiveNotLast = "core.verify.PROJECTION_PRIMITIVE_NOT_LAST";
inline constexpr std::string_view kProjectionUnresolved = "core.verify.PROJECTION_UNRESOLVED";
inline constexpr std::string_view kConstructUnresolved = "core.verify.CONSTRUCT_UNRESOLVED";
inline constexpr std::string_view kConstructTypeInvalid = "core.verify.CONSTRUCT_TYPE_INVALID";
inline constexpr std::string_view kConstructVariantInvalid = "core.verify.CONSTRUCT_VARIANT_INVALID";
inline constexpr std::string_view kConstructFieldInvalid = "core.verify.CONSTRUCT_FIELD_INVALID";
inline constexpr std::string_view kConstructFieldDuplicated = "core.verify.CONSTRUCT_FIELD_DUPLICATED";
inline constexpr std::string_view kConstructFieldMissing = "core.verify.CONSTRUCT_FIELD_MISSING";
inline constexpr std::string_view kConstructPayloadArity = "core.verify.CONSTRUCT_PAYLOAD_ARITY";
inline constexpr std::string_view kQualifiedUnresolved = "core.verify.QUALIFIED_UNRESOLVED";
inline constexpr std::string_view kQualifiedVariantInvalid = "core.verify.QUALIFIED_VARIANT_INVALID";
inline constexpr std::string_view kFlowTargetInvalid = "core.verify.FLOW_TARGET_INVALID";
inline constexpr std::string_view kGotoTargetInvalid = "core.verify.GOTO_TARGET_INVALID";
inline constexpr std::string_view kCapabilityArityMismatch = "core.verify.CAPABILITY_ARITY_MISMATCH";
inline constexpr std::string_view kUnsupportedExpr = "core.verify.UNSUPPORTED_EXPR";
inline constexpr std::string_view kStmtAfterTerminator = "core.verify.STMT_AFTER_TERMINATOR";
inline constexpr std::string_view kTypedShellInvalid = "core.verify.TYPED_SHELL_INVALID";
} // namespace verify

/// Result of verifying a Core-IR program: any structural violations, as
/// `CoreLowerDiagnostic`s (severity + `core.verify.*` code + range). Only ERROR
/// diagnostics gate consumption; the verifier emits Errors (never a silent pass).
struct CoreVerifyResult {
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    /// A program is well-formed iff it has no ERROR diagnostics.
    [[nodiscard]] bool ok() const noexcept { return !has_errors(); }
};

/// Verify a Core-IR program's structural invariants (see the header comment).
/// Pure and fail-closed: never throws, never mutates the program; every
/// violation is a diagnostic. A program with `ok()` is safe for a backend to
/// consume without re-deriving these facts.
[[nodiscard]] CoreVerifyResult verify_core_program(const CoreProgram &program);

} // namespace ahfl::ir::core
