// RFC 0026 KR6.5 E4-B2-D2b-5: see the matching header for the contract.

#include "runtime/engine/durable_effect_live_mint.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "base/support/sha256.hpp"
#include "runtime/value/value_json.hpp" // try_value_to_json

namespace ahfl::runtime::durable_effect_live_mint {

std::optional<durable_effect_intent::DurableEffectIntent>
mint_live_effect_intent(
    const core_wasm_idempotency_token::IdempotencyAuthorityId &authority,
    const durable_effect_intent::CheckpointNamespace &checkpoint_namespace,
    const core_wasm_schema_module::VerifiedCoreWasmCallSite &call_site,
    const runtime::Value &param) {
    namespace dei = durable_effect_intent;

    const auto canonical = runtime::try_value_to_json(param);
    if (!canonical.has_value()) {
        return std::nullopt;
    }
    const auto param_digest = support::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(canonical->data()), canonical->size()));

    dei::FrozenAuthorityNamespaceBuilder builder;
    if (!builder.bind(authority, checkpoint_namespace).has_value()) {
        // The namespace ids come from the authenticated manifest (validated at
        // open_snapshot) and the authority id is bound once: a bind failure is
        // an internal fault, fail closed.
        return std::nullopt;
    }
    dei::IntentCoordinate coordinate;
    coordinate.checkpoint_namespace = checkpoint_namespace;
    coordinate.node = call_site.workflow_node_id();
    coordinate.ordinal = call_site.invocation_ordinal();
    coordinate.capability = call_site.capability();
    coordinate.source_symbol = call_site.source_symbol();
    coordinate.param_digest = param_digest;
    auto minted = builder.mint(coordinate);
    if (!minted.has_value()) {
        return std::nullopt;
    }
    return std::move(*minted);
}

} // namespace ahfl::runtime::durable_effect_live_mint
