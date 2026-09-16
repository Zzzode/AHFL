// RFC 0026 KR6.5 E4-B2-D2b-3 tests for the host-independent durable-effect
// dedupe / recover / result authority and its in-memory backend.
// Hand-rolled check()/main(), mirroring the D2b-1/D2b-2 engine tests.
//
// Coverage (per slice spec):
//  (a) begin on a fresh token -> New(FreshCoordinate);
//  (b) begin again with the identical coordinate before a result ->
//      ReplayPending, never a second New (idempotent begin);
//  (c) after record_result, begin -> ReplaySucceeded(handle) and resolve
//      returns Succeeded(same handle); the recorded bytes round-trip only via
//      read_result(handle);
//  (d) same authority/namespace but a different param digest -> Diverged,
//      fail-closed: no second Pending row is sealed, nothing is replayable;
//  (e) authority-id mismatch on an otherwise identical coordinate -> an
//      isolated New(AuthorityIsolated) verdict, never a cross-authority replay;
//  (f) recover() on a FRESH authority instance seeded from the same backend
//      (simulated crash) lists exactly the Pending tokens and no terminal ones;
//  (g) result bytes never appear in any decision/error channel -- they leave
//      the authority only through read_result(ResultHandle);
//  (h) every outcome is a closed std::variant visited with Overloaded, and
//      every failure is a typed enum (no bool/string error channel).

#include "runtime/engine/durable_effect_authority.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/durable_effect_intent.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using ahfl::Overloaded;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::durable_effect_authority::DedupDecision;
using ahfl::runtime::durable_effect_authority::DurableEffectAuthority;
using ahfl::runtime::durable_effect_authority::DurableEffectBackendError;
using ahfl::runtime::durable_effect_authority::DurableEffectGuarantee;
using ahfl::runtime::durable_effect_authority::EffectDiverged;
using ahfl::runtime::durable_effect_authority::EffectFailed;
using ahfl::runtime::durable_effect_authority::EffectNew;
using ahfl::runtime::durable_effect_authority::EffectPending;
using ahfl::runtime::durable_effect_authority::EffectReplayFailed;
using ahfl::runtime::durable_effect_authority::EffectReplayPending;
using ahfl::runtime::durable_effect_authority::EffectReplaySucceeded;
using ahfl::runtime::durable_effect_authority::EffectResolution;
using ahfl::runtime::durable_effect_authority::EffectSucceeded;
using ahfl::runtime::durable_effect_authority::EffectTerminalError;
using ahfl::runtime::durable_effect_authority::EffectUnknown;
using ahfl::runtime::durable_effect_authority::guarantee_provided_by_in_memory_backend;
using ahfl::runtime::durable_effect_authority::IDurableEffectBackend;
using ahfl::runtime::durable_effect_authority::InMemoryDurableEffectBackend;
using ahfl::runtime::durable_effect_authority::make_in_memory_durable_effect_backend;
using ahfl::runtime::durable_effect_authority::make_in_memory_durable_effect_authority;
using ahfl::runtime::durable_effect_authority::NewEffectScope;
using ahfl::runtime::durable_effect_authority::PayloadReadConflict;
using ahfl::runtime::durable_effect_authority::PendingRecoveryEntry;
using ahfl::runtime::durable_effect_authority::RegistrationConflict;
using ahfl::runtime::durable_effect_authority::ResultHandle;
using ahfl::runtime::durable_effect_authority::ResultReadError;
using ahfl::runtime::durable_effect_authority::SealedEffectRegistration;
using ahfl::runtime::durable_effect_authority::SealDiverged;
using ahfl::runtime::durable_effect_authority::SealExisting;
using ahfl::runtime::durable_effect_authority::SealOutcome;
using ahfl::runtime::durable_effect_authority::SealSealed;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyAuthorityId;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyToken;
using ahfl::runtime::durable_effect_intent::CheckpointNamespace;
using ahfl::runtime::durable_effect_intent::DurableEffectIntent;
using ahfl::runtime::durable_effect_intent::FrozenAuthorityNamespaceBuilder;
using ahfl::runtime::durable_effect_intent::IntentCoordinate;
using ahfl::runtime::payload_store::ResumeCheckpointId;
using ahfl::support::Sha256Digest;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

IdempotencyAuthorityId make_authority(std::uint8_t seed) {
    std::array<std::uint8_t, 16> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 7);
    }
    return IdempotencyAuthorityId{raw};
}

Sha256Digest make_digest(std::uint8_t seed) {
    Sha256Digest d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 13);
    }
    return d;
}

CheckpointNamespace make_namespace(std::uint32_t workflow, std::uint64_t checkpoint) {
    return CheckpointNamespace{
        .workflow = CoreWorkflowId{workflow},
        .checkpoint = ResumeCheckpointId{checkpoint},
    };
}

