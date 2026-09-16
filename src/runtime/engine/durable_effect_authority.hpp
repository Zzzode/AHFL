#pragma once

// RFC 0026 KR6.5 E4-B2-D2b-3: the host-independent durable-effect
// dedupe / recover / result authority (in-memory backend).
//
// The seam contract is LOCKED in
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md and RFC 0026 line 777:
// D2b is "a durable-effect intent/result authority with
// read/recover/dedup/result authority (a new Core-Wasm typed authority, NOT the
// native write-only void sink)" -- this is a READ/RECOVER authority, not a
// write-only hook. The vertical kernel is:
//
//   begin_effect(intent) -> DedupDecision
//       New | ReplayPending | ReplaySucceeded(handle) | ReplayFailed(handle)
//       | Diverged
//   record_result(token, typed result bytes)  -> ResultHandle
//   record_failure(token, typed failure bytes) -> ResultHandle
//   resolve(token) -> Unknown | Pending | Succeeded(handle) | Failed(handle)
//   recover(namespace) -> Pending recovery rows for host reconciliation
//   read_result(handle) -> the recorded bytes
//
// SCOPE / NON-GOALS (see also DurableEffectGuarantee):
//  * This authority is an IDENTITY/COLLISION authority ONLY. It dedupes on the
//    slice-1 IdempotencyToken (SHA-256 over the fixed 103-byte preimage) and on
//    the digest-excluding call site, fail-CLOSED on collision. It is NEVER an
//    authenticity authority and NEVER a rollback authority (RFC line 777): it
//    performs no HMAC, no encryption, no key management, no lease, and no
//    at-most-once side effect. It must NOT be read as claiming exactly-once
//    through IntegrityPayloadStore slots -- those are integrity-only and their
//    guarantee set is empty for exactly-once.
//  * The shipped backend is IN-MEMORY and same-process only. It makes no crash /
//    power-loss durability claim: a simulated "crash" is a fresh
//    DurableEffectAuthority over the SAME backend instance. The durable
//    POSIX/AEAD production store is a separately blocked item; when it lands it
//    implements the narrow IDurableEffectBackend seam below, and none of the
//    typed DECISIONS in this header change.
//  * Identity is strong ids / indices only (Principle 2): tokens, authority
//    ids, namespaces, call coordinates, and the ResultHandle index into the
//    backend's flat recorded-payload store. Result bytes reach callers only via
//    read_result(ResultHandle); no decision, resolution, recovery row, or error
//    ever carries or stringifies a result byte.

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "runtime/engine/durable_effect_intent.hpp"

namespace ahfl::runtime::durable_effect_authority {

using durable_effect_intent::CheckpointNamespace;
using durable_effect_intent::DurableEffectIntent;
using durable_effect_intent::IntentCoordinate;
using core_wasm_idempotency_token::IdempotencyAuthorityId;
using core_wasm_idempotency_token::IdempotencyToken;

// ---- guarantee declaration (typed; never silently upgraded) -----------------

/// Every guarantee question about this authority is answered by visiting one of
/// these arms, so a future backend cannot quietly claim a guarantee it does not
/// provide. The first two arms ARE provided (by the in-memory backend only
/// within one process for recovery); the last three are EXPLICIT NON-GOALS of
/// the D2b authority itself, regardless of backend.
enum class DurableEffectGuarantee : std::uint8_t {
    IdentityAndCollisionDedup, // PROVIDED: New/Replay/Diverged begin decisions
    SameProcessRecovery,       // PROVIDED by InMemoryDurableEffectBackend:
                               // recover() through a fresh authority over the
                               // same live backend, same process only
    CrossProcessDurability,    // NOT PROVIDED here (blocked POSIX/AEAD store)
    Authenticity,              // NEVER PROVIDED: HMAC belongs to payload store
    Rollback,                  // NEVER PROVIDED: needs the protected store
};

/// The typed, exhaustively-compiled in-memory guarantee table. A newly added
/// guarantee arm makes this switch fail to compile until its claim is decided.
[[nodiscard]] constexpr bool
guarantee_provided_by_in_memory_backend(DurableEffectGuarantee guarantee) noexcept {
    switch (guarantee) {
    case DurableEffectGuarantee::IdentityAndCollisionDedup:
        return true;
    case DurableEffectGuarantee::SameProcessRecovery:
        return true;
    case DurableEffectGuarantee::CrossProcessDurability:
        return false;
    case DurableEffectGuarantee::Authenticity:
        return false;
    case DurableEffectGuarantee::Rollback:
        return false;
    }
    return false;
}

// ---- index-based result identity --------------------------------------------

/// Opaque index into the backend's flat recorded-payload store. It names one
/// recorded terminal payload (success or failure); success vs failure is the
/// registration's terminal state, never a property of the handle. The
/// UINT64_MAX sentinel is never returned by a successful record.
struct ResultHandle {
    static constexpr std::uint64_t kInvalid = UINT64_MAX;
    std::uint64_t value{kInvalid};

