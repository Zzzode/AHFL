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

/// The digest-EXCLUDING call-site location inside a checkpoint namespace:
/// (namespace, node, ordinal, capability, source_symbol). It deliberately
/// carries NEITHER the authority NOR param_digest: lookups at this location are
/// how the authority detects (a) same-authority param divergence and (b) a
/// foreign-authority registration at an otherwise identical coordinate.
struct EffectCallSiteLocation {
    CheckpointNamespace checkpoint_namespace{};
    ir::core::CoreWorkflowNodeId node{};
    core_wasm_resume::InvocationOrdinal ordinal{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};

    [[nodiscard]] friend bool operator==(const EffectCallSiteLocation &,
                                         const EffectCallSiteLocation &) noexcept = default;
};

/// One Pending row returned by recover(): everything a host reconciler needs to
/// re-drive the effect, and nothing terminal (Succeeded/Failed rows are never
/// listed).
struct PendingRecoveryEntry {
    IdempotencyAuthorityId authority{};
    IdempotencyToken token{};
    IntentCoordinate coordinate{};
};

// ---- typed storage errors (closed sets, no bool/string error channel) --------

/// Storage-layer failures, independent of the dedup DOMAIN decisions. The
/// in-memory backend never returns these; the future durable backend uses them.
enum class DurableEffectBackendError : std::uint8_t {
    StorageUnavailable, // the backend cannot currently serve the operation
    StorageCorrupt,     // sealed state is internally inconsistent / handle stale
};

/// Domain conflict when CAS-completing a Pending registration.
enum class RegistrationConflict : std::uint8_t {
    Unknown,         // no registration is sealed at the token
    AlreadyTerminal, // the registration was already completed
};

// ---- the narrow backend seam -------------------------------------------------

/// The storage seam the production POSIX/AEAD store later implements. It seals
/// and looks up sealed registrations and owns recorded payload bytes; it makes
/// NO dedup decisions (those live in DurableEffectAuthority). All methods are
/// linearizable per-backend; the in-memory implementation is process-local.
///
/// begin is a compare-and-seal: `insert_pending` atomically seals a Pending row
/// keyed by the token and returns false when a row already exists, so a racing
/// double begin can never produce two `New` decisions against any conforming
/// backend. `complete_pending` is the CAS Pending -> terminal.
class IDurableEffectBackend {
  public:
    virtual ~IDurableEffectBackend() = default;

    [[nodiscard]] virtual std::optional<SealedEffectRegistration>
    find_registration(const IdempotencyToken &token) const = 0;

    /// Every sealed row at the digest-excluding call-site location, across
    /// authorities (the caller filters by authority).
    [[nodiscard]] virtual std::expected<std::vector<SealedEffectRegistration>,
                                        DurableEffectBackendError>
    find_at_call_site(const EffectCallSiteLocation &location) const = 0;

    /// CAS-seal a Pending registration keyed by `registration.token`. Returns
    /// false (with no mutation) when a row already exists at that token.
    [[nodiscard]] virtual bool insert_pending(SealedEffectRegistration registration) = 0;

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

    /// Copy out the recorded bytes named by `handle`. The handle is the only
    /// way result bytes leave the backend.
    [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>,
                                        DurableEffectBackendError>
    read_payload(ResultHandle handle) const = 0;
};

/// The process-local in-memory backend: flat append-only stores + token and
/// call-site indexes. Same-process recovery only (a fresh authority over the
/// SAME backend instance); it survives no process exit. Holds no key and
/// provides no integrity/confidentiality.
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

    [[nodiscard]] std::expected<std::vector<SealedEffectRegistration>,
                                DurableEffectBackendError>
    find_at_call_site(const EffectCallSiteLocation &location) const override;

    [[nodiscard]] bool insert_pending(SealedEffectRegistration registration) override;

    [[nodiscard]] std::expected<ResultHandle,
                                std::variant<RegistrationConflict,
                                             DurableEffectBackendError>>
    complete_pending(const IdempotencyToken &token, bool succeeded,
                     std::span<const std::uint8_t> typed_payload) override;

    [[nodiscard]] std::expected<std::vector<PendingRecoveryEntry>,
                                DurableEffectBackendError>
    list_pending(const CheckpointNamespace &checkpoint_namespace) const override;

    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                DurableEffectBackendError>
    read_payload(ResultHandle handle) const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::shared_ptr<IDurableEffectBackend>
make_in_memory_durable_effect_backend();

// ---- dedup decisions (Principle 4: closed std::variant) ----------------------

/// Why a begin was a New. `FreshCoordinate`: nothing was sealed at the call
/// site. `AuthorityIsolated`: a row IS sealed at the same call site but under a
/// DIFFERENT authority; the caller's token is a fresh, isolated token -- the
/// namespace-isolation guarantee operating, never a cross-authority replay.
enum class NewEffectScope : std::uint8_t {
    FreshCoordinate,
    AuthorityIsolated,
};

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