IntentCoordinate make_coordinate(CheckpointNamespace ns, std::uint8_t digest_seed = 0x61,
                                 std::uint32_t node = 0x21222324u,
                                 std::uint64_t ordinal = 0x3132333435363738ULL,
                                 std::uint32_t capability = 0x41424344u,
                                 std::uint64_t source_symbol = 0x5152535455565758ULL) {
    return IntentCoordinate{
        .checkpoint_namespace = ns,
        .node = CoreWorkflowNodeId{node},
        .ordinal = InvocationOrdinal{ordinal},
        .capability = CoreCapabilityId{capability},
        .source_symbol = source_symbol,
        .param_digest = make_digest(digest_seed),
    };
}

[[nodiscard]] DurableEffectIntent mint(const IdempotencyAuthorityId &authority,
                                       const CheckpointNamespace &ns,
                                       const IntentCoordinate &coordinate) {
    auto builder = FrozenAuthorityNamespaceBuilder{};
    static_cast<void>(builder.bind(authority, ns));
    return builder.mint(coordinate).value();
}

// Exhaustive decision classifier (Principle 4): a sixth DedupDecision arm makes
// this fail to compile.
struct DecisionSummary {
    int arm{};
    ResultHandle handle{ResultHandle::kInvalid};
    NewEffectScope new_scope{NewEffectScope::FreshCoordinate};
};

[[nodiscard]] DecisionSummary summarize(const DedupDecision &decision) {
    return std::visit(
        Overloaded{
            [](const EffectNew &d) -> DecisionSummary { return {0, {}, d.scope}; },
            [](EffectReplayPending) -> DecisionSummary { return {1}; },
            [](const EffectReplaySucceeded &d) -> DecisionSummary { return {2, d.handle}; },
            [](const EffectReplayFailed &d) -> DecisionSummary { return {3, d.handle}; },
            [](EffectDiverged) -> DecisionSummary { return {4}; },
        },
        decision);
}

[[nodiscard]] int resolution_arm(const EffectResolution &resolution) {
    return std::visit(
        Overloaded{
            [](EffectUnknown) -> int { return 0; },
            [](EffectPending) -> int { return 1; },
            [](const EffectSucceeded &) -> int { return 2; },
            [](const EffectFailed &) -> int { return 3; },
        },
        resolution);
}

[[nodiscard]] bool contains_token(const std::vector<PendingRecoveryEntry> &entries,
                                  const DurableEffectIntent &intent) {
    for (const PendingRecoveryEntry &entry : entries) {
        if (entry.token == intent.token()) {
            return true;
        }
    }
    return false;
}

// Distinctive secret payload: the bytes the authority must never echo except
// through the exact ResultHandle.
std::vector<std::uint8_t> secret_bytes(std::uint8_t seed) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < 48; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(seed + static_cast<int>(i) * 5));
    }
    return bytes;
}

// A pool of long-lived worker threads that runs one identical racing action
// per round behind barriers. Thread creation/destruction is hoisted out of the
// timed region (per-round churn otherwise serializes the begin calls and hides
// the TOCTOU windows the test exists to catch). Each round: main publishes the
// shared state and the action, releases the workers, waits for all of them to
// finish, then inspects the accumulated counters. The barrier phases establish
// a total order, so the action published before the release barrier is visible
// to every worker after it.
class RacingPool {
  public:
    explicit RacingPool(unsigned thread_count)
        : ready_(thread_count + 1), go_(thread_count + 1), done_(thread_count + 1) {
        workers_.reserve(thread_count);
        for (unsigned t = 0; t < thread_count; ++t) {
            workers_.emplace_back([this, t] { worker(t); });
        }
    }
    ~RacingPool() {
        stop_ = true;
        ready_.arrive_and_wait();
        for (std::thread &th : workers_) th.join();
    }
    RacingPool(const RacingPool &) = delete;
    RacingPool &operator=(const RacingPool &) = delete;

    void run_round(std::function<void(unsigned)> action) {
        action_ = std::move(action);
        ready_.arrive_and_wait();
        go_.arrive_and_wait();
        done_.arrive_and_wait();
    }

  private:
    void worker(unsigned thread_index) {
        for (;;) {
            ready_.arrive_and_wait();
            if (stop_) {
                break;
            }
            go_.arrive_and_wait();
            action_(thread_index);
            done_.arrive_and_wait();
        }
    }

    std::vector<std::thread> workers_;
    std::barrier<> ready_;
    std::barrier<> go_;
    std::barrier<> done_;
    bool stop_{false};
    std::function<void(unsigned)> action_;
};

// A backend that delegates everything to the in-memory backend but reports a
// genuine storage-integrity failure (a torn/truncated payload) on read. The
// authority must pass that through as DurableEffectBackendError::StorageCorrupt,
// never collapse it into the benign UnknownHandle lookup miss.
class CorruptReadBackend final : public IDurableEffectBackend {
  public:
    explicit CorruptReadBackend(std::shared_ptr<IDurableEffectBackend> inner)
        : inner_(std::move(inner)) {}

