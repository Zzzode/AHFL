// RFC 0026 KR6.5 E4-B2-D2b-3: host-independent durable-effect
// dedupe / recover / result authority. See durable_effect_authority.hpp for the
// locked contract. This TU implements (1) the process-local in-memory backend
// (flat stores + indexes behind a process-local linearizability mutex) and
// (2) the decision layer that projects sealed storage state onto the closed
// DedupDecision / EffectResolution variants.

#include "runtime/engine/durable_effect_authority.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <array>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace ahfl::runtime::durable_effect_authority {

namespace {

// Whether two rows name the same digest-EXCLUDING call site inside one
// checkpoint namespace: (namespace, node, ordinal, capability, source_symbol).
// Authority and param_digest are deliberately excluded: the seal transaction
// inspects same-call-site rows to detect same-authority param divergence and
// foreign-authority isolation.
[[nodiscard]] bool same_call_site(const IntentCoordinate &lhs,
                                  const IntentCoordinate &rhs) noexcept {
    return lhs.checkpoint_namespace == rhs.checkpoint_namespace && lhs.node == rhs.node &&
           lhs.ordinal == rhs.ordinal && lhs.capability == rhs.capability &&
           lhs.source_symbol == rhs.source_symbol;
}

// Project sealed storage state onto the begin decision. The caller has already
// established that the looked-up row's token (and authority/coordinate) equals
// the in-flight intent's.
[[nodiscard]] DedupDecision project_begin(const SealedTerminal &terminal) noexcept {
    return std::visit(
        Overloaded{
            [](SealedPending) -> DedupDecision { return EffectReplayPending{}; },
            [](SealedSucceeded succeeded) -> DedupDecision {
                return EffectReplaySucceeded{succeeded.handle};
            },
            [](SealedFailed failed) -> DedupDecision {
                return EffectReplayFailed{failed.handle};
            },
        },
        terminal);
}

[[nodiscard]] EffectResolution project_resolution(const SealedTerminal &terminal) noexcept {
    return std::visit(
        Overloaded{
            [](SealedPending) -> EffectResolution { return EffectPending{}; },
            [](SealedSucceeded succeeded) -> EffectResolution {
                return EffectSucceeded{succeeded.handle};
            },
            [](SealedFailed failed) -> EffectResolution {
                return EffectFailed{failed.handle};
            },
        },
        terminal);
}

// Project the atomic seal_or_load outcome onto the begin decision. The
// SealExisting row's token is the in-flight intent's token; because the slice-1
// token deterministically binds authority + full coordinate, a row whose
// authority/coordinate differ is corrupt backend state rather than a replay.
[[nodiscard]] std::expected<DedupDecision, DurableEffectBackendError>
project_seal(const SealOutcome &outcome, const DurableEffectIntent &intent) noexcept {
    return std::visit(
        Overloaded{
            [](const SealSealed &sealed)
                -> std::expected<DedupDecision, DurableEffectBackendError> {
                return DedupDecision{EffectNew{sealed.scope}};
            },
            [](SealDiverged) -> std::expected<DedupDecision, DurableEffectBackendError> {
                return DedupDecision{EffectDiverged{}};
            },
            [&](const SealExisting &existing)
                -> std::expected<DedupDecision, DurableEffectBackendError> {
                if (existing.registration.authority != intent.authority() ||
                    existing.registration.coordinate != intent.coordinate()) {
                    return std::unexpected(DurableEffectBackendError::StorageCorrupt);
                }
                return project_begin(existing.registration.terminal);
            },
        },
        outcome);
}

using CompletionError = std::variant<RegistrationConflict, DurableEffectBackendError>;
using TerminalRecordResult =
    std::expected<ResultHandle,
                  std::variant<EffectTerminalError, DurableEffectBackendError>>;

// Map the storage CAS outcome onto the authority's domain error: the store's
// Unknown/AlreadyTerminal conflicts are the caller-facing
// UnknownToken/TerminalAlreadyRecorded; storage errors pass through.
[[nodiscard]] TerminalRecordResult
map_completion_error(CompletionError error) noexcept {
    return std::visit(
        Overloaded{
            [](RegistrationConflict conflict) -> TerminalRecordResult {
                switch (conflict) {
                case RegistrationConflict::Unknown:
                    return std::unexpected(std::variant<EffectTerminalError,
                                                        DurableEffectBackendError>{
                        EffectTerminalError::UnknownToken});
                case RegistrationConflict::AlreadyTerminal:
                    return std::unexpected(std::variant<EffectTerminalError,
                                                        DurableEffectBackendError>{
                        EffectTerminalError::TerminalAlreadyRecorded});
                }
                return std::unexpected(
                    std::variant<EffectTerminalError, DurableEffectBackendError>{
                        EffectTerminalError::UnknownToken});
            },
            [](DurableEffectBackendError storage_error) -> TerminalRecordResult {
                return std::unexpected(
                    std::variant<EffectTerminalError, DurableEffectBackendError>{storage_error});
            },
        },
        error);
}

} // namespace

