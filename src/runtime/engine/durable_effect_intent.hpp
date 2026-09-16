#pragma once

// RFC 0026 KR6.5 E4-B2-D2b-2: the typed durable-effect intent record, its
// IntentCoordinate, and the frozen-authority checkpoint-namespace builder.
//
// The identity contract is LOCKED in
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md section 3 (lines
// 497-511):
//
//  * the dedup/intent backend's identity is the opaque 16-byte
//    `IdempotencyAuthorityId` -- it is NEVER derived from key_id/path/hostname
//    and is NEVER switched within a checkpoint lifetime (a namespace swap on
//    recovery would mint a fresh token and bypass dedup);
//  * within one authority scope, `(CoreWorkflowId, ResumeCheckpointId)` is the
//    stable checkpoint namespace;
//  * the canonical token is the slice-1 `IdempotencyToken`
//    (SHA-256 over the fixed 103-byte preimage), so this file computes no
//    digest of its own.
//
// This slice turns those rules into a typed value and a typed constructor:
//
//  * `CheckpointNamespace` / `IntentCoordinate` are strong-id aggregates
//    (Principle 2) -- the coordinate deliberately carries no authority (it is
//    bound once on the builder) and no generation/attempt (replay-changing
//    values cannot enter the token);
//  * `FrozenAuthorityNamespaceBuilder` binds authority + namespace exactly
//    once: a rebind is a typed `IntentBindError::AlreadyBound`, a mint before
//    bind is `IntentMintError::NotBound`, and a coordinate from another
//    checkpoint namespace is `IntentMintError::NamespaceMismatch`;
//  * `DurableEffectIntent` is an immutable record (const accessors only; the
//    builder is its sole friend/minter) whose identity is authority +
//    coordinate; the optional display name is a diagnostic-only string and is
//    excluded from BOTH the token and equality.
//
// This is a NEW Core-Wasm identity type. The native void intent sink is
// explicitly unchanged (RFC 0026 line 777). There is still no VM, no store,
// no persistence, and no read/recover/dedup/result gate (that is D2b-3); the
// authority id is deliberately NOT persisted into any A1 record here -- that
// grammar change is a separately blocked gate and the authority is bound once
// by the future host/controller (seam lines 500-503).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_idempotency_token.hpp" // IdempotencyAuthorityId / IdempotencyToken
#include "runtime/engine/core_wasm_resume_record.hpp"     // core_wasm_resume::InvocationOrdinal
#include "runtime/engine/payload_store_codec.hpp"         // payload_store::ResumeCheckpointId

namespace ahfl::runtime::durable_effect_intent {

using core_wasm_idempotency_token::IdempotencyAuthorityId;
using core_wasm_idempotency_token::IdempotencyToken;

/// The stable checkpoint namespace within one authority scope
/// `(CoreWorkflowId, ResumeCheckpointId)` (seam lines 504-505). Every intent a
/// bound builder mints MUST name this exact namespace; identity is the two
/// strong ids, never any workflow/checkpoint name string.
struct CheckpointNamespace {
    ir::core::CoreWorkflowId workflow{};
    payload_store::ResumeCheckpointId checkpoint{};

    [[nodiscard]] friend bool operator==(const CheckpointNamespace &,
                                         const CheckpointNamespace &) noexcept = default;
};

/// The full call coordinate of one durable effect inside a checkpoint
/// namespace: the workflow node, its per-node invocation ordinal, the
/// capability being invoked, the source symbol, and the SHA-256 of the
/// canonical typed Param bytes.
///
/// There is deliberately NO authority member (bound once on the builder), NO
/// generation and NO attempt member: replay-changing values cannot enter the
/// token. A simulated retry that changes only an external attempt number
/// re-mints the same `IntentCoordinate` and gets an identical token/record.
struct IntentCoordinate {
    CheckpointNamespace checkpoint_namespace{};
    ir::core::CoreWorkflowNodeId node{};
    core_wasm_resume::InvocationOrdinal ordinal{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    support::Sha256Digest param_digest{};

    [[nodiscard]] friend bool operator==(const IntentCoordinate &,
                                         const IntentCoordinate &) noexcept = default;
};

// ---- lifecycle (Principle 4: std::variant, visited with Overloaded) --------

/// The effect has been registered with the authority but no terminal result is
/// known yet. This is the only lifecycle a freshly minted intent has.
struct IntentPending {};

/// The effect completed and its result handle was recorded (D2b-3).
struct IntentSucceeded {};

/// The effect reached a typed terminal failure (the failure payload is added
/// by the D2b-3 result authority; this slice pins the arm shape only).
struct IntentFailed {};

/// Closed lifecycle state set. Consumers visit it with
/// `std::visit(Overloaded{...}, intent.lifecycle())`; a newly added arm makes
/// an exhaustive visitor fail to compile.
using IntentLifecycle = std::variant<IntentPending, IntentSucceeded, IntentFailed>;

// ---- typed builder/record errors (no bool/string error channel) ------------

enum class IntentBindError : std::uint8_t {
    InvalidWorkflow,   // CoreWorkflowId{kInvalid} cannot name a namespace
    InvalidCheckpoint, // ResumeCheckpointId{kInvalid} cannot name a namespace
    AlreadyBound,      // authority/namespace was already bound in this lifetime
};

enum class IntentMintError : std::uint8_t {
    NotBound,          // mint before bind(): no frozen authority/namespace
    NamespaceMismatch, // coordinate names a different (workflow, checkpoint)
    InvalidNode,       // CoreWorkflowNodeId{kInvalid}
    InvalidCapability, // CoreCapabilityId{kInvalid} (0 stays legal)
};

// ---- the immutable record ---------------------------------------------------

/// One durable-effect intent. Constructible only through
/// `FrozenAuthorityNamespaceBuilder::mint`; every accessor is const, so a
/// built record cannot be mutated in place. Identity (operator==) is bound
/// authority + full coordinate; the token is their deterministic slice-1
/// digest. Lifecycle state and the optional display name do NOT participate in
/// identity.
class DurableEffectIntent {
  public:
    // Value semantics: copyable and movable, but every data member is private
    // and there is no mutating accessor, so a built record's identity can only
    // be replaced wholesale by another minted record -- never edited in place.
    DurableEffectIntent(const DurableEffectIntent &) = default;
    DurableEffectIntent(DurableEffectIntent &&) noexcept = default;
    DurableEffectIntent &operator=(const DurableEffectIntent &) = default;
    DurableEffectIntent &operator=(DurableEffectIntent &&) noexcept = default;
    ~DurableEffectIntent() = default;

    [[nodiscard]] IdempotencyAuthorityId authority() const noexcept {
        return authority_;
    }
    [[nodiscard]] const CheckpointNamespace &checkpoint_namespace() const noexcept {
        return coordinate_.checkpoint_namespace;
    }
    [[nodiscard]] const IntentCoordinate &coordinate() const noexcept {
        return coordinate_;
    }
    [[nodiscard]] IdempotencyToken token() const noexcept {
        return token_;
    }
    [[nodiscard]] const IntentLifecycle &lifecycle() const noexcept {
        return lifecycle_;
    }

    /// Diagnostic-only display label. Never part of the token or identity.
    [[nodiscard]] std::optional<std::string_view> display_name() const noexcept {
        if (!display_name_.has_value()) {
            return std::nullopt;
        }
        return std::string_view{*display_name_};
    }

    /// Identity equality: bound authority + full coordinate (the token is their
    /// deterministic digest, so comparing it is implied). Lifecycle state and
    /// display name are excluded by design.
    friend bool operator==(const DurableEffectIntent &lhs, const DurableEffectIntent &rhs) noexcept;

    friend class FrozenAuthorityNamespaceBuilder;

  private:
    DurableEffectIntent(IdempotencyAuthorityId authority,
                        IntentCoordinate coordinate,
                        IdempotencyToken token,
                        IntentLifecycle lifecycle,
                        std::optional<std::string> display_name) noexcept;

    IdempotencyAuthorityId authority_;
    IntentCoordinate coordinate_;
    IdempotencyToken token_;
    IntentLifecycle lifecycle_;
    std::optional<std::string> display_name_;
};

// ---- the frozen-authority namespace builder --------------------------------

/// Binds exactly one `(IdempotencyAuthorityId, CheckpointNamespace)` for its
/// whole lifetime and mints immutable `DurableEffectIntent`s only for
/// coordinates inside that namespace.
///
/// Default construction yields an UNBOUND builder. `bind` completes the
/// binding exactly once: a second call is rejected with
/// `IntentBindError::AlreadyBound` rather than silently switching authority
/// (the freeze the seam mandates), and `mint` on an unbound builder fails with
/// `IntentMintError::NotBound`.
class FrozenAuthorityNamespaceBuilder {
  public:
    FrozenAuthorityNamespaceBuilder() = default;

    FrozenAuthorityNamespaceBuilder(const FrozenAuthorityNamespaceBuilder &) = delete;
    FrozenAuthorityNamespaceBuilder &operator=(const FrozenAuthorityNamespaceBuilder &) = delete;
    FrozenAuthorityNamespaceBuilder(FrozenAuthorityNamespaceBuilder &&) = default;
    FrozenAuthorityNamespaceBuilder &operator=(FrozenAuthorityNamespaceBuilder &&) = default;
    ~FrozenAuthorityNamespaceBuilder() = default;

    /// Bind the immutable authority identity and the stable checkpoint
    /// namespace. Fails typed on an invalid namespace id or on a second bind
    /// attempt; on failure the builder stays unbound/unchanged.
    [[nodiscard]] std::expected<void, IntentBindError>
    bind(IdempotencyAuthorityId authority, CheckpointNamespace checkpoint_namespace) noexcept;

    [[nodiscard]] bool bound() const noexcept {
        return bound_;
    }

    /// Mint one intent for `coordinate` under the frozen binding. The
    /// coordinate MUST name the bound checkpoint namespace
    /// (`IntentMintError::NamespaceMismatch` otherwise); node/capability must
    /// not hold their kInvalid sentinels. `display_name` is diagnostic-only and
    /// excluded from the token and identity. No generation/attempt argument
    /// exists: retries re-mint with the same coordinate.
    [[nodiscard]] std::expected<DurableEffectIntent, IntentMintError>
    mint(IntentCoordinate coordinate, std::optional<std::string> display_name = std::nullopt) const;

  private:
    IdempotencyAuthorityId authority_{};
    CheckpointNamespace namespace_{};
    bool bound_{false};
};

} // namespace ahfl::runtime::durable_effect_intent
