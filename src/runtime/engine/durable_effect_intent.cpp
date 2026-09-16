// RFC 0026 KR6.5 E4-B2-D2b-2: typed DurableEffectIntent record +
// IntentCoordinate + frozen-authority namespace builder.
// See durable_effect_intent.hpp for the locked seam contract.

#include "runtime/engine/durable_effect_intent.hpp"

#include <utility>

namespace ahfl::runtime::durable_effect_intent {

DurableEffectIntent::DurableEffectIntent(IdempotencyAuthorityId authority,
                                         IntentCoordinate coordinate,
                                         IdempotencyToken token,
                                         IntentLifecycle lifecycle,
                                         std::optional<std::string> display_name) noexcept
    : authority_(authority), coordinate_(std::move(coordinate)), token_(token),
      lifecycle_(std::move(lifecycle)), display_name_(std::move(display_name)) {}

bool operator==(const DurableEffectIntent &lhs, const DurableEffectIntent &rhs) noexcept {
    // Identity is the bound authority plus the full call coordinate. The token
    // is their deterministic slice-1 digest and need not be compared; lifecycle
    // state and the display name are deliberately excluded.
    return lhs.authority_ == rhs.authority_ && lhs.coordinate_ == rhs.coordinate_;
}

std::expected<void, IntentBindError>
FrozenAuthorityNamespaceBuilder::bind(IdempotencyAuthorityId authority,
                                      CheckpointNamespace checkpoint_namespace) noexcept {
    if (bound_) {
        // The authority identity is immutable for the builder's whole
        // checkpoint lifetime: never silently switch it (seam lines 500-503).
        return std::unexpected(IntentBindError::AlreadyBound);
    }
    if (checkpoint_namespace.workflow.value == ir::core::CoreWorkflowId::kInvalid) {
        return std::unexpected(IntentBindError::InvalidWorkflow);
    }
    if (checkpoint_namespace.checkpoint.value == payload_store::ResumeCheckpointId::kInvalid) {
        return std::unexpected(IntentBindError::InvalidCheckpoint);
    }
    authority_ = authority;
    namespace_ = checkpoint_namespace;
    bound_ = true;
    return {};
}

std::expected<DurableEffectIntent, IntentMintError>
FrozenAuthorityNamespaceBuilder::mint(IntentCoordinate coordinate,
                                      std::optional<std::string> display_name) const {
    if (!bound_) {
        return std::unexpected(IntentMintError::NotBound);
    }
    if (coordinate.checkpoint_namespace != namespace_) {
        // A namespace swap must never silently mint under the bound authority:
        // it would produce a cross-namespace token and bypass dedup.
        return std::unexpected(IntentMintError::NamespaceMismatch);
    }
    if (coordinate.node.value == ir::core::CoreWorkflowNodeId::kInvalid) {
        return std::unexpected(IntentMintError::InvalidNode);
    }
    if (coordinate.capability.value == ir::core::CoreCapabilityId::kInvalid) {
        return std::unexpected(IntentMintError::InvalidCapability);
    }

    // Project the frozen binding + call coordinate onto the slice-1 token
    // coordinate. The builder is the single place the authority enters the
    // preimage; call coordinates cannot supply one.
    core_wasm_idempotency_token::IdempotencyCoordinate preimage{};
    preimage.authority = authority_;
    preimage.workflow = namespace_.workflow;
    preimage.checkpoint = namespace_.checkpoint;
    preimage.node = coordinate.node;
    preimage.ordinal = coordinate.ordinal;
    preimage.capability = coordinate.capability;
    preimage.source_symbol = coordinate.source_symbol;
    preimage.param_digest = coordinate.param_digest;

    const IdempotencyToken token = core_wasm_idempotency_token::compute_idempotency_token(preimage);

    // Every freshly minted intent starts Pending; the record is then immutable
    // (result recording is the D2b-3 authority's external state).
    return DurableEffectIntent{authority_,
                               std::move(coordinate),
                               token,
                               IntentLifecycle{IntentPending{}},
                               std::move(display_name)};
}

} // namespace ahfl::runtime::durable_effect_intent