    [[nodiscard]] std::optional<SealedEffectRegistration>
    find_registration(const IdempotencyToken &token) const override {
        return inner_->find_registration(token);
    }
    [[nodiscard]] std::expected<SealOutcome, DurableEffectBackendError>
    seal_or_load(SealedEffectRegistration candidate) override {
        return inner_->seal_or_load(std::move(candidate));
    }
    [[nodiscard]] std::expected<ResultHandle,
                                std::variant<RegistrationConflict,
                                             DurableEffectBackendError>>
    complete_pending(const IdempotencyToken &token, bool succeeded,
                     std::span<const std::uint8_t> typed_payload) override {
        return inner_->complete_pending(token, succeeded, typed_payload);
    }
    [[nodiscard]] std::expected<std::vector<PendingRecoveryEntry>, DurableEffectBackendError>
    list_pending(const CheckpointNamespace &checkpoint_namespace) const override {
        return inner_->list_pending(checkpoint_namespace);
    }
    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                std::variant<PayloadReadConflict,
                                             DurableEffectBackendError>>
    read_payload(ResultHandle) const override {
        return std::unexpected(
            std::variant<PayloadReadConflict, DurableEffectBackendError>{
                DurableEffectBackendError::StorageCorrupt});
    }

  private:
    std::shared_ptr<IDurableEffectBackend> inner_;
};

} // namespace

