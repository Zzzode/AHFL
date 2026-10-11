#pragma once

// RFC 0026 KR6.5 E4-B2-D2b-5: the SHARED canonicalization authority for the
// live capability effect intent. Both the decision-only resume controller
// (its READ-ONLY preview_begin consultation at the ReadyForLive frontier) and
// the production resume-host driver (its SEALING begin_effect before the live
// call) mint the effect intent through this one helper, so the
// IdempotencyToken the controller consulted under and the token the driver
// seals under can never diverge (CLAUDE.md Principle 1: one canonicalization
// authority, no parallel copies).
//
// The canonical byte form is the codebase's canonical strict wire-JSON SSOT
// (`try_value_to_json`): param_digest = sha256(try_value_to_json(param)). A
// param that is not wire-encodable fails closed as nullopt (every param here
// was decoded from wire JSON under its binding and can never be a closure).

#include <cstdint>
#include <optional>

#include "runtime/engine/core_wasm_idempotency_token.hpp" // IdempotencyAuthorityId
#include "runtime/engine/core_wasm_schema_module.hpp"    // VerifiedCoreWasmCallSite
#include "runtime/engine/durable_effect_intent.hpp"      // DurableEffectIntent, CheckpointNamespace
#include "runtime/value/value.hpp"                       // runtime::Value

namespace ahfl::runtime::durable_effect_live_mint {

/// Mint the DurableEffectIntent for one live capability call: bind the frozen
/// (authority, checkpoint namespace) once, build the IntentCoordinate from the
/// call site's strong-id coordinate + the canonical param digest, and mint.
/// Returns nullopt when the param is not wire-encodable or the bind/mint is
/// rejected (an internal invariant fault: the namespace ids come from the
/// authenticated manifest and the authority id is bound once).
[[nodiscard]] std::optional<durable_effect_intent::DurableEffectIntent>
mint_live_effect_intent(
    const core_wasm_idempotency_token::IdempotencyAuthorityId &authority,
    const durable_effect_intent::CheckpointNamespace &checkpoint_namespace,
    const core_wasm_schema_module::VerifiedCoreWasmCallSite &call_site,
    const runtime::Value &param);

} // namespace ahfl::runtime::durable_effect_live_mint