// =============================================================================
// In-memory backend
// =============================================================================

struct InMemoryDurableEffectBackend::Impl {
    // Flat append-only stores (Principle 3). Registrations are never removed or
    // reordered; the token index holds flat indices.
    std::vector<SealedEffectRegistration> registrations;
    std::vector<std::vector<std::uint8_t>> payloads;

    // Token bytes have a total (lexicographic) order through std::array, so a
    // std::map key needs no custom hash.
    std::map<std::array<std::uint8_t, 32>, std::size_t> by_token;

    // Process-local linearizability. seal_or_load and complete_pending run the
    // ENTIRE decision/state transition under this one mutex, so the begin
    // collision check and the Pending insert share a single linearization
    // point.
    mutable std::mutex mutex;
};

InMemoryDurableEffectBackend::InMemoryDurableEffectBackend()
    : impl_(std::make_unique<Impl>()) {}
InMemoryDurableEffectBackend::~InMemoryDurableEffectBackend() = default;
InMemoryDurableEffectBackend::InMemoryDurableEffectBackend(
    InMemoryDurableEffectBackend &&) noexcept = default;
InMemoryDurableEffectBackend &
InMemoryDurableEffectBackend::operator=(InMemoryDurableEffectBackend &&) noexcept = default;

std::optional<SealedEffectRegistration>
InMemoryDurableEffectBackend::find_registration(const IdempotencyToken &token) const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->by_token.find(token.bytes);
    if (found == impl_->by_token.end()) {
        return std::nullopt;
    }
    return impl_->registrations[found->second];
}

std::expected<SealOutcome, DurableEffectBackendError>
InMemoryDurableEffectBackend::seal_or_load(SealedEffectRegistration candidate) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);

    // (1) The token already owns a row (an identical racing begin, or a repeat
    // begin): replay the sealed lifecycle, no mutation.
    if (const auto found = impl_->by_token.find(candidate.token.bytes);
        found != impl_->by_token.end()) {
        return SealOutcome{SealExisting{impl_->registrations[found->second]}};
    }

    // (2) Scan the digest-EXCLUDING call site at the SAME linearization point as
    // the insert. A same-authority row with a different param digest fails
    // closed (seal nothing); foreign-authority rows mark an isolated New.
    bool foreign_authority_present = false;
    for (const SealedEffectRegistration &row : impl_->registrations) {
        if (!same_call_site(row.coordinate, candidate.coordinate)) {
            continue;
        }
        if (row.authority != candidate.authority) {
            foreign_authority_present = true;
            continue;
        }
        if (row.coordinate.param_digest != candidate.coordinate.param_digest) {
            return SealOutcome{SealDiverged{}};
        }
        // Same authority + identical full coordinate MUST own the identical
        // token (the slice-1 token is a deterministic digest of exactly those
        // fields), and step (1) found no such row: the token index is
        // internally inconsistent.
        return std::unexpected(DurableEffectBackendError::StorageCorrupt);
    }

    // (3) Seal Pending. No other thread can have observed the pre-insert call
    // site within this same critical section, so exactly one thread reaches
    // this append per same-authority coordinate.
    impl_->by_token.emplace(candidate.token.bytes, impl_->registrations.size());
    impl_->registrations.push_back(std::move(candidate));
    return SealOutcome{SealSealed{
        foreign_authority_present ? NewEffectScope::AuthorityIsolated
                                  : NewEffectScope::FreshCoordinate}};
}

std::expected<ResultHandle,
              std::variant<RegistrationConflict, DurableEffectBackendError>>
InMemoryDurableEffectBackend::complete_pending(const IdempotencyToken &token, bool succeeded,
                                               std::span<const std::uint8_t> typed_payload) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->by_token.find(token.bytes);
    if (found == impl_->by_token.end()) {
        return std::unexpected(std::variant<RegistrationConflict, DurableEffectBackendError>{
            RegistrationConflict::Unknown});
    }
    SealedEffectRegistration &row = impl_->registrations[found->second];
    if (!std::holds_alternative<SealedPending>(row.terminal)) {
        return std::unexpected(std::variant<RegistrationConflict, DurableEffectBackendError>{
            RegistrationConflict::AlreadyTerminal});
    }

    // Append the recorded payload first; only then CAS the row terminal, so a
    // handle handed out always names a stored payload.
    const auto handle_value = static_cast<std::uint64_t>(impl_->payloads.size());
    impl_->payloads.emplace_back(typed_payload.begin(), typed_payload.end());
    const ResultHandle handle{handle_value};
    row.terminal = succeeded ? SealedTerminal{SealedSucceeded{handle}}
                             : SealedTerminal{SealedFailed{handle}};
    return handle;
}

std::expected<std::vector<PendingRecoveryEntry>, DurableEffectBackendError>
InMemoryDurableEffectBackend::list_pending(
    const CheckpointNamespace &checkpoint_namespace) const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<PendingRecoveryEntry> pending;
    for (const SealedEffectRegistration &row : impl_->registrations) {
        if (row.coordinate.checkpoint_namespace != checkpoint_namespace) {
            continue;
        }
        if (std::holds_alternative<SealedPending>(row.terminal)) {
            pending.push_back(PendingRecoveryEntry{
                .authority = row.authority,
                .token = row.token,
                .coordinate = row.coordinate,
            });
        }
    }
    return pending;
}

