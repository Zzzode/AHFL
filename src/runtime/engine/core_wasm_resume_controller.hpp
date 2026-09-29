#pragma once

// RFC 0026 KR6.5 E4-B2-D1b: the host-INDEPENDENT durable-resume REPLAY CONTROLLER.
//
// This is a pure DECISION-ONLY authority. It owns the deterministic transition rules
// of a single fresh-instance replay: which stored memo bytes an import must return,
// when the frontier import needs an injected result, the EXACT bytes/slot set of the
// Suspended->Injected publish, and whether a run2 exit may be tombstoned. It NEVER
// touches a Wasm VM, allocates linear memory, transfers a frame, authenticates an
// artifact, writes the durable store, or performs a CAS. The production VM / host
// adapter (future D2a) drives it: the adapter obtains a `ResumeSnapshot` from the
// B1 store, the A2 `VerifiedCoreWasmSchemaModule`, runs the module, and calls the
// store's `publish_available` / `mark_consumed`; this controller only INSPECTS,
// DECIDES, and VERIFIES the store's returned result. IdempotencyToken, durable
// effect intent/result, and confidential storage stay with future D2b / B2-E.
//
// Two-phase admission (staged order is a hard invariant):
//   * open_gated_resume  -- PHASE 1 gates: complete all three fixed-work artifact
//     digests THEN select by priority, then coordinate / A2-baseline. It NEVER
//     admits slots and NEVER judges transition eligibility.
//   * admit_and_preflight -- PHASE 2: admit the exact slot set on the pinned snapshot
//     fd, decode stored memo per occurrence, judge transition eligibility, then
//     either yield `PendingInjection` (Suspended with no injected input) or run the
//     two-pass TOTAL linear-memory preflight and mint a `PreparedResume`.
//   * supply_injected_result -- continuation for `PendingInjection`: Verified-decode
//     the injected result THEN run the full two-pass TOTAL preflight.
//
// The `PreparedResume` then drives an in-place state machine (no moved-from handoff):
// per-import decisions, a two-step publish handshake for the frontier injection, and
// a terminal run2-exit gate. Every controller fault (wrong phase, invalid slot,
// duplicate / late / mismatched ACK) is `ResumeStepReason::TransitionInvalid` and
// moves the controller to a terminal Failed state; a store error is carried back
// verbatim as the real `PayloadStoreError` arm.

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "runtime/engine/core_wasm_idempotency_token.hpp" // IdempotencyAuthorityId/Token
#include "runtime/engine/core_wasm_resume_record.hpp"    // PayloadSlotId, CoreWasmResumeRecord
#include "runtime/engine/core_wasm_schema_module.hpp"    // VerifiedCoreWasmSchemaModule, call site
#include "runtime/engine/durable_effect_authority.hpp"  // DurableEffectAuthority, ResultHandle
#include "runtime/engine/payload_store.hpp"              // ResumeSnapshot, payload_store::Slot
#include "runtime/engine/payload_store_codec.hpp"        // PayloadStoreError
#include "runtime/value/value.hpp"                   // runtime::Value

