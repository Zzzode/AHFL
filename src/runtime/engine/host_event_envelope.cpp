// RFC 0026 KR6.5 E4-B2-E-1: pure host-independent import-observation join /
// exact-order + no-reinvoke verdict engine. See host_event_envelope.hpp for the
// seam contract and the FOUNDATION honesty boundary (no VM, no host, no
// persistence, no authenticated wire envelope).

#include "runtime/engine/host_event_envelope.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ahfl/runtime/ahfl_host.h" // AHFL_CAP_OK (ABI SSOT)

namespace ahfl::runtime::host_event_envelope {
namespace {

using core_wasm_node_events::NodeEventRecord;
using core_wasm_resume::CoreWasmResumeRecord;
using core_wasm_resume::InvocationOrdinal;
using core_wasm_resume::NodeKind;
using core_wasm_resume::ResumeMemoEntry;
using core_wasm_resume::ResumeNode;
using core_wasm_resume::ResumePendingEntry;
using core_wasm_resume::ResumeState;
using core_wasm_schema_module::ManifestCallSiteIndex;
using core_wasm_schema_module::ManifestNodeIndex;
using core_wasm_schema_module::VerifiedCoreWasmSchemaModule;

constexpr std::uint8_t kIdentityCallCount = 0;
constexpr std::uint8_t kCapabilityCallCount = 1;

[[nodiscard]] HostEventEnvelopeVerdict diverge(DivergenceReason reason) {
    return HostEventEnvelopeVerdict{std::in_place_type<EnvelopeDiverged>, EnvelopeDiverged{reason}};
}

// Build the schedule_pos -> capability call-site-index map from the A2 module.
// Mirrors the D1b controller's join authority (the controller keeps its own
// copy until the future production host routes both through this authority).
// Returns nullopt on a manifest whose call sites are out of range / collide.
[[nodiscard]] std::optional<std::vector<std::optional<std::size_t>>>
schedule_to_callsite(const VerifiedCoreWasmSchemaModule &module) {
    std::vector<std::optional<std::size_t>> map(module.node_count(), std::nullopt);
    for (std::size_t i = 0; i < module.call_site_count(); ++i) {
        auto cs = module.resolve(ManifestCallSiteIndex{i});
        if (!cs.ok()) {
            return std::nullopt;
        }
        const std::size_t sp = cs.call_site->schedule_pos().value;
        if (sp >= map.size() || map[sp].has_value()) {
            return std::nullopt;
        }
        map[sp] = i;
    }
    return map;
}

// Match a ledger capability coordinate (capability, source_symbol, ordinal 0)
// against the A2 call site at `schedule_pos` via the prebuilt map.
[[nodiscard]] bool
capability_coordinate_matches(const VerifiedCoreWasmSchemaModule &module,
                              const std::vector<std::optional<std::size_t>> &sched_map,
                              std::size_t schedule_pos,
                              ir::core::CoreCapabilityId capability,
                              std::uint64_t source_symbol,
                              InvocationOrdinal ordinal) {
    if (ordinal != InvocationOrdinal{0}) {
        return false;
    }
    if (schedule_pos >= sched_map.size() || !sched_map[schedule_pos].has_value()) {
        return false;
    }
    auto cs = module.resolve(ManifestCallSiteIndex{*sched_map[schedule_pos]});
    if (!cs.ok()) {
        return false;
    }
    return cs.call_site->capability() == capability &&
           cs.call_site->source_symbol() == source_symbol &&
           cs.call_site->invocation_ordinal() == InvocationOrdinal{0};
}

// Join one module completion record against the A2 manifest node at its slot.
// Enforces kind (identity vs capability), workflow node id, dense schedule
// position, and (for a capability) the call-site identity + OK status.
[[nodiscard]] bool
event_record_matches_manifest(const NodeEventRecord &record,
                              const VerifiedCoreWasmSchemaModule &module,
                              const std::vector<std::optional<std::size_t>> &sched_map,
                              std::size_t slot) {
    auto node = module.resolve_node(ManifestNodeIndex{slot});
    if (!node.ok()) {
        return false;
    }
    const bool manifest_is_cap = node.node->cap_call_count() == kCapabilityCallCount;
    const bool record_is_cap = record.kind == NodeKind::Capability;
    if (manifest_is_cap != record_is_cap) {
        return false;
    }
    if (record.workflow_node_id != node.node->workflow_node_id() ||
        record.schedule_pos != ManifestNodeIndex{slot} || record.status != AHFL_CAP_OK) {
        return false;
    }
    if (!record_is_cap) {
        return true;
    }
    if (!sched_map[slot].has_value()) {
        return false;
    }
    auto cs = module.resolve(ManifestCallSiteIndex{*sched_map[slot]});
    if (!cs.ok()) {
        return false;
    }
    return record.capability == cs.call_site->capability() &&
           record.source_symbol == cs.call_site->source_symbol() &&
           record.invocation_ordinal == cs.call_site->invocation_ordinal();
}

} // namespace

HostEventEnvelopeVerdict
join_host_observations(std::span<const HostCallbackObservation> observations,
                       std::span<const NodeEventRecord> node_events,
                       const VerifiedCoreWasmSchemaModule &module,
                       const CoreWasmResumeRecord &ledger) {
    // --- ledger / frontier shape ------------------------------------------------
    if (ledger.resume_state != ResumeState::Suspended) {
        return diverge(DivergenceReason::FrontierNotSuspended);
    }
    if (ledger.nodes.empty()) {
        return diverge(DivergenceReason::LedgerFrontierInvalid);
    }
    const std::size_t frontier_pos = ledger.nodes.size() - 1;
    const ResumeNode &frontier = ledger.nodes[frontier_pos];
    if (frontier.node_kind != NodeKind::Capability || !frontier.pending.has_value() ||
        frontier.workflow_node_id != ledger.suspended_node_id) {
        return diverge(DivergenceReason::LedgerFrontierInvalid);
    }

    auto sched_map = schedule_to_callsite(module);
    if (!sched_map.has_value()) {
        return diverge(DivergenceReason::ManifestTopologyInvalid);
    }

    // --- ledger <-> A2 manifest + memo presence (strictly below frontier) -------
    // The ledger nodes are a dense schedule prefix; verify every node against the
    // A2 manifest. Every strictly-below capability carries the single memo its
    // observation will replay; the frontier carries the one pending.
    for (std::size_t i = 0; i < ledger.nodes.size(); ++i) {
        const ResumeNode &n = ledger.nodes[i];
        if (n.schedule_pos != i) {
            return diverge(DivergenceReason::LedgerCoordinateMismatch);
        }
        auto node = module.resolve_node(ManifestNodeIndex{i});
        if (!node.ok() || node.node->workflow_node_id() != n.workflow_node_id) {
            return diverge(DivergenceReason::LedgerCoordinateMismatch);
        }
        const bool is_capability = n.node_kind == NodeKind::Capability;
        const std::uint8_t want = is_capability ? kCapabilityCallCount : kIdentityCallCount;
        if (node.node->cap_call_count() != want) {
            return diverge(DivergenceReason::LedgerCoordinateMismatch);
        }

        if (!is_capability) {
            if (!n.memo.empty() || n.pending.has_value()) {
                return diverge(DivergenceReason::LedgerCoordinateMismatch);
            }
            continue;
        }

        const bool is_frontier = i == frontier_pos;
        if (is_frontier) {
            // Frontier: exactly one pending, NO memo. Gate on the cardinalities
            // directly -- never derive presence from memo.size() == 1, which
            // would silently accept two-or-more stray memos that are then never
            // coordinate-checked or counted.
            if (!n.pending.has_value() || !n.memo.empty()) {
                return diverge(DivergenceReason::LedgerCoordinateMismatch);
            }
            const ResumePendingEntry &pending = *n.pending;
            if (!capability_coordinate_matches(module,
                                               *sched_map,
                                               i,
                                               pending.capability,
                                               pending.source_symbol,
                                               pending.invocation_ordinal)) {
                return diverge(DivergenceReason::LedgerCoordinateMismatch);
            }
            continue;
        }

        // Strictly below the frontier: exactly one ordinal-0 memo, no pending.
        if (n.pending.has_value() || n.memo.size() != 1) {
            return diverge(DivergenceReason::MemoNotInLedger);
        }
        const ResumeMemoEntry &memo = n.memo[0];
        if (!capability_coordinate_matches(module,
                                           *sched_map,
                                           i,
                                           memo.capability,
                                           memo.source_symbol,
                                           memo.invocation_ordinal)) {
            return diverge(DivergenceReason::LedgerCoordinateMismatch);
        }
    }

    // --- module completion records: dense prefix strictly below the frontier ----
    if (node_events.size() < frontier_pos) {
        return diverge(DivergenceReason::EventPrefixIncomplete);
    }
    if (node_events.size() > frontier_pos) {
        return diverge(DivergenceReason::EventPrefixBeyondFrontier);
    }
    for (std::size_t i = 0; i < node_events.size(); ++i) {
        if (!event_record_matches_manifest(node_events[i], module, *sched_map, i)) {
            return diverge(DivergenceReason::EventCoordinateMismatch);
        }
    }

    // --- host observations: one per below-frontier capability, dense + exact ----
    // The expected import-cursor order is the below-frontier capability nodes in
    // ascending SCHEDULE order (derived from the manifest map, not assumed from
    // how A2 happens to store call sites). The host callbacks must visit exactly
    // those call sites, in that order.
    std::vector<std::size_t> expected_calls;
    for (std::size_t i = 0; i < frontier_pos; ++i) {
        if (sched_map->at(i).has_value()) {
            expected_calls.push_back(*sched_map->at(i));
        }
    }
    const std::size_t replay_capabilities = expected_calls.size();

    // Cardinality first so an over/under-supplied observation vector is reported
    // as Extra/Missing, while a right-sized vector that still repeats or skips a
    // call site falls through to the Duplicate / OrderGap checks below.
    if (observations.size() > replay_capabilities) {
        return diverge(DivergenceReason::ObservationExtra);
    }
    if (observations.size() < replay_capabilities) {
        return diverge(DivergenceReason::ObservationMissing);
    }
    // Actual call-site values already visited (keyed over the module call-site
    // domain), so a repeated call site is a Duplicate even if its value happens
    // to equal a future expected position.
    std::vector<bool> seen_callsite(module.call_site_count(), false);
    for (std::size_t j = 0; j < observations.size(); ++j) {
        const HostCallbackObservation &obs = observations[j];
        if (obs.call_site.value >= module.call_site_count()) {
            return diverge(DivergenceReason::ObservationCoordinateInvalid);
        }
        auto cs = module.resolve(obs.call_site);
        if (!cs.ok()) {
            return diverge(DivergenceReason::ObservationCoordinateInvalid);
        }
        const std::size_t schedule = cs.call_site->schedule_pos().value;
        if (schedule >= frontier_pos) {
            return diverge(DivergenceReason::ObservationExtra);
        }
        if (seen_callsite[obs.call_site.value]) {
            return diverge(DivergenceReason::ObservationDuplicate);
        }
        // The callback at cursor position j must be the exact call site occupying
        // that below-frontier schedule slot; any other value is an order gap.
        if (obs.call_site.value != expected_calls[j]) {
            return diverge(DivergenceReason::ObservationOrderGap);
        }
        if (obs.import_ordinal != cs.call_site->import_ordinal()) {
            return diverge(DivergenceReason::ImportOrdinalMismatch);
        }
        if (obs.status != AHFL_CAP_OK) {
            return diverge(DivergenceReason::ObservationStatusNotOk);
        }
        switch (obs.source_state) {
        case CallbackSourceState::Live:
            return diverge(DivergenceReason::LiveBelowFrontier);
        case CallbackSourceState::Injected:
            return diverge(DivergenceReason::InjectedBelowFrontier);
        case CallbackSourceState::MemoReplayed:
            break;
        default:
            // Fail closed: an out-of-set source state (legal for a
            // fixed-underlying-type enum via static_cast, e.g. from an
            // unvalidated wire decoder or host FFI boundary) must never be
            // accepted as a clean MemoReplayed replay. -Wswitch gives no
            // signal once every named enumerator is covered.
            return diverge(DivergenceReason::ObservationSourceStateInvalid);
        }
        seen_callsite[obs.call_site.value] = true;
    }

    // --- exact ------------------------------------------------------------------
    EnvelopeExact exact;
    exact.runtime_node_order_observed = true;
    // A durable-resume attestation needs a NON-EMPTY memo-replayed prefix; an
    // all-identity prefix has no durable effect whose replay can be witnessed.
    exact.durable_resume_observed = replay_capabilities > 0;
    return HostEventEnvelopeVerdict{std::in_place_type<EnvelopeExact>, exact};
}

} // namespace ahfl::runtime::host_event_envelope