    [[nodiscard]] friend bool operator==(ResultHandle, ResultHandle) noexcept = default;
};

// ---- sealed backend records --------------------------------------------------

/// Terminal state as sealed by the backend. Pending carries no payload; each
/// terminal arm carries the ResultHandle of the recorded typed bytes.
struct SealedPending {};
struct SealedSucceeded {
    ResultHandle handle{};
};
struct SealedFailed {
    ResultHandle handle{};
};
using SealedTerminal = std::variant<SealedPending, SealedSucceeded, SealedFailed>;

/// One sealed effect registration exactly as the backend holds it. This is the
/// storage-layer value; the decision layer projects it onto DedupDecision /
/// EffectResolution and never stores a private copy.
struct SealedEffectRegistration {
    IdempotencyAuthorityId authority{};
    IdempotencyToken token{};
    IntentCoordinate coordinate{};
    SealedTerminal terminal{SealedPending{}};

    [[nodiscard]] friend bool operator==(const SealedEffectRegistration &,
                                         const SealedEffectRegistration &) noexcept = default;
};

/// Why a begin sealed a New effect. `FreshCoordinate`: nothing was sealed at
/// the call site. `AuthorityIsolated`: a row IS sealed at the same call site
/// but under a DIFFERENT authority; the caller's token is a fresh, isolated
/// token -- the namespace-isolation guarantee operating, never a
/// cross-authority replay.
enum class NewEffectScope : std::uint8_t {
    FreshCoordinate,
    AuthorityIsolated,
};

/// The atomic outcome of `IDurableEffectBackend::seal_or_load`. The whole
/// begin collision check (token lookup + digest-excluding call-site scan +
/// Pending insert) is decided at the backend's single linearization point, so
/// the decision layer never performs a check-then-act sequence.
struct SealSealed {
    // No token row existed: a fresh Pending row was sealed.
    NewEffectScope scope{};
};
struct SealDiverged {
    // A same-authority row at the same call site carries a DIFFERENT param
    // digest; nothing was sealed (fail closed).
};
struct SealExisting {
    // A row already owns the token (an identical concurrent begin lost the
    // race, or the effect was already sealed): replay its sealed lifecycle.
    SealedEffectRegistration registration{};
};
using SealOutcome = std::variant<SealSealed, SealDiverged, SealExisting>;

/// One Pending row returned by recover(): everything a host reconciler needs to
/// re-drive the effect, and nothing terminal (Succeeded/Failed rows are never
/// listed).
struct PendingRecoveryEntry {
    IdempotencyAuthorityId authority{};
    IdempotencyToken token{};
    IntentCoordinate coordinate{};
};

/// READ-ONLY projection of sealed state at ONE digest-EXCLUDING call site
/// (D2b-4). `own_registration` is the single row the querying authority owns at
/// the site (absent when it owns none); `foreign_authority_present` reports a
/// row under a DIFFERENT authority (namespace isolation: it never joins the
/// own-authority decision, exactly as in `seal_or_load`). This carries no
/// decision: the caller still compares `own_registration`'s token/param digest
/// against the in-flight coordinate.
struct CallSiteLookup {
    std::optional<SealedEffectRegistration> own_registration{};
    bool foreign_authority_present{false};
};

// ---- typed storage errors (closed sets, no bool/string error channel) --------

/// Storage-layer failures, independent of the dedup DOMAIN decisions. The
/// in-memory backend only raises StorageUnavailable in principle (its flat
/// stores cannot be torn); the future durable backend reports genuine
/// sealed-state / payload-integrity inconsistency as StorageCorrupt. A handle
/// naming no payload is NOT corruption -- it is the domain lookup miss
/// PayloadReadConflict::UnknownHandle.
enum class DurableEffectBackendError : std::uint8_t {
    StorageUnavailable, // the backend cannot currently serve the operation
    StorageCorrupt,     // sealed state is internally inconsistent / payload torn
};

/// Domain conflict when CAS-completing a Pending registration.
enum class RegistrationConflict : std::uint8_t {
    Unknown,         // no registration is sealed at the token
    AlreadyTerminal, // the registration was already completed
};

/// Domain conflict when reading back a recorded payload: the handle names no
/// stored payload. This is a lookup MISS, never an integrity failure -- a torn
/// / truncated / internally inconsistent payload is a
/// DurableEffectBackendError::StorageCorrupt and passes through unchanged.
enum class PayloadReadConflict : std::uint8_t {
    UnknownHandle, // the handle is kInvalid or past the flat store's end
};

// ---- the narrow backend seam -------------------------------------------------

/// The storage seam the production POSIX/AEAD store later implements. It seals
/// and looks up sealed registrations and owns recorded payload bytes; it makes
/// NO dedup decisions (those live in DurableEffectAuthority). All methods are
/// linearizable per-backend; the in-memory implementation is process-local.
///
/// begin is ONE atomic transaction, `seal_or_load`: at the backend's single
/// linearization point it looks up the token, scans the digest-excluding call
/// site, and seals Pending (or returns the collision/existing outcome), so a
/// racing double begin can never produce two `New` decisions against any
/// conforming backend. The decision layer must never re-derive the call-site
/// collision check from separate find/insert calls. `complete_pending` is the
/// CAS Pending -> terminal.
class IDurableEffectBackend {
  public:
    virtual ~IDurableEffectBackend() = default;