namespace ahfl::runtime::core_wasm_resume_controller {

namespace detail {
// Opaque controller state, defined only in the .cpp. Each opaque handle below holds a
// `std::unique_ptr` to one of these so its layout, and every reused decoder type it
// composes, stays out of the public header.
struct GatedState;
struct PreparedState;
struct PendingState;
} // namespace detail

// The whole linear memory's fixed-single-page byte capacity, as a named strong type
// so a raw span size can never be passed where a capacity is expected. The two-pass
// TOTAL preflight compares its checked reservation against this.
struct LinearMemoryCapacityBytes {
    std::uint64_t value{0};
    [[nodiscard]] friend bool operator==(LinearMemoryCapacityBytes,
                                         LinearMemoryCapacityBytes) noexcept = default;
};

// The EXACT set of reasons the two admission phases can fail with. Digest mismatches
// are reported by artifact after ALL three fixed-work compares complete (priority
// Module -> WireSchema -> ExecManifest). `Unbounded` and `ResourceExhausted` are the
// two-pass TOTAL preflight verdicts. There is no event / slot-authority reason here:
// those belong to the step phase, never to preflight.
enum class ResumePrepareReason : std::uint8_t {
    ModuleDigestMismatch,
    WireSchemaDigestMismatch,
    ExecManifestDigestMismatch,
    CoordinateMismatch,
    PayloadSchemaInvalid,
    TransitionInvalid,
    Unbounded,
    ResourceExhausted,
};

// The EXACT set of reasons a per-import / publish / terminal step can fail with. A
// controller-side generation / slot / state fault is always `TransitionInvalid`; the
// store's OWN generation error stays inside the `PayloadStoreError` arm below.
enum class ResumeStepReason : std::uint8_t {
    CoordinateMismatch,
    EventMalformed,
    PayloadSchemaInvalid,
    TransitionInvalid,
    ModuleError,
    ModuleTrap,
};

// Two closed error SSOTs. A controller reason OR the real 16-variant store error
// (or, at the D2b-4 dedup seam, the real durable-effect backend error), never a
// bool / optional / illegal-combination struct.
using ResumePrepareError = std::variant<ResumePrepareReason, payload_store::PayloadStoreError>;
using ResumeStepError = std::variant<ResumeStepReason,
                                     payload_store::PayloadStoreError,
                                     durable_effect_authority::DurableEffectBackendError>;

// A read-only, non-owning view of the L0 workflow ENTRY frame: the verbatim opaque
// bytes of the authenticated slot whose id equals `record.entry_input_slot`. It is
// NEVER decoded (AHFLWS carries no entry schema). Available ONLY from a minted
// `PreparedResume`; the span points into controller-owned storage and stays valid for
// that `PreparedResume`'s lifetime.
class EntryFrame {
  public:
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] core_wasm_resume::PayloadSlotId slot() const noexcept { return slot_; }

  private:
    friend struct detail::PreparedState;
    EntryFrame(std::span<const std::uint8_t> bytes, core_wasm_resume::PayloadSlotId slot) noexcept
        : bytes_(bytes), slot_(slot) {}

    std::span<const std::uint8_t> bytes_{};
    core_wasm_resume::PayloadSlotId slot_{};
};

// PHASE-1 result: the snapshot has passed the three artifact-digest gates and the
// coordinate / A2-baseline gate, but NO slot has been admitted and NO transition
// eligibility judged. Opaque, move-only; minted only by `open_gated_resume`.
struct GatedResumeOptions;
class GatedResume {
  public:
    GatedResume(const GatedResume &) = delete;
    GatedResume &operator=(const GatedResume &) = delete;
    GatedResume(GatedResume &&) noexcept;
    GatedResume &operator=(GatedResume &&) noexcept;
    ~GatedResume();

  private:
    friend std::expected<GatedResume, ResumePrepareError>
    open_gated_resume(const core_wasm_schema_module::VerifiedCoreWasmSchemaModule &,
                      payload_store::ResumeSnapshot &&, GatedResumeOptions);
    friend std::expected<std::variant<class PreparedResume, class PendingInjection>,
                         ResumePrepareError>
    admit_and_preflight(GatedResume &&, std::span<const std::uint8_t, 16>,
                        std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                        LinearMemoryCapacityBytes);
    explicit GatedResume(std::unique_ptr<detail::GatedState> state) noexcept;

    std::unique_ptr<detail::GatedState> state_;
};

// D2b-4 binding for the token-aware ReadyForLive frontier. The checkpoint
// namespace `(CoreWorkflowId, ResumeCheckpointId)` is NOT supplied here: it is
// read from the snapshot's AUTHENTICATED commit manifest (the SSOT), so a
// caller cannot mint a token under a namespace inconsistent with the loaded
// generation. Only the dedup authority identity and the optional read-only
// decision authority are host-supplied.
struct GatedResumeOptions {
    // Opaque 16-byte identity of the durable-effect backend, bound once for the
    // whole checkpoint lifetime (seam lines 500-503); never derived from
    // key_id/path/hostname. A zero/absent value would only ever mint Fresh
    // tokens, so it is an explicit required input when dedup is enabled.
    core_wasm_idempotency_token::IdempotencyAuthorityId authority_id{};

    // When set, every AFTER-frontier import consults this authority READ-ONLY
    // (preview_begin: it seals nothing) before issuing ReadyForLive. When
    // absent, the controller behaves exactly as pre-D2b-4 (always ReadyForLive)
    // and no token is computed. Borrowed for the GatedResume lifetime only; the
    // caller owns the (shared, backend-backed) authority.
    const durable_effect_authority::DurableEffectAuthority *dedup_authority{nullptr};
};

