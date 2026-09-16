// RFC 0026 KR6.5 E4-B2-E-1 permanent regression for the host-independent
// import-observation join / exact-order + no-reinvoke verdict engine.
// Hand-rolled check()/main(), no test framework, always runs (no store, no VM).
//
// The module/event fixtures come from the SHARED resume_test_support.hpp
// authority (F4) -- the same hand-built A2-admitted modules and 40-byte-record
// event-memory synthesizer the D1b controller regression uses -- so this TU
// duplicates NO fixture code. The structural framing itself is decoded through
// the landed D1a-3 `decode_node_events`; this TU only joins its typed records
// with hand-built host observations and a hand-built A1-style ledger.
//
// FOUNDATION: the verdicts here are evidence ABOUT a supplied run; there is no
// HMAC wire envelope, no VM, and the conformance release claims stay false.

#include "resume_test_support.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "runtime/engine/core_wasm_node_events.hpp"
#include "runtime/engine/host_event_envelope.hpp"

namespace {

using namespace ahfl::runtime::resume_test_support;
namespace hee = ahfl::runtime::host_event_envelope;
namespace nev = ahfl::runtime::core_wasm_node_events;
namespace csm = ahfl::runtime::core_wasm_schema_module;

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_resume::CoreWasmResumeRecord;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::core_wasm_resume::NodeKind;
using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::core_wasm_resume::ResumeMemoEntry;
using ahfl::runtime::core_wasm_resume::ResumeNode;
using ahfl::runtime::core_wasm_resume::ResumePendingEntry;
using ahfl::runtime::core_wasm_resume::ResumeState;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// One module topology: identity 40, then capabilities 41 (cap 3/sym 900),
// 42 (cap 4/sym 901), 43 (cap 5/sym 902). Schema node 1 = bounded-String result
// shared by every cap; node 0 is the Int param.
struct Topology {
    ModuleSpec spec;
    std::vector<ManifestNodeSpec> nodes;
};

Topology make_topology() {
    Topology t;
    t.spec.entry_id = 7;
    t.spec.extra_schema_nodes = {bounded_string_node(8)};
    t.spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                   CapSpec{4, 901, CoreWireSchemaNodeId{1}},
                   CapSpec{5, 902, CoreWireSchemaNodeId{1}}};
    t.spec.nodes = {ManifestNodeSpec{40, 0, 0, 0},
                    ManifestNodeSpec{41, 1, 3, 900},
                    ManifestNodeSpec{42, 1, 4, 901},
                    ManifestNodeSpec{43, 1, 5, 902}};
    t.nodes = t.spec.nodes;
    return t;
}

// Decode a full event page through the landed D1a-3 framing authority.
std::vector<nev::NodeEventRecord> decode(const std::vector<std::uint8_t> &page,
                                         std::size_t node_count) {
    auto decoded = nev::decode_node_events(std::span<const std::uint8_t>(page), node_count);
    check(decoded.has_value(), "fixture.event_page_decodes");
    if (!decoded.has_value()) {
        return {};
    }
    return std::move(*decoded);
}

// Build a Suspended ledger over `node_count` schedule nodes: every strictly
// below-frontier capability carries one ordinal-0 memo; the final node is the
// pending capability frontier. `nodes` is the admitted manifest topology.
CoreWasmResumeRecord make_suspended_ledger(const std::vector<ManifestNodeSpec> &nodes,
                                           std::size_t node_count) {
    CoreWasmResumeRecord r;
    r.format_version = 1;
    r.entry_id = ahfl::ir::core::CoreWorkflowId{7};
    r.entry_input_slot = PayloadSlotId{9};
    r.resume_state = ResumeState::Suspended;
    const std::size_t frontier = node_count - 1;
    r.suspended_node_id = CoreWorkflowNodeId{nodes[frontier].workflow_node_id};
    for (std::size_t i = 0; i < node_count; ++i) {
        const auto &spec = nodes[i];
        ResumeNode n;
        n.workflow_node_id = CoreWorkflowNodeId{spec.workflow_node_id};
        n.schedule_pos = static_cast<std::uint32_t>(i);
        n.node_kind = spec.cap_call_count == 1 ? NodeKind::Capability : NodeKind::Identity;
        if (spec.cap_call_count == 1) {
            if (i == frontier) {
                ResumePendingEntry p;
                p.invocation_ordinal = InvocationOrdinal{0};
                p.capability = CoreCapabilityId{spec.capability};
                p.source_symbol = spec.source_symbol;
                p.arg_hash = 0xABCDEF;
                n.pending = p;
            } else {
                ResumeMemoEntry m;
                m.invocation_ordinal = InvocationOrdinal{0};
                m.capability = CoreCapabilityId{spec.capability};
                m.source_symbol = spec.source_symbol;
                m.arg_hash = 0x1234 + static_cast<std::uint64_t>(i);
                m.result_slot = PayloadSlotId{100 + i};
                n.memo.push_back(m);
            }
        }
        r.nodes.push_back(n);
    }
    return r;
}