std::expected<std::vector<std::uint8_t>,
              std::variant<PayloadReadConflict, DurableEffectBackendError>>
InMemoryDurableEffectBackend::read_payload(ResultHandle handle) const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    if (handle.value == ResultHandle::kInvalid ||
        handle.value >= impl_->payloads.size()) {
        // A stale / never-minted handle is a lookup MISS, not an integrity
        // failure: the flat append-only store has no torn/truncated state.
        return std::unexpected(
            std::variant<PayloadReadConflict, DurableEffectBackendError>{
                PayloadReadConflict::UnknownHandle});
    }
    return impl_->payloads[handle.value];
}

std::shared_ptr<IDurableEffectBackend> make_in_memory_durable_effect_backend() {
    return std::make_shared<InMemoryDurableEffectBackend>();
}

// =============================================================================
// Decision layer
// =============================================================================

std::expected<DedupDecision, DurableEffectBackendError>
DurableEffectAuthority::begin_effect(const DurableEffectIntent &intent) const {
    // One atomic backend transaction decides the whole begin: token lookup +
    // digest-excluding call-site collision scan + Pending seal share a single
    // linearization point, so two racing begins at one same-authority
    // coordinate can never both seal (no check-then-act window).
    SealedEffectRegistration candidate{
        .authority = intent.authority(),
        .token = intent.token(),
        .coordinate = intent.coordinate(),
        .terminal = SealedTerminal{SealedPending{}},
    };
    auto outcome = backend_->seal_or_load(std::move(candidate));
    if (!outcome.has_value()) {
        return std::unexpected(outcome.error());
    }
    return project_seal(*outcome, intent);
}

std::expected<ResultHandle, std::variant<EffectTerminalError, DurableEffectBackendError>>
DurableEffectAuthority::record_result(const IdempotencyToken &token,
                                      std::span<const std::uint8_t> typed_result) const {
    auto recorded = backend_->complete_pending(token, /*succeeded=*/true, typed_result);
    if (recorded.has_value()) {
        return *recorded;
    }
    return map_completion_error(recorded.error());
}

std::expected<ResultHandle, std::variant<EffectTerminalError, DurableEffectBackendError>>
DurableEffectAuthority::record_failure(const IdempotencyToken &token,
                                       std::span<const std::uint8_t> typed_failure) const {
    auto recorded = backend_->complete_pending(token, /*succeeded=*/false, typed_failure);
    if (recorded.has_value()) {
        return *recorded;
    }
    return map_completion_error(recorded.error());
}

std::expected<EffectResolution, DurableEffectBackendError>
DurableEffectAuthority::resolve(const IdempotencyToken &token) const {
    auto row = backend_->find_registration(token);
    if (!row.has_value()) {
        return EffectUnknown{};
    }
    return project_resolution(row->terminal);
}

std::expected<std::vector<PendingRecoveryEntry>, DurableEffectBackendError>
DurableEffectAuthority::recover(const CheckpointNamespace &checkpoint_namespace) const {
    return backend_->list_pending(checkpoint_namespace);
}

std::expected<std::vector<std::uint8_t>,
              std::variant<ResultReadError, DurableEffectBackendError>>
DurableEffectAuthority::read_result(ResultHandle handle) const {
    auto bytes = backend_->read_payload(handle);
    if (bytes.has_value()) {
        return *bytes;
    }
    // A lookup miss is the authority-local UnknownHandle; a genuine storage
    // integrity failure (torn/truncated payload, internally inconsistent
    // sealed state) passes through verbatim so the host fails closed into
    // reconciliation instead of treating corruption as "never recorded".
    return std::visit(
        Overloaded{
            [](PayloadReadConflict conflict)
                -> std::expected<std::vector<std::uint8_t>,
                                 std::variant<ResultReadError, DurableEffectBackendError>> {
                switch (conflict) {
                case PayloadReadConflict::UnknownHandle:
                    return std::unexpected(
                        std::variant<ResultReadError, DurableEffectBackendError>{
                            ResultReadError::UnknownHandle});
                }
                return std::unexpected(
                    std::variant<ResultReadError, DurableEffectBackendError>{
                        ResultReadError::UnknownHandle});
            },
            [](DurableEffectBackendError storage_error)
                -> std::expected<std::vector<std::uint8_t>,
                                 std::variant<ResultReadError, DurableEffectBackendError>> {
                return std::unexpected(
                    std::variant<ResultReadError, DurableEffectBackendError>{storage_error});
            },
        },
        bytes.error());
}

DurableEffectAuthority make_in_memory_durable_effect_authority() {
    return DurableEffectAuthority{make_in_memory_durable_effect_backend()};
}

} // namespace ahfl::runtime::durable_effect_authority