// A fully-preflighted, ready-to-drive replay. Opaque, move-only; minted only by
// `admit_and_preflight` (Injected path) or `supply_injected_result` (Suspended path).
// It carries an internal phase machine
//   Replaying -> AwaitingSlot -> AwaitingPublishAck -> Replaying
//             -> AwaitingLiveResult (follow-on; step APIs TransitionInvalid here)
//             -> AwaitingConsumedAck -> Consumed
//             -> Failed (any controller fault or store error)
// exercised only through the free functions below. Every span it hands out stays
// valid for the whole `PreparedResume` lifetime.
class PreparedResume {
  public:
    PreparedResume(const PreparedResume &) = delete;
    PreparedResume &operator=(const PreparedResume &) = delete;
    PreparedResume(PreparedResume &&) noexcept;
    PreparedResume &operator=(PreparedResume &&) noexcept;
    ~PreparedResume();

    // L0 authority: the entry frame view (see `EntryFrame`). Read-only; no alloc.
    [[nodiscard]] EntryFrame entry_frame() const noexcept;

  private:
    friend struct detail::PreparedState;
    explicit PreparedResume(std::unique_ptr<detail::PreparedState> state) noexcept;

    std::unique_ptr<detail::PreparedState> state_;
};

// A Suspended replay that reached the frontier with NO injected result supplied. It
// OWNS the already-admitted intermediate state (slots admitted, memo decoded --
// `admit_slots` is one-shot and never retried) plus the capacity and module
// authority. `supply_injected_result` is the one-shot continuation; any failure
// destroys it and fails closed. Opaque, move-only.
class PendingInjection {
  public:
    PendingInjection(const PendingInjection &) = delete;
    PendingInjection &operator=(const PendingInjection &) = delete;
    PendingInjection(PendingInjection &&) noexcept;
    PendingInjection &operator=(PendingInjection &&) noexcept;
    ~PendingInjection();

  private:
    friend struct detail::PendingState;
    friend std::expected<PreparedResume, ResumePrepareError>
    supply_injected_result(PendingInjection &&, std::span<const std::uint8_t>);
    explicit PendingInjection(std::unique_ptr<detail::PendingState> state) noexcept;

    std::unique_ptr<detail::PendingState> state_;
};

// The outcome of `admit_and_preflight`: a ready replay, or a Suspended replay still
// awaiting its injected result. A distinct sum type, never an error.
using AdmitOutcome = std::variant<PreparedResume, PendingInjection>;

// The adapter-supplied observation of ONE module import callback. `observed_ordinal`
// is the module's own import cursor; `module_param_frame` is the raw Param bytes the
// module presented; `whole_linear_memory` is the entire module memory (the sole
// authority the controller re-decodes the node-event buffer from). The controller
// itself decodes the events and derives the expected coordinate -- the adapter
// supplies no decoded prefix and no expected coordinate.
struct ImportStepInput {
    core_wasm_schema_module::CapabilityImportOrdinal observed_ordinal{};
    std::span<const std::uint8_t> module_param_frame{};
    std::span<const std::uint8_t> whole_linear_memory{};
};

// Decision: return these exact stored memo result bytes to the module. The span
// points into controller-owned storage, valid for the `PreparedResume` lifetime.
struct ReturnMemo {
    std::span<const std::uint8_t> memo_bytes{};
};

// Decision: this is the frontier import and it needs a fresh injected result slot
// bound before it can proceed. The controller has moved to AwaitingSlot; the adapter
// must call `bind_publish_injected`.
struct NeedInjectedSlot {};

// Decision: replay is past the frontier; the adapter must invoke the live capability.
// It carries the controller-verified call-site identity, the controller-computed
// argument hash, and the EXACT arity-1 typed, Verified-decoded Param value (owned, so
// never dangling). AFTER-frontier only. `arg_hash` is the wire-canonical value hash;
// it is NEVER the D0 IdempotencyToken. Returning this moves the controller to
// AwaitingLiveResult (a follow-on state -- step APIs are TransitionInvalid there in
// this slice).
struct ReadyForLive {
    core_wasm_schema_module::VerifiedCoreWasmCallSite call_site;
    std::uint64_t arg_hash{0};
    std::vector<runtime::Value> params; // exactly one element
};

// D2b-4 decision: the D2b authority already has a SUCCEEDED terminal for this
// exact token. The adapter MUST NOT issue a second live effect; it reads the
// recorded typed result through `handle` (the only way result bytes leave the
// authority) and feeds it back to the instance via the (follow-on, blocked)
// live-response API. Decision-only: the controller performs no read, effect, or
// CAS and moves to AwaitingLiveResult. `token` is the D0 token minted from the
// controller's wf/ckpt/authority/call-site/canonical-param coordinate.
struct DedupReplay {
    core_wasm_idempotency_token::IdempotencyToken token;
    durable_effect_authority::ResultHandle handle;
};