    [[nodiscard]] virtual std::optional<SealedEffectRegistration>
    find_registration(const IdempotencyToken &token) const = 0;

    /// Atomically decide one begin. `candidate` is the Pending registration the
    /// caller wants sealed (token + authority + full coordinate). Under the
    /// backend's single linearization point:
    ///  * a row already owns `candidate.token` -> SealExisting (the racing
    ///    identical begin, or a repeat begin), no mutation;
    ///  * otherwise a same-authority row at the SAME digest-excluding call site
    ///    carries a DIFFERENT param digest -> SealDiverged, nothing is sealed;
    ///  * otherwise the candidate is appended as Pending -> SealSealed, carrying
    ///    AuthorityIsolated when the only rows at the call site belong to a
    ///    foreign authority, else FreshCoordinate.
    /// A durable implementation realizes this as one transaction (a unique
    /// constraint on token plus a conditional sibling check).
    [[nodiscard]] virtual std::expected<SealOutcome, DurableEffectBackendError>
    seal_or_load(SealedEffectRegistration candidate) = 0;

    /// CAS a Pending registration to a terminal state, storing the opaque typed
    /// payload bytes and returning their ResultHandle. Fails typed on an
    /// unknown token or an already-terminal registration.
    [[nodiscard]] virtual std::expected<ResultHandle,
                                        std::variant<RegistrationConflict,
                                                     DurableEffectBackendError>>
    complete_pending(const IdempotencyToken &token, bool succeeded,
                     std::span<const std::uint8_t> typed_payload) = 0;

    /// All Pending rows sealed in `namespace` (across authorities), in
    /// unspecified order. Terminal rows are never returned.
    [[nodiscard]] virtual std::expected<std::vector<PendingRecoveryEntry>,
                                        DurableEffectBackendError>
    list_pending(const CheckpointNamespace &checkpoint_namespace) const = 0;

    /// READ-ONLY lookup at one digest-EXCLUDING call site (D2b-4): return the
    /// row the CALLING authority owns there plus whether any FOREIGN-authority
    /// row exists at the same site. NOTHING is sealed or mutated. At most one
    /// same-authority row can exist per call site -- `seal_or_load` rejects a
    /// same-authority different-digest second seal -- so a second own row is
    /// backend corruption, not a normal miss. Foreign rows are isolated
    /// (invisible to the owning-authority decision) exactly as in the sealing
    /// begin. This is the single read the decision-only resume controller
    /// consults at the ReadyForLive frontier; it can never replace the mutating
    /// `seal_or_load` the host runs when it actually issues a live effect.
    [[nodiscard]] virtual std::expected<CallSiteLookup, DurableEffectBackendError>
    find_registration_at_call_site(const IdempotencyAuthorityId &authority,
                                   const CheckpointNamespace &checkpoint_namespace,
                                   ir::core::CoreWorkflowNodeId node,
                                   core_wasm_resume::InvocationOrdinal ordinal,
                                   ir::core::CoreCapabilityId capability,
                                   std::uint64_t source_symbol) const = 0;