// One dense, in-order MemoReplayed OK observation per below-frontier cap.
std::vector<hee::HostCallbackObservation>
make_memo_observations(const csm::VerifiedCoreWasmSchemaModule &mod,
                       std::size_t replay_capabilities) {
    std::vector<hee::HostCallbackObservation> obs;
    for (std::size_t c = 0; c < replay_capabilities; ++c) {
        auto cs = mod.resolve(csm::ManifestCallSiteIndex{c});
        check(cs.ok(), "fixture.callsite_resolves");
        hee::HostCallbackObservation o;
        o.call_site = csm::ManifestCallSiteIndex{c};
        o.import_ordinal = cs.call_site->import_ordinal();
        o.source_state = hee::CallbackSourceState::MemoReplayed;
        o.status = 0; // AHFL_CAP_OK
        obs.push_back(o);
    }
    return obs;
}

[[nodiscard]] bool is_exact(const hee::HostEventEnvelopeVerdict &v) {
    return std::holds_alternative<hee::EnvelopeExact>(v);
}

[[nodiscard]] bool diverged_with(const hee::HostEventEnvelopeVerdict &v,
                                 hee::DivergenceReason want) {
    return std::visit(ahfl::Overloaded{[](hee::EnvelopeExact) { return false; },
                                       [want](hee::EnvelopeDiverged d) {
                                           return d.reason == want;
                                       }},
                      v);
}

} // namespace