// D2b-4 decision: symmetric to DedupReplay for a prior recorded FAILURE. A
// repeated begin after a terminal failure replays the failure instead of
// silently issuing a second effect; the host re-surfaces the recorded failure
// (read via `handle`) through the follow-on live-response API.
struct DedupReplayFailure {
    core_wasm_idempotency_token::IdempotencyToken token;
    durable_effect_authority::ResultHandle handle;
};

// D2b-4 decision: the authority holds a PENDING row for this exact token (a
// prior attempt crashed/reopened before a terminal was recorded). The host MUST
// reconcile that row (recover) rather than invoke a fresh live effect; no
// result bytes exist yet, so this arm carries only the token. Decision-only;
// moves to AwaitingLiveResult.
struct RecoverPending {
    core_wasm_idempotency_token::IdempotencyToken token;
};

// A closed per-import decision. Publish and terminal decisions are NOT here.
using ImportStepDecision = std::variant<ReturnMemo,
                                        NeedInjectedSlot,
                                        ReadyForLive,
                                        DedupReplay,
                                        DedupReplayFailure,
                                        RecoverPending>;

// The COMPLETE, controller-owned publish plan for the Suspended->Injected transition.
// The controller decides every byte: the updated record (Suspended->Injected, the
// frontier pending entry promoted to an ordinal-0 memo bound to the chosen slot,
// pending cleared) and the exact distinct slot set
//   {entry_input_slot} U {distinct existing memo result slots} U {new injected slot}.
// The adapter (D2a) only allocates the candidate slot id and calls
// `store.publish_available(..., expected_generation(), record(), slots(), ...)`; it
// cannot rewrite the transition. Every view points into controller-owned storage that
// stays stable at least until the store result is ACKed.
struct PublishInjectedPlan {
    [[nodiscard]] std::uint64_t expected_generation() const noexcept {
        return expected_generation_;
    }
    [[nodiscard]] const core_wasm_resume::CoreWasmResumeRecord &record() const noexcept {
        return *record_;
    }
    [[nodiscard]] std::span<const payload_store::Slot> slots() const noexcept { return slots_; }

  private:
    friend struct detail::PreparedState;
    PublishInjectedPlan(std::uint64_t expected_generation,
                        const core_wasm_resume::CoreWasmResumeRecord *record,
                        std::span<const payload_store::Slot> slots) noexcept
        : expected_generation_(expected_generation), record_(record), slots_(slots) {}

    std::uint64_t expected_generation_{0};
    const core_wasm_resume::CoreWasmResumeRecord *record_{nullptr};
    std::span<const payload_store::Slot> slots_{};
};

// The raw run2 exit the adapter observed, classified by the CONTROLLER (not the
// adapter): a normal return with the raw u32 status word and the whole linear memory,
// or a trap. The adapter never pre-classifies OK / PENDING / ERROR / trap into an
// enum; the controller maps the raw status.
struct Run2Returned {
    std::uint32_t raw_status{0};
    std::span<const std::uint8_t> whole_linear_memory{};
};
struct Run2Trapped {};
using Run2Exit = std::variant<Run2Returned, Run2Trapped>;

// The controller-owned plan to tombstone the consumed generation. The store call is
// manifest-only, so the plan carries only the expected current generation.
struct MarkConsumedPlan {
    [[nodiscard]] std::uint64_t expected_generation() const noexcept {
        return expected_generation_;
    }

  private:
    friend struct detail::PreparedState;
    explicit MarkConsumedPlan(std::uint64_t expected_generation) noexcept
        : expected_generation_(expected_generation) {}

    std::uint64_t expected_generation_{0};
};

// -------------------------------------------------------------------------------
// PHASE 1: digest + coordinate / A2-baseline gates. NO slot admission, NO
// eligibility. The module is captured for later phases; the snapshot is moved in.
// -------------------------------------------------------------------------------
[[nodiscard]] std::expected<GatedResume, ResumePrepareError>
open_gated_resume(const core_wasm_schema_module::VerifiedCoreWasmSchemaModule &module,
                  payload_store::ResumeSnapshot &&snapshot,
                  GatedResumeOptions options = GatedResumeOptions{});

