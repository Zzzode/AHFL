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

// The digest-EXCLUDING call-site location of one coordinate (see the header's
// EffectCallSiteLocation rationale).
[[nodiscard]] EffectCallSiteLocation call_site_of(const IntentCoordinate &coordinate) noexcept {
    return EffectCallSiteLocation{
        .checkpoint_namespace = coordinate.checkpoint_namespace,
        .node = coordinate.node,
        .ordinal = coordinate.ordinal,
        .capability = coordinate.capability,
        .source_symbol = coordinate.source_symbol,
    };
}

// Project sealed storage state onto the begin decision. The caller has already
// established that the looked-up row's token equals the in-flight intent's
// token.
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
    // reordered; the token/call-site indexes hold flat indices.
    std::vector<SealedEffectRegistration> registrations;
    std::vector<std::vector<std::uint8_t>> payloads;

    // Token bytes have a total (lexicographic) order through std::array, so a
    // std::map key needs no custom hash.
    std::map<std::array<std::uint8_t, 32>, std::size_t> by_token;

    // Process-local linearizability for the CAS operations: insert_pending and
    // complete_pending are atomic with respect to concurrent readers.
    mutable std::mutex mutex;

    [[nodiscard]] const SealedEffectRegistration *
    row_locked(const IdempotencyToken &token) const noexcept {
        const auto found = by_token.find(token.bytes);
        if (found == by_token.end()) {
            return nullptr;
        }
        return &registrations[found->second];
    }
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
    if (const SealedEffectRegistration *row = impl_->row_locked(token)) {
        return *row;
    }
    return std::nullopt;
}

std::expected<std::vector<SealedEffectRegistration>, DurableEffectBackendError>
InMemoryDurableEffectBackend::find_at_call_site(const EffectCallSiteLocation &location) const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<SealedEffectRegistration> hits;
    for (const SealedEffectRegistration &row : impl_->registrations) {
        if (call_site_of(row.coordinate) == location) {
            hits.push_back(row);
        }
    }
    return hits;
}

bool InMemoryDurableEffectBackend::insert_pending(SealedEffectRegistration registration) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto [it, inserted] = impl_->by_token.emplace(registration.token.bytes,
                                                        impl_->registrations.size());
    if (!inserted) {
        // A row already owns this token: the CAS loses with no mutation.
        return false;
    }
    impl_->registrations.push_back(std::move(registration));
    return true;
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

std::expected<std::vector<std::uint8_t>, DurableEffectBackendError>
InMemoryDurableEffectBackend::read_payload(ResultHandle handle) const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    if (handle.value == ResultHandle::kInvalid ||
        handle.value >= impl_->payloads.size()) {
        return std::unexpected(DurableEffectBackendError::StorageCorrupt);
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
    const IdempotencyToken token = intent.token();

    // Fast path: the token is already sealed. Project the sealed lifecycle; a
    // row whose authority/coordinate disagree with the token-owning intent is
    // corrupt backend state (the slice-1 token is a deterministic digest of
    // exactly those fields).
    if (auto existing = backend_->find_registration(token); existing.has_value()) {
        if (existing->authority != intent.authority() ||
            existing->coordinate != intent.coordinate()) {
            return std::unexpected(DurableEffectBackendError::StorageCorrupt);
        }
        return project_begin(existing->terminal);
    }

    // Slow path: inspect every sealed row at the digest-EXCLUDING call site.
    // Same-authority rows either diverge in their param digest (fail closed) or
    // are impossible (equal digest implies the token already existed); rows of
    // another authority make this an isolated New.
    const EffectCallSiteLocation location = call_site_of(intent.coordinate());
    auto siblings = backend_->find_at_call_site(location);
    if (!siblings.has_value()) {
        return std::unexpected(siblings.error());
    }

    bool foreign_authority_present = false;
    for (const SealedEffectRegistration &row : *siblings) {
        if (row.authority != intent.authority()) {
            foreign_authority_present = true;
            continue;
        }
        if (row.coordinate.param_digest != intent.coordinate().param_digest) {
            // Same authority, same checkpoint namespace, same node / ordinal /
            // capability / source symbol, DIFFERENT canonical Param bytes:
            // never silently issue a second effect and never surface the prior
            // effect's bytes.
            return EffectDiverged{};
        }
        // Same authority + same param digest at this call site MUST already
        // have sealed the identical token (find_registration above); missing it
        // means the backend's token index is corrupt.
        return std::unexpected(DurableEffectBackendError::StorageCorrupt);
    }

    // CAS-seal Pending. A losing race (another thread sealed the token between
    // the find and the insert) re-reads and projects rather than double-New.
    SealedEffectRegistration registration{
        .authority = intent.authority(),
        .token = token,
        .coordinate = intent.coordinate(),
        .terminal = SealedTerminal{SealedPending{}},
    };
    if (!backend_->insert_pending(std::move(registration))) {
        auto raced = backend_->find_registration(token);
        if (!raced.has_value()) {
            return std::unexpected(DurableEffectBackendError::StorageCorrupt);
        }
        if (raced->authority != intent.authority() || raced->coordinate != intent.coordinate()) {
            return std::unexpected(DurableEffectBackendError::StorageCorrupt);
        }
        return project_begin(raced->terminal);
    }

    return EffectNew{foreign_authority_present ? NewEffectScope::AuthorityIsolated
                                               : NewEffectScope::FreshCoordinate};
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
    switch (bytes.error()) {
    case DurableEffectBackendError::StorageCorrupt:
        return std::unexpected(
            std::variant<ResultReadError, DurableEffectBackendError>{ResultReadError::UnknownHandle});
    case DurableEffectBackendError::StorageUnavailable:
        return std::unexpected(std::variant<ResultReadError, DurableEffectBackendError>{
            DurableEffectBackendError::StorageUnavailable});
    }
    return std::unexpected(
        std::variant<ResultReadError, DurableEffectBackendError>{ResultReadError::UnknownHandle});
}

DurableEffectAuthority make_in_memory_durable_effect_authority() {
    return DurableEffectAuthority{make_in_memory_durable_effect_backend()};
}

} // namespace ahfl::runtime::durable_effect_authority