    /// Copy out the recorded bytes named by `handle`. The handle is the only
    /// way result bytes leave the backend. A handle naming no stored payload is
    /// the domain conflict PayloadReadConflict::UnknownHandle; genuine
    /// sealed-state / payload-integrity failures pass through as
    /// DurableEffectBackendError.
    [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>,
                                        std::variant<PayloadReadConflict,
                                                     DurableEffectBackendError>>
    read_payload(ResultHandle handle) const = 0;
};

/// The process-local in-memory backend: flat append-only stores, a token
/// index, and an in-critical-section call-site scan. Same-process recovery
/// only (a fresh authority over the SAME backend instance); it survives no
/// process exit. Holds no key and provides no integrity/confidentiality.
class InMemoryDurableEffectBackend final : public IDurableEffectBackend {
  public:
    InMemoryDurableEffectBackend();
    ~InMemoryDurableEffectBackend() override;

    InMemoryDurableEffectBackend(const InMemoryDurableEffectBackend &) = delete;
    InMemoryDurableEffectBackend &operator=(const InMemoryDurableEffectBackend &) = delete;
    InMemoryDurableEffectBackend(InMemoryDurableEffectBackend &&) noexcept;
    InMemoryDurableEffectBackend &operator=(InMemoryDurableEffectBackend &&) noexcept;

    [[nodiscard]] std::optional<SealedEffectRegistration>
    find_registration(const IdempotencyToken &token) const override;

    [[nodiscard]] std::expected<SealOutcome, DurableEffectBackendError>
    seal_or_load(SealedEffectRegistration candidate) override;

    [[nodiscard]] std::expected<ResultHandle,
                                std::variant<RegistrationConflict,
                                             DurableEffectBackendError>>
    complete_pending(const IdempotencyToken &token, bool succeeded,
                     std::span<const std::uint8_t> typed_payload) override;

    [[nodiscard]] std::expected<std::vector<PendingRecoveryEntry>,
                                DurableEffectBackendError>
    list_pending(const CheckpointNamespace &checkpoint_namespace) const override;

    [[nodiscard]] std::expected<CallSiteLookup, DurableEffectBackendError>
    find_registration_at_call_site(const IdempotencyAuthorityId &authority,
                                   const CheckpointNamespace &checkpoint_namespace,
                                   ir::core::CoreWorkflowNodeId node,
                                   core_wasm_resume::InvocationOrdinal ordinal,
                                   ir::core::CoreCapabilityId capability,
                                   std::uint64_t source_symbol) const override;

    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                std::variant<PayloadReadConflict,
                                             DurableEffectBackendError>>
    read_payload(ResultHandle handle) const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::shared_ptr<IDurableEffectBackend>
make_in_memory_durable_effect_backend();

// ---- dedup decisions (Principle 4: closed std::variant) ----------------------

struct EffectNew {
    NewEffectScope scope{};
};
struct EffectReplayPending {};
struct EffectReplaySucceeded {
    ResultHandle handle{};
};
/// Additive terminal-replay arm (the spec's four-arm kernel names only the
/// Succeeded replay): a repeated begin after a recorded FAILURE replays the
/// failure rather than silently issuing a second effect.
struct EffectReplayFailed {
    ResultHandle handle{};
};
/// Fail-closed collision: within the SAME authority and checkpoint namespace,
/// the same call site (node/ordinal/capability/source_symbol) is already sealed
/// with a DIFFERENT param digest. No second effect is sealed and no cross-bytes
/// are exposed.
struct EffectDiverged {};

using DedupDecision = std::variant<EffectNew,
                                   EffectReplayPending,
                                   EffectReplaySucceeded,
                                   EffectReplayFailed,
                                   EffectDiverged>;

// ---- READ-ONLY begin preview (D2b-4: no seal) --------------------------------

/// A NON-MUTATING projection of the same state `begin_effect` decides from.
/// The decision-only resume controller consults this at the ReadyForLive
/// frontier: it can never seal the Pending row that marks the host's intent to
/// perform an effect, so this preview -- and NOT `begin_effect` -- is the only
/// authority call the controller may make. The host still runs the sealing
/// `begin_effect` when it actually issues the live call; a racing seal between
/// preview and begin is resolved by that later atomic transaction.
struct PreviewFresh {
    // Mirrors EffectNew::scope: AuthorityIsolated when only FOREIGN-authority
    // rows exist at the call site.
    NewEffectScope scope{};
};
struct PreviewReplayPending {
    // The sealed row's token (equals the in-flight intent token; the digest
    // matched, otherwise the verdict is PreviewDiverged).
    IdempotencyToken token{};
};
struct PreviewReplaySucceeded {
    IdempotencyToken token{};
    ResultHandle handle{};
};
struct PreviewReplayFailed {
    IdempotencyToken token{};
    ResultHandle handle{};
};
struct PreviewDiverged {};
using BeginPreview = std::variant<PreviewFresh,
                                  PreviewReplayPending,
                                  PreviewReplaySucceeded,
                                  PreviewReplayFailed,
                                  PreviewDiverged>;

// ---- resolutions -------------------------------------------------------------

struct EffectUnknown {};
struct EffectPending {};
struct EffectSucceeded {
    ResultHandle handle{};
};
struct EffectFailed {
    ResultHandle handle{};
};
using EffectResolution = std::variant<EffectUnknown,
                                      EffectPending,
                                      EffectSucceeded,
                                      EffectFailed>;

// ---- authority-facing domain errors ------------------------------------------

enum class EffectTerminalError : std::uint8_t {
    UnknownToken,            // record on a token never sealed by begin
    TerminalAlreadyRecorded, // the effect already has a terminal result/failure
};

enum class ResultReadError : std::uint8_t {
    UnknownHandle, // the ResultHandle names no recorded payload
};

// ---- the authority -----------------------------------------------------------

/// The host-independent dedupe / recover / result decision layer over one
/// IDurableEffectBackend. Copyable as a lightweight backend handle (the backend
/// is shared, so a "reopened" authority after a simulated crash is a second
/// instance over the same backend).
class DurableEffectAuthority {
  public:
    explicit DurableEffectAuthority(std::shared_ptr<IDurableEffectBackend> backend)
        : backend_(std::move(backend)) {}