// -------------------------------------------------------------------------------
// PHASE 2: admit the exact slot set (one-shot), decode stored memo per occurrence,
// judge transition eligibility, then EITHER yield `PendingInjection` (Suspended with
// an empty injected input) OR Verified-decode the injected result and run the full
// two-pass TOTAL preflight. `key` is borrowed for admission and never stored. The
// injected-input matrix:
//   Suspended + empty    -> PendingInjection (await supply_injected_result)
//   Suspended + nonempty -> decode injected + two-pass TOTAL -> PreparedResume
//   Injected  + empty    -> normal two-pass TOTAL -> PreparedResume
//   Injected  + nonempty -> TransitionInvalid (an Injected record takes no new input)
// -------------------------------------------------------------------------------
[[nodiscard]] std::expected<AdmitOutcome, ResumePrepareError>
admit_and_preflight(GatedResume &&gated, std::span<const std::uint8_t, 16> key_id,
                    std::span<const std::uint8_t> key,
                    std::span<const std::uint8_t> injected_or_empty,
                    LinearMemoryCapacityBytes capacity);

// Continuation for a `PendingInjection`: Verified-decode the injected result against
// the frontier's Result binding, then run the FULL two-pass TOTAL preflight with the
// injected result's actual length. One-shot (consumes the rvalue); any failure fails
// closed.
[[nodiscard]] std::expected<PreparedResume, ResumePrepareError>
supply_injected_result(PendingInjection &&pending, std::span<const std::uint8_t> injected_bytes);

// -------------------------------------------------------------------------------
// Per-import decision. Re-decodes the published node-event prefix from the supplied
// whole linear memory and joins it record-by-record to the manifest (identity: kind +
// workflow_node_id + schedule_pos; capability: additionally capability +
// source_symbol + invocation_ordinal), requiring a dense prefix whose length equals
// the current expected schedule position. Complexity is per-import O(current
// event_count) and whole-run O(sum of prefixes) = worst-case O(N^2); this slice
// introduces no incremental decoder. Only valid in the Replaying phase.
// -------------------------------------------------------------------------------
[[nodiscard]] std::expected<ImportStepDecision, ResumeStepError>
next_import(PreparedResume &prepared, const ImportStepInput &input);

// -------------------------------------------------------------------------------
// Publish handshake (two steps, in place; no moved-from reference). `bind` validates
// the adapter-chosen fresh slot (non-invalid, distinct from the entry slot and every
// existing memo result slot) and PURELY builds the complete plan into controller-owned
// storage, moving to AwaitingPublishAck. `ack` consumes the store's real result: on
// error it keeps the `PayloadStoreError` and fails; on success it checks N+1 == M,
// updates the internal record / generation, returns to Replaying, and returns the
// stable injected bytes (valid for the `PreparedResume` lifetime). A duplicate / late
// ACK is `TransitionInvalid`.
// -------------------------------------------------------------------------------
[[nodiscard]] std::expected<PublishInjectedPlan, ResumeStepError>
bind_publish_injected(PreparedResume &prepared, core_wasm_resume::PayloadSlotId chosen_slot);

[[nodiscard]] std::expected<std::span<const std::uint8_t>, ResumeStepError>
ack_publish_injected(
    PreparedResume &prepared,
    std::expected<std::uint64_t, payload_store::PayloadStoreError> store_result);

// -------------------------------------------------------------------------------
// Terminal run2-exit gate. Only valid in the Replaying phase. The controller
// classifies the raw exit:
//   Run2Returned raw OK       -> full published-prefix gate -> MarkConsumedPlan
//                                (moves to AwaitingConsumedAck)
//   Run2Returned raw PENDING  -> TransitionInvalid (a second PENDING is ineligible)
//   Run2Returned raw ERROR/*  -> ModuleError
//   Run2Trapped               -> ModuleTrap
// Only the OK path yields a plan / emits a tombstone.
// -------------------------------------------------------------------------------
[[nodiscard]] std::expected<MarkConsumedPlan, ResumeStepError>
finish_run(PreparedResume &prepared, const Run2Exit &exit);

// ACK the `mark_consumed` store result. Only valid in AwaitingConsumedAck. On error
// keeps the `PayloadStoreError` and fails; on success checks N+1 == M and moves to
// the terminal Consumed phase. A duplicate ACK is `TransitionInvalid`.
[[nodiscard]] std::expected<void, ResumeStepError>
ack_mark_consumed(PreparedResume &prepared,
                  std::expected<std::uint64_t, payload_store::PayloadStoreError> store_result);

} // namespace ahfl::runtime::core_wasm_resume_controller
