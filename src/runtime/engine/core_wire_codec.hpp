#pragma once

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/json/json_value.hpp"
#include "runtime/evaluator/value.hpp"

#include <optional>
#include <string>
#include <utility>

// RFC 0026 KR6.5 E4-B0-C2b: the shared, schema-guided wire codec. It runs the
// SAME shape traversal under two policies over a `VerifiedWireSchemaBinding`
// (C2a): the binding is the only schema input, so a hand-built or tampered
// `CoreWireSchemaTable` can never reach this engine.
//
//   * decode_json:   a JSON DOM value  -> a native runtime Value, constructing
//                    the EXACT variant the schema names, or a diagnostic. A JSON
//                    string under a Decimal/Duration node becomes the matching
//                    variant (never a bare StringValue); under a String node it
//                    stays a String. UUID and Timestamp are NOT bare strings: the
//                    only canonical wire forms are the exact objects
//                    `{"_uuid":"<32 lowercase hex>"}` and `{"_timestamp":<int64>}`,
//                    matching the value_json encoder; a bare string under a
//                    Uuid/Timestamp node is rejected.
//   * validate_value: a native runtime Value is checked structurally against the
//                    schema without being rebuilt.
//
// Trust-boundary invariants (Codex C2b policy rev2, locked):
//   * Decimal/Duration decode PRESERVES the input spelling verbatim; the parsed
//     mantissa/scale/ms are used only to check validity, scale, and overflow —
//     never to normalize or rewrite the bytes.
//   * Float requires a JSON Float / a finite native FloatValue — no implicit
//     Int->Float or Int/Float->Decimal widening.
//   * Set/Map: on JSON decode, any input element/key order is accepted and a
//     canonical collection is constructed, but structurally-equal duplicate Set
//     elements and duplicate Map keys are rejected (never silently merged). On
//     native validate, the incoming Value must ALREADY be canonical (no null;
//     Map keys exact String; strict dedup + order), checked WITHOUT mutating the
//     input by canonicalizing a clone and comparing structural equality.
//   * Int/String honor the schema's bounds / length_bounds.
//
// This codec does NOT change value_json / the wire encoder, nor
// workflow_recovery's save path or its persisted bytes; those shapes are
// unchanged in this checkpoint. (The P0-10 direct-DOM load admission is a
// separate, already-landed shared gate, not part of this codec.)

namespace ahfl::runtime {

// The shared {valid, error} validation result. Owned here so this codec is the
// single authority for schema-guided native-Value validation; the legacy
// response_schema_validator reuses THIS type (it will be demoted to a thin shim
// in a later C2b stage) rather than defining a parallel one.
struct SchemaValidationResult {
    bool valid{true};
    std::string error;

    [[nodiscard]] static SchemaValidationResult ok() { return {true, {}}; }
    [[nodiscard]] static SchemaValidationResult fail(std::string msg) {
        return {false, std::move(msg)};
    }
};

namespace wire_codec {

// Result of decoding a JSON DOM value against a wire-schema binding. Distinct
// from SchemaValidationResult because it carries the decoded Value, not just a
// pass/fail verdict.
struct WireDecodeResult {
    std::optional<evaluator::Value> value;
    std::string error; // empty iff `value` is set

    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }

    [[nodiscard]] static WireDecodeResult success(evaluator::Value v) {
        return {std::move(v), {}};
    }
    [[nodiscard]] static WireDecodeResult failure(std::string msg) {
        return {std::nullopt, std::move(msg)};
    }
};

// Policy 1: schema-guided decode of a raw JSON DOM value into a native Value.
[[nodiscard]] WireDecodeResult
decode_json(const json::JsonValue &json, const ir::core::VerifiedWireSchemaBinding &binding);

// Policy 2: schema-guided structural validation of an existing native Value.
[[nodiscard]] SchemaValidationResult
validate_value(const evaluator::Value &value,
               const ir::core::VerifiedWireSchemaBinding &binding);

} // namespace wire_codec

} // namespace ahfl::runtime