    /// Begin (or replay) one durable effect. See DedupDecision: a fresh call
    /// seals Pending and returns New; a repeated begin returns the prior state;
    /// a same-authority param collision returns Diverged; a foreign-authority
    /// coordinate returns an isolated New(AuthorityIsolated).
    [[nodiscard]] std::expected<DedupDecision, DurableEffectBackendError>
    begin_effect(const DurableEffectIntent &intent) const;

    /// READ-ONLY counterpart of begin_effect (D2b-4): project the SAME sealed
    /// state begin_effect would decide from, but seal nothing. For use by
    /// decision-only callers (the resume controller) that must not register an
    /// effect intent. It mints no row, so a later begin_effect can still return
    /// New; the preview is advisory evidence, never a claim or an exactly-once
    /// guarantee.
    [[nodiscard]] std::expected<BeginPreview, DurableEffectBackendError>
    preview_begin(const DurableEffectIntent &intent) const;

    /// Record the opaque typed SUCCESS bytes for a Pending token and return
    /// their handle. Fails typed on an unknown token or a second terminal
    /// record (the first result is never overwritten).
    [[nodiscard]] std::expected<ResultHandle,
                                std::variant<EffectTerminalError,
                                             DurableEffectBackendError>>
    record_result(const IdempotencyToken &token,
                  std::span<const std::uint8_t> typed_result) const;

    /// Record the opaque typed FAILURE bytes for a Pending token. Symmetric to
    /// record_result; resolve() then reports Failed(handle) and a repeated
    /// begin reports ReplayFailed(handle).
    [[nodiscard]] std::expected<ResultHandle,
                                std::variant<EffectTerminalError,
                                             DurableEffectBackendError>>
    record_failure(const IdempotencyToken &token,
                   std::span<const std::uint8_t> typed_failure) const;

    /// Resolve a token's current lifecycle without sealing anything.
    [[nodiscard]] std::expected<EffectResolution, DurableEffectBackendError>
    resolve(const IdempotencyToken &token) const;

    /// List the Pending effects in `checkpoint_namespace` for host
    /// reconciliation. Succeeded/Failed effects are never listed.
    [[nodiscard]] std::expected<std::vector<PendingRecoveryEntry>,
                                DurableEffectBackendError>
    recover(const CheckpointNamespace &checkpoint_namespace) const;

    /// Read back the recorded bytes named by `handle` (success or failure).
    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                std::variant<ResultReadError,
                                             DurableEffectBackendError>>
    read_result(ResultHandle handle) const;

  private:
    std::shared_ptr<IDurableEffectBackend> backend_;
};

/// Convenience: an authority over a fresh, private in-memory backend.
[[nodiscard]] DurableEffectAuthority make_in_memory_durable_effect_authority();

} // namespace ahfl::runtime::durable_effect_authority