int main() {
    // ---- (h) closed sets, pinned at compile time ---------------------------
    static_assert(std::variant_size_v<DedupDecision> == 5,
                  "DedupDecision must be exactly New/ReplayPending/"
                  "ReplaySucceeded/ReplayFailed/Diverged");
    static_assert(std::variant_size_v<EffectResolution> == 4,
                  "EffectResolution must be exactly Unknown/Pending/Succeeded/Failed");
    // The atomic backend seam outcome is a closed set: a fourth seal outcome
    // (or a split back into separate find/scan/insert calls) must fail here.
    static_assert(std::variant_size_v<SealOutcome> == 3,
                  "SealOutcome must be exactly SealSealed/SealDiverged/SealExisting");
    static_assert(std::is_same_v<std::variant_alternative_t<0, SealOutcome>, SealSealed>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, SealOutcome>, SealDiverged>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, SealOutcome>, SealExisting>);
    static_assert(std::is_same_v<std::variant_alternative_t<0, DedupDecision>, EffectNew>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<1, DedupDecision>, EffectReplayPending>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<2, DedupDecision>, EffectReplaySucceeded>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<3, DedupDecision>, EffectReplayFailed>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, DedupDecision>, EffectDiverged>);
    static_assert(std::is_same_v<std::variant_alternative_t<0, EffectResolution>, EffectUnknown>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, EffectResolution>, EffectPending>);
    static_assert(
        std::is_same_v<std::variant_alternative_t<2, EffectResolution>, EffectSucceeded>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, EffectResolution>, EffectFailed>);

    // (g) structurally, no decision arm can carry result bytes or a string:
    // the conflict arms are empty, the replay arms carry only a ResultHandle.
    static_assert(std::is_empty_v<EffectReplayPending>,
                  "ReplayPending must carry no result bytes");
    static_assert(std::is_empty_v<EffectDiverged>,
                  "Diverged must carry no result bytes (fail closed)");
    static_assert(sizeof(EffectReplaySucceeded) == sizeof(ResultHandle),
                  "ReplaySucceeded names the result only by index handle");
    static_assert(sizeof(EffectReplayFailed) == sizeof(ResultHandle),
                  "ReplayFailed names the failure only by index handle");
    static_assert(!std::is_constructible_v<EffectDiverged, std::vector<std::uint8_t>>,
                  "Diverged cannot be constructed from result bytes");
    static_assert(!std::is_constructible_v<EffectReplaySucceeded,
                                           std::vector<std::uint8_t>>,
                  "ReplaySucceeded cannot be constructed from result bytes");

    // The typed guarantee table.
    static_assert(
        guarantee_provided_by_in_memory_backend(
            DurableEffectGuarantee::IdentityAndCollisionDedup));
    static_assert(
        guarantee_provided_by_in_memory_backend(
            DurableEffectGuarantee::SameProcessRecovery));
    static_assert(
        !guarantee_provided_by_in_memory_backend(
            DurableEffectGuarantee::CrossProcessDurability),
        "in-memory backend must not claim crash durability");
    static_assert(
        !guarantee_provided_by_in_memory_backend(DurableEffectGuarantee::Authenticity),
        "D2b identity authority is never an authenticity authority");
    static_assert(
        !guarantee_provided_by_in_memory_backend(DurableEffectGuarantee::Rollback),
        "D2b identity authority is never a rollback authority");

    const IdempotencyAuthorityId authority_a = make_authority(0x11);
    const IdempotencyAuthorityId authority_b = make_authority(0x22);
    const CheckpointNamespace ns1 = make_namespace(7, 42);
    const CheckpointNamespace ns2 = make_namespace(7, 43);

    // ---- (a) fresh token -> New --------------------------------------------
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        const DurableEffectIntent intent = mint(authority_a, ns1, make_coordinate(ns1));

        const auto decision = authority.begin_effect(intent);
        check(decision.has_value(), "fresh_begin_has_no_storage_error");
        check(std::holds_alternative<EffectNew>(*decision), "fresh_begin_is_New");
        check(summarize(*decision).new_scope == NewEffectScope::FreshCoordinate,
              "fresh_coordinate_begin_is_FreshCoordinate");

        // resolve() on an un-begun token is Unknown without sealing anything.
        const DurableEffectIntent untouched =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/77));
        const auto unknown = authority.resolve(untouched.token());
        check(unknown.has_value() && std::holds_alternative<EffectUnknown>(*unknown),
              "resolve_unknown_token_is_Unknown");
    }

    // ---- (b) identical re-begin before a result -> ReplayPending -----------
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        const DurableEffectIntent intent = mint(authority_a, ns1, make_coordinate(ns1));

        const auto first = authority.begin_effect(intent);
        check(summarize(*first).arm == 0, "first_begin_New");

        // A simulated retry loop (only its external attempt counter changes,
        // which by construction never enters the coordinate/token).
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            static_cast<void>(attempt);
            const auto again = authority.begin_effect(intent);
            check(again.has_value(), "retry_begin_has_no_storage_error");
            check(std::holds_alternative<EffectReplayPending>(*again),
                  "retry_begin_is_ReplayPending_never_second_New");
        }

        const auto resolution = authority.resolve(intent.token());
        check(resolution.has_value() && std::holds_alternative<EffectPending>(*resolution),
              "resolve_pending_token_is_Pending");
    }

    // ---- (c) record_result -> ReplaySucceeded + Succeeded(handle) ----------
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        const DurableEffectIntent intent = mint(authority_a, ns1, make_coordinate(ns1));
        static_cast<void>(authority.begin_effect(intent));

        const std::vector<std::uint8_t> result = secret_bytes(0xA0);
        const auto handle = authority.record_result(intent.token(), result);
        check(handle.has_value(), "record_result_succeeds_for_pending");
        check(handle->value != ResultHandle::kInvalid, "recorded_handle_is_valid_index");

        const auto resolution = authority.resolve(intent.token());
        check(resolution.has_value() && std::holds_alternative<EffectSucceeded>(*resolution),
              "resolve_after_result_is_Succeeded");
        const ResultHandle resolved_handle =
            std::get<EffectSucceeded>(*resolution).handle;
        check(resolved_handle == *handle, "resolve_returns_recorded_handle");

        const auto replay = authority.begin_effect(intent);
        check(std::holds_alternative<EffectReplaySucceeded>(*replay),
              "begin_after_result_is_ReplaySucceeded");
        check(std::get<EffectReplaySucceeded>(*replay).handle == *handle,
              "replay_succeeded_carries_same_handle");

        // The terminal record is immutable: a second record is a typed
        // rejection, never an overwrite.
        const auto second = authority.record_result(intent.token(), secret_bytes(0xB0));
        check(!second.has_value() &&
                  std::holds_alternative<EffectTerminalError>(second.error()) &&
                  std::get<EffectTerminalError>(second.error()) ==
                      EffectTerminalError::TerminalAlreadyRecorded,
              "second_record_is_TerminalAlreadyRecorded");

        // record on a token that was never begun is UnknownToken.
        const DurableEffectIntent never_begun =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/88));
        const auto orphan = authority.record_result(never_begun.token(), result);
        check(!orphan.has_value() &&
                  std::get<EffectTerminalError>(orphan.error()) ==
                      EffectTerminalError::UnknownToken,
              "record_without_begin_is_UnknownToken");
    }

    // ---- (d) different param digest, same authority/namespace -> Diverged --
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        const IntentCoordinate coord_v1 = make_coordinate(ns1, /*digest=*/0x61);
        const IntentCoordinate coord_v2 = make_coordinate(ns1, /*digest=*/0x62);
        const DurableEffectIntent intent_v1 = mint(authority_a, ns1, coord_v1);
        const DurableEffectIntent intent_v2 = mint(authority_a, ns1, coord_v2);
        check(intent_v1.token() != intent_v2.token(),
              "distinct_param_digests_mint_distinct_tokens");

        const auto first = authority.begin_effect(intent_v1);
        check(std::holds_alternative<EffectNew>(*first), "v1_begin_New");

        const auto diverged = authority.begin_effect(intent_v2);
        check(diverged.has_value(), "divergence_is_a_decision_not_a_storage_error");
        check(std::holds_alternative<EffectDiverged>(*diverged),
              "same_call_site_different_param_digest_is_Diverged");

        // Fail-closed: the diverged token sealed no Pending row, is not
        // recoverable, and re-trying it stays Diverged.
        check(std::holds_alternative<EffectUnknown>(*authority.resolve(intent_v2.token())),
              "diverged_token_seals_no_pending_row");
        const auto diverged_again = authority.begin_effect(intent_v2);
        check(std::holds_alternative<EffectDiverged>(*diverged_again),
              "diverged_retry_stays_Diverged");
        const auto recovered = authority.recover(ns1);
        check(recovered.has_value() && !contains_token(*recovered, intent_v2),
              "diverged_token_is_not_recoverable");
        // The original effect is untouched and still replays Pending.
        check(std::holds_alternative<EffectReplayPending>(*authority.begin_effect(intent_v1)),
              "divergence_leaves_original_effect_pending");
    }

    // ---- (e) authority mismatch -> isolated New, never cross-replay --------
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        // Identical call coordinate body incl. param digest, different
        // authority identity -> different tokens by construction.
        const DurableEffectIntent intent_a = mint(authority_a, ns1, make_coordinate(ns1));
        const DurableEffectIntent intent_b = mint(authority_b, ns1, make_coordinate(ns1));
        check(intent_a.token() != intent_b.token(),
              "authority_swap_isolates_tokens");

        const auto begin_a = authority.begin_effect(intent_a);
        check(summarize(*begin_a).arm == 0 &&
                  summarize(*begin_a).new_scope == NewEffectScope::FreshCoordinate,
              "first_authority_FreshCoordinate");

        const auto begin_b = authority.begin_effect(intent_b);
        check(std::holds_alternative<EffectNew>(*begin_b) &&
                  std::get<EffectNew>(*begin_b).scope ==
                      NewEffectScope::AuthorityIsolated,
              "foreign_authority_same_site_is_isolated_New");

        // Each authority replays only its own Pending row.
        check(std::holds_alternative<EffectReplayPending>(*authority.begin_effect(intent_a)),
              "authority_a_replays_own_pending");
        check(std::holds_alternative<EffectReplayPending>(*authority.begin_effect(intent_b)),
              "authority_b_replays_own_pending");

        // Completing A never surfaces through B.
        const auto handle_a = authority.record_result(intent_a.token(), secret_bytes(0xC0));
        check(handle_a.has_value(), "authority_a_records_result");
        check(std::holds_alternative<EffectReplaySucceeded>(*authority.begin_effect(intent_a)),
              "authority A sees its own ReplaySucceeded");
        check(std::holds_alternative<EffectReplayPending>(*authority.begin_effect(intent_b)),
              "authority B stays Pending: no cross-authority replay");

        // A genuinely different checkpoint namespace under the same authority is
        // a fresh coordinate, not an isolation verdict or divergence.
        const DurableEffectIntent other_ns = mint(authority_a, ns2, make_coordinate(ns2));
        const auto begin_other = authority.begin_effect(other_ns);
        check(std::holds_alternative<EffectNew>(*begin_other) &&
                  std::get<EffectNew>(*begin_other).scope ==
                      NewEffectScope::FreshCoordinate,
              "different_namespace_same_authority_is_FreshCoordinate");
    }

    // ---- (f) reopen over the same backend: recover lists exactly Pending ---
    {
        std::shared_ptr<IDurableEffectBackend> backend =
            make_in_memory_durable_effect_backend();

        const DurableEffectIntent p1 =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/101));
        const DurableEffectIntent p2 =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/102));
        const DurableEffectIntent s1 =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/103));
        const DurableEffectIntent f1 =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/104));
        const DurableEffectIntent other_ns_pending =
            mint(authority_a, ns2, make_coordinate(ns2, 0x61, /*node=*/101));
        // A Pending effect of a SECOND authority in the SAME namespace must also
        // be listed: reconciliation needs the authority to re-drive it.
        const DurableEffectIntent foreign_pending =
            mint(authority_b, ns1, make_coordinate(ns1, 0x61, /*node=*/201));

        {
            DurableEffectAuthority before{backend};
            static_cast<void>(before.begin_effect(p1));
            static_cast<void>(before.begin_effect(p2));
            static_cast<void>(before.begin_effect(s1));
            static_cast<void>(before.begin_effect(f1));
            static_cast<void>(before.begin_effect(other_ns_pending));
            static_cast<void>(before.begin_effect(foreign_pending));
            static_cast<void>(before.record_result(s1.token(), secret_bytes(0xD0)));
            static_cast<void>(before.record_failure(f1.token(), secret_bytes(0xD1)));
        }
        // Simulated crash: the original authority is destroyed; a FRESH
        // authority instance is seeded from the SAME live backend.
        DurableEffectAuthority reopened{backend};

        const auto recovered = reopened.recover(ns1);
        check(recovered.has_value(), "reopen_recover_has_no_storage_error");
        check(recovered->size() == 3,
              "recover_lists_exactly_the_pending_rows_in_namespace");
        check(contains_token(*recovered, p1) && contains_token(*recovered, p2),
              "recover_lists_pending_effects");
        check(contains_token(*recovered, foreign_pending),
              "recover_lists_foreign_authority_pending_with_its_authority");
        check(!contains_token(*recovered, s1) && !contains_token(*recovered, f1),
              "recover_never_lists_succeeded_or_failed_rows");
        for (const PendingRecoveryEntry &entry : *recovered) {
            check(entry.coordinate.checkpoint_namespace == ns1,
                  "recover_entries_name_the_requested_namespace");
        }

        // The foreign entry carries the authority needed to reconcile it.
        bool foreign_found = false;
        for (const PendingRecoveryEntry &entry : *recovered) {
            if (entry.token == foreign_pending.token()) {
                foreign_found = entry.authority == authority_b;
            }
        }
        check(foreign_found, "recovered_foreign_entry_round_trips_authority_id");

        // The reopened authority still dedupes: the pending rows replay rather
        // than re-seal, and the terminal rows replay their terminals.
        check(std::holds_alternative<EffectReplayPending>(*reopened.begin_effect(p1)),
              "reopened_authority_replays_pending");
        check(std::holds_alternative<EffectReplaySucceeded>(*reopened.begin_effect(s1)),
              "reopened_authority_replays_succeeded");
        check(std::holds_alternative<EffectReplayFailed>(*reopened.begin_effect(f1)),
              "reopened_authority_replays_failed");

        // Other namespaces are isolated.
        const auto ns2_rows = reopened.recover(ns2);
        check(ns2_rows.has_value() && ns2_rows->size() == 1 &&
                  contains_token(*ns2_rows, other_ns_pending),
              "recover_is_scoped_to_the_requested_namespace");
        const auto fresh_ns = reopened.recover(make_namespace(999, 999));
        check(fresh_ns.has_value() && fresh_ns->empty(),
              "recover_on_unknown_namespace_is_empty");
    }

    // ---- (g)+(h) failure terminal + bytes only via the exact handle ---------
    {
        DurableEffectAuthority authority = make_in_memory_durable_effect_authority();
        const DurableEffectIntent ok_intent =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/301));
        const DurableEffectIntent bad_intent =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/302));
        static_cast<void>(authority.begin_effect(ok_intent));
        static_cast<void>(authority.begin_effect(bad_intent));

        const std::vector<std::uint8_t> ok_bytes = secret_bytes(0xE0);
        const std::vector<std::uint8_t> bad_bytes = secret_bytes(0xE1);
        const auto ok_handle = authority.record_result(ok_intent.token(), ok_bytes);
        const auto bad_handle = authority.record_failure(bad_intent.token(), bad_bytes);
        check(ok_handle.has_value() && bad_handle.has_value(),
              "result_and_failure_records_return_handles");
        check(*ok_handle != *bad_handle, "success_and_failure_get_distinct_handles");

        const auto ok_resolution = authority.resolve(ok_intent.token());
        const auto bad_resolution = authority.resolve(bad_intent.token());
        check(std::holds_alternative<EffectSucceeded>(*ok_resolution),
              "resolve_reports_Succeeded");
        check(std::holds_alternative<EffectFailed>(*bad_resolution),
              "resolve_reports_Failed");
        check(std::get<EffectFailed>(*bad_resolution).handle == *bad_handle,
              "failed_resolution_names_recorded_handle");

        const auto replay_failed = authority.begin_effect(bad_intent);
        check(std::holds_alternative<EffectReplayFailed>(*replay_failed) &&
                  std::get<EffectReplayFailed>(*replay_failed).handle == *bad_handle,
              "repeated_begin_after_failure_is_ReplayFailed");

        // Bytes leave the authority ONLY through read_result(exact handle):
        // each handle returns exactly its own recorded bytes.
        check(authority.read_result(*ok_handle).value() == ok_bytes,
              "success_bytes_round_trip_via_handle");
        check(authority.read_result(*bad_handle).value() == bad_bytes,
              "failure_bytes_round_trip_via_handle");
        check(authority.read_result(*ok_handle).value() != bad_bytes,
              "handles_are_isolated_result_indices");

        // A stale / invalid handle is a typed UnknownHandle, not a crash and
        // not bytes from a neighboring slot.
        const auto stale = authority.read_result(ResultHandle{ResultHandle::kInvalid});
        check(!stale.has_value() && std::holds_alternative<ResultReadError>(stale.error()) &&
                  std::get<ResultReadError>(stale.error()) == ResultReadError::UnknownHandle,
              "invalid_handle_is_typed_UnknownHandle");
        const auto future = authority.read_result(ResultHandle{0xDEADBEEFCAFEULL});
        check(!future.has_value() &&
                  std::get<ResultReadError>(future.error()) == ResultReadError::UnknownHandle,
              "out_of_range_handle_is_typed_UnknownHandle");

        // The decision/resolution/recovery channels expose no byte container:
        // exhaustively visit them and prove only handles flow out.
        const auto replay = authority.begin_effect(ok_intent);
        const DecisionSummary summary = summarize(*replay);
        check(summary.arm == 2 && summary.handle == *ok_handle,
              "replay_decision_carries_handle_only");
        check(resolution_arm(*authority.resolve(bad_intent.token())) == 3,
              "resolution_visitor_classifies_failed");
        const auto pending =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/303));
        static_cast<void>(authority.begin_effect(pending));
        const auto recovered = authority.recover(ns1).value();
        for (const PendingRecoveryEntry &entry : recovered) {
            static_cast<void>(entry);
            // PendingRecoveryEntry has no payload/string accessor by construction
            // (only authority/token/coordinate strong ids).
        }
        check(contains_token(recovered, pending), "pending_effect_is_recoverable");
    }

    // ---- (i) concurrency: the linearizable-begin guarantee under re-drive --
    //
    // begin/record are the hot path under concurrent replay controllers /
    // workers re-driving the same checkpoint. The guarantee only matters under
    // contention, so every invariant below is asserted over hundreds of rounds
    // with a persistent pool of barrier-released workers, never serially. The
    // pool keeps the worker threads alive across rounds: creating/joining a
    // thread per round serializes the begins and hides the exact TOCTOU windows
    // these tests pin.
    {
        constexpr unsigned kIdenticalThreads = 24;
        constexpr unsigned kDivergeThreads = 8;
        constexpr unsigned kRounds = 600;

        // (i.1) N-way IDENTICAL begin: exactly one New, every other call a
        // ReplayPending, zero storage errors. This is the former P1 window (a
        // racing identical begin was misreported as StorageCorrupt).
        {
            RacingPool pool(kIdenticalThreads);
            for (unsigned round = 0; round < kRounds; ++round) {
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority authority{backend};
                const DurableEffectIntent intent = mint(authority_a, ns1,
                                                        make_coordinate(ns1, 0x61,
                                                                        /*node=*/400 + round));
                std::atomic<unsigned> new_count{0};
                std::atomic<unsigned> pending_count{0};
                std::atomic<unsigned> error_count{0};
                pool.run_round([&](unsigned) {
                    auto decision = authority.begin_effect(intent);
                    if (!decision.has_value()) {
                        ++error_count;
                        return;
                    }
                    const int arm = summarize(*decision).arm;
                    if (arm == 0) {
                        ++new_count;
                    } else if (arm == 1) {
                        ++pending_count;
                    } else {
                        ++error_count;
                    }
                });
                check(new_count.load() == 1, "identical_race_exactly_one_New");
                check(pending_count.load() == kIdenticalThreads - 1,
                      "identical_race_all_losers_ReplayPending");
                check(error_count.load() == 0, "identical_race_zero_errors");
                const auto recovered = authority.recover(ns1);
                check(recovered.has_value() && contains_token(*recovered, intent) &&
                          recovered->size() == 1,
                      "identical_race_seals_exactly_one_pending_row");
            }
        }

        // (i.2) N-way record_result on one Pending token: exactly one winner,
        // every other record TerminalAlreadyRecorded, no backend error.
        {
            RacingPool pool(kIdenticalThreads);
            for (unsigned round = 0; round < kRounds; ++round) {
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority authority{backend};
                const DurableEffectIntent intent = mint(authority_a, ns1,
                                                        make_coordinate(ns1, 0x61,
                                                                        /*node=*/900 + round));
                static_cast<void>(authority.begin_effect(intent));
                std::atomic<unsigned> ok_count{0};
                std::atomic<unsigned> terminal_count{0};
                std::atomic<unsigned> other_count{0};
                pool.run_round([&](unsigned t) {
                    auto recorded =
                        authority.record_result(intent.token(), secret_bytes(0x40 + t));
                    if (recorded.has_value()) {
                        ++ok_count;
                    } else if (std::holds_alternative<EffectTerminalError>(recorded.error()) &&
                               std::get<EffectTerminalError>(recorded.error()) ==
                                   EffectTerminalError::TerminalAlreadyRecorded) {
                        ++terminal_count;
                    } else {
                        ++other_count;
                    }
                });
                check(ok_count.load() == 1, "concurrent_record_exactly_one_success");
                check(terminal_count.load() == kIdenticalThreads - 1,
                      "concurrent_record_losers_TerminalAlreadyRecorded");
                check(other_count.load() == 0, "concurrent_record_zero_other_errors");
            }
        }

        // (i.3) Same authority, SAME call site, DIFFERENT param digest under
        // contention: never two New. The winner seals one Pending row; losers
        // carrying the winner's digest ReplayPending and losers carrying the
        // other digest Diverge (sealing nothing). recover() names exactly one
        // Pending row at the coordinate. This is the former P0 TOCTOU window
        // (two distinct-token CAS inserts could both win).
        {
            RacingPool pool(kDivergeThreads);
            for (unsigned round = 0; round < kRounds; ++round) {
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority authority{backend};
                const DurableEffectIntent intent_v1 =
                    mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/1400 + round));
                const DurableEffectIntent intent_v2 =
                    mint(authority_a, ns1, make_coordinate(ns1, 0x62, /*node=*/1400 + round));
                std::atomic<unsigned> new_count{0};
                std::atomic<unsigned> replay_count{0};
                std::atomic<unsigned> diverged_count{0};
                std::atomic<unsigned> error_count{0};
                pool.run_round([&](unsigned t) {
                    const DurableEffectIntent &which = (t & 1U) ? intent_v2 : intent_v1;
                    auto decision = authority.begin_effect(which);
                    if (!decision.has_value()) {
                        ++error_count;
                        return;
                    }
                    switch (summarize(*decision).arm) {
                    case 0: ++new_count; break;
                    case 1: ++replay_count; break;
                    case 4: ++diverged_count; break;
                    default: ++error_count; break;
                    }
                });
                check(new_count.load() == 1, "divergent_race_never_two_New");
                check(error_count.load() == 0, "divergent_race_zero_errors");
                check(new_count.load() + replay_count.load() + diverged_count.load() ==
                          kDivergeThreads,
                      "divergent_race_every_caller_classified");
                const auto recovered = authority.recover(ns1);
                const bool one_pending =
                    recovered.has_value() && recovered->size() == 1 &&
                    (contains_token(*recovered, intent_v1) ||
                     contains_token(*recovered, intent_v2));
                check(one_pending, "divergent_race_exactly_one_pending_row_at_coordinate");
                // The loser-side digest seals nothing and is Unknown forever.
                const bool v1_pending =
                    recovered.has_value() && contains_token(*recovered, intent_v1);
                const DurableEffectIntent &loser_digest =
                    v1_pending ? intent_v2 : intent_v1;
                check(std::holds_alternative<EffectUnknown>(
                          *authority.resolve(loser_digest.token())),
                      "divergent_race_loser_digest_seals_nothing");
            }
        }

        // (i.4) Two FOREIGN authorities at one identical coordinate both begin:
        // each seals its own isolated New (one FreshCoordinate, one
        // AuthorityIsolated), and recover lists both rows with their own
        // authority -- never cross-authority dedup.
        {
            constexpr unsigned kIsoRounds = 600;
            RacingPool pool(2);
            for (unsigned round = 0; round < kIsoRounds; ++round) {
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority authority{backend};
                const DurableEffectIntent intent_a =
                    mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/1900 + round));
                const DurableEffectIntent intent_b =
                    mint(authority_b, ns1, make_coordinate(ns1, 0x61, /*node=*/1900 + round));
                std::atomic<unsigned> isolated_new{0};
                std::atomic<unsigned> other{0};
                pool.run_round([&](unsigned t) {
                    const DurableEffectIntent &which = t ? intent_b : intent_a;
                    auto decision = authority.begin_effect(which);
                    if (decision.has_value() &&
                        std::holds_alternative<EffectNew>(*decision)) {
                        ++isolated_new;
                    } else {
                        ++other;
                    }
                });
                check(isolated_new.load() == 2 && other.load() == 0,
                      "foreign_authority_race_both_get_isolated_New");
                const auto recovered = authority.recover(ns1);
                check(recovered.has_value() && recovered->size() == 2 &&
                          contains_token(*recovered, intent_a) &&
                          contains_token(*recovered, intent_b),
                      "foreign_authority_race_seals_two_isolated_rows");
            }
        }
    }

    // ---- (j) genuine StorageCorrupt on read passes through, not UnknownHandle
    {
        auto inner = make_in_memory_durable_effect_backend();
        auto corrupting = std::make_shared<CorruptReadBackend>(inner);
        DurableEffectAuthority authority{corrupting};
        const DurableEffectIntent intent =
            mint(authority_a, ns1, make_coordinate(ns1, 0x61, /*node=*/2300));
        static_cast<void>(authority.begin_effect(intent));
        const auto handle = authority.record_result(intent.token(), secret_bytes(0xF0));
        check(handle.has_value(), "corrupt_backend_still_seals_and_records");

        // The seam reports a real torn/truncated payload; the authority must
        // surface DurableEffectBackendError::StorageCorrupt rather than the
        // benign authority-local UnknownHandle lookup miss.
        const auto read = authority.read_result(*handle);
        check(!read.has_value() &&
                  std::holds_alternative<DurableEffectBackendError>(read.error()) &&
                  std::get<DurableEffectBackendError>(read.error()) ==
                      DurableEffectBackendError::StorageCorrupt,
              "storage_corrupt_on_read_passes_through_not_UnknownHandle");

        // The ordinary in-memory backend still classifies a stale handle as
        // the domain UnknownHandle (a miss, not corruption).
        DurableEffectAuthority plain = make_in_memory_durable_effect_authority();
        const auto miss = plain.read_result(ResultHandle{0xDEADBEEFCAFEULL});
        check(!miss.has_value() && std::holds_alternative<ResultReadError>(miss.error()) &&
                  std::get<ResultReadError>(miss.error()) == ResultReadError::UnknownHandle,
              "stale_handle_in_memory_is_UnknownHandle");
    }

    if (g_failures == 0) {
        std::cout << "all durable-effect authority tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " durable-effect authority test(s) failed\n";
    return 1;
}