int main() {
    Topology topo = make_topology();
    auto mod_res = admit_module(topo.spec);
    check(mod_res.ok(), "module.admits");
    if (!mod_res.ok()) {
        std::cerr << "host_event_envelope: FATAL module build\n";
        return 1;
    }
    const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
    check(mod.node_count() == 4 && mod.call_site_count() == 3, "module.topology_4_3");

    // Suspended at node 43: below-frontier prefix = nodes 40,41,42 = identity +
    // two memo-replayed capabilities; event log publishes exactly those three.
    constexpr std::size_t kFullNodes = 4;
    constexpr std::size_t kFullReplayCaps = 2; // caps 41,42 replay; cap 43 is frontier
    const auto full_ledger = make_suspended_ledger(topo.nodes, kFullNodes);
    const auto full_page = event_memory(topo.nodes, 3); // identity + 2 replay caps
    const auto full_events = decode(full_page, kFullNodes);
    const auto full_obs = make_memo_observations(mod, kFullReplayCaps);

    // ---- (a) dense exact memo replay below a Suspended frontier -> Exact -------
    {
        const auto verdict =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(is_exact(verdict), "a.exact");
        if (is_exact(verdict)) {
            const auto &e = std::get<hee::EnvelopeExact>(verdict);
            check(e.runtime_node_order_observed, "a.node_order_observed");
            check(e.durable_resume_observed, "a.durable_resume_observed");
        }
    }

    // ---- (b) a Live observation below the frontier -> LiveBelowFrontier --------
    {
        auto obs = full_obs;
        obs[0].source_state = hee::CallbackSourceState::Live;
        const auto verdict =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(verdict, hee::DivergenceReason::LiveBelowFrontier),
              "b.live_below_frontier");
    }
    // An Injected source at a strictly-below-frontier node is also a divergence.
    {
        auto obs = full_obs;
        obs[1].source_state = hee::CallbackSourceState::Injected;
        const auto verdict =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(verdict, hee::DivergenceReason::InjectedBelowFrontier),
              "b.injected_below_frontier");
    }

    // ---- (c) missing / extra / duplicate / order-gap / schedule gap ------------
    {
        // Missing one observation.
        auto obs = full_obs;
        obs.pop_back();
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ObservationMissing), "c.missing");
    }
    {
        // Extra: an observation for the frontier call site (cap 43, index 2).
        auto obs = full_obs;
        hee::HostCallbackObservation frontier_obs = obs[0];
        frontier_obs.call_site = csm::ManifestCallSiteIndex{2};
        obs.push_back(frontier_obs);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ObservationExtra), "c.extra");
    }
    {
        // Right-sized but duplicated call site (0,0 instead of 0,1).
        auto obs = full_obs;
        obs[1].call_site = csm::ManifestCallSiteIndex{0};
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ObservationDuplicate), "c.duplicate");
    }
    {
        // Right-sized but out of dense order (1,0 instead of 0,1).
        auto obs = full_obs;
        std::swap(obs[0], obs[1]);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ObservationOrderGap), "c.order_gap");
    }
    {
        // Schedule gap: event log publishes fewer records than the below-frontier
        // prefix (only the identity node, missing both capability completions).
        const auto gap_page = event_memory(topo.nodes, 1);
        const auto gap_events = decode(gap_page, kFullNodes);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(gap_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::EventPrefixIncomplete),
              "c.event_schedule_gap");
    }
    {
        // Event log reaches the frontier (4 records) -> beyond the suspended edge.
        const auto beyond_page = event_memory(topo.nodes, 4);
        const auto beyond_events = decode(beyond_page, kFullNodes);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(beyond_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::EventPrefixBeyondFrontier),
              "c.event_beyond_frontier");
    }

    // ---- (d) observation status OK without a matching module completion --------
    {
        // Drop the second capability completion record by publishing a page whose
        // count (2) only carries identity + cap 41, but keep two observations.
        const auto short_page = event_memory(topo.nodes, 2);
        const auto short_events = decode(short_page, kFullNodes);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(short_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::EventPrefixIncomplete),
              "d.ok_status_without_completion");
    }
    // And a non-OK host status is rejected even though the module record is OK.
    {
        auto obs = full_obs;
        obs[0].status = 1; // AHFL_CAP_ERROR
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ObservationStatusNotOk),
              "d.observation_status_not_ok");
    }

    // ---- (e) memo / coordinate / ordinal / ledger defects ----------------------
    {
        // A below-frontier memo whose capability does not match the A2 call site.
        auto ledger = full_ledger;
        ledger.nodes[1].memo[0].capability = CoreCapabilityId{3};
        ledger.nodes[1].memo[0].source_symbol = 999;
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        ledger);
        check(diverged_with(v, hee::DivergenceReason::LedgerCoordinateMismatch),
              "e.ledger_memo_coordinate_mismatch");
    }
    {
        // A below-frontier capability with no memo -> MemoNotInLedger.
        auto ledger = full_ledger;
        ledger.nodes[1].memo.clear();
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        ledger);
        check(diverged_with(v, hee::DivergenceReason::MemoNotInLedger), "e.memo_not_in_ledger");
    }
    {
        // Wrong import ordinal on the observation.
        auto obs = full_obs;
        obs[0].import_ordinal = csm::CapabilityImportOrdinal{7};
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::ImportOrdinalMismatch),
              "e.import_ordinal_mismatch");
    }
    {
        // Ledger not Suspended -> FrontierNotSuspended.
        auto ledger = full_ledger;
        ledger.resume_state = ResumeState::Injected;
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(full_events),
                                        mod,
                                        ledger);
        check(diverged_with(v, hee::DivergenceReason::FrontierNotSuspended),
              "e.frontier_not_suspended");
    }
    {
        // Event completion record capability does not match the manifest call site.
        auto page = full_page;
        // record slot 1 (cap 41) capability field lives at base 1032+40+12.
        put_u32(page, kEventRecordsBase + 1 * kRecordBytes + 12, 4);
        const auto bad_events = decode(page, kFullNodes);
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(full_obs),
                                        std::span<const nev::NodeEventRecord>(bad_events),
                                        mod,
                                        full_ledger);
        check(diverged_with(v, hee::DivergenceReason::EventCoordinateMismatch),
              "e.event_coordinate_mismatch");
    }

    // ---- (f) all-identity below-frontier prefix: order exact, no durable claim -
    {
        // Suspend immediately at the first capability node 41: below-frontier is
        // just the identity node 40; zero memo replays.
        constexpr std::size_t kEarlyNodes = 2;
        const auto early_ledger = make_suspended_ledger(topo.nodes, kEarlyNodes);
        const auto early_page = event_memory(topo.nodes, 1); // identity only
        const auto early_events = decode(early_page, kEarlyNodes);
        const std::vector<hee::HostCallbackObservation> no_obs;
        const auto v =
            hee::join_host_observations(std::span<const hee::HostCallbackObservation>(no_obs),
                                        std::span<const nev::NodeEventRecord>(early_events),
                                        mod,
                                        early_ledger);
        check(is_exact(v), "f.identity_only_exact");
        if (is_exact(v)) {
            const auto &e = std::get<hee::EnvelopeExact>(v);
            check(e.runtime_node_order_observed, "f.identity_order_observed");
            check(!e.durable_resume_observed, "f.identity_no_durable_claim");
        }
    }

    if (g_failures != 0) {
        std::cerr << "host_event_envelope: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "host_event_envelope: all checks passed\n";
    return 0;
}
