#pragma once

// RFC 0026 KR6.5 E4-B2-E-1: the HOST-INDEPENDENT import-observation model and
// the exact-order / no-reinvoke verdict engine.
//
// Seam (RFC 0026 lines 751-752): a Wasm module cannot attest whether a given
// `ahfl_cap` callback was memo-replayed, injected, or live -- the import sees
// only `(status, ptr, len)`. The callback's `source_state` is recorded by the
// PRODUCTION HOST at each import, and at the run2 boundary it is JOINED with
// the module event log by manifest coordinate into an authenticated host
// envelope; NO-REINVOKE is proven by THAT envelope together with the
// ledger/manifest gate, never by the module event record alone.
//
// This file is the host-INDEPENDENT, PURE half of that envelope: the typed
// `HostCallbackObservation` a host records per import, and a pure join/verdict
// over four already-landed evidence kinds:
//   * the host observations (source state + status + import ordinal per call),
//   * the structurally DECODED node-event records (D1a-3 framing is a separate
//     authority; this engine never touches a linear-memory span),
//   * the A2 verified exec-manifest module (identity / call-site authority),
//   * the A1 authenticated per-node replay ledger (memo / pending frontier).
// It generalizes the D1b controller's private `event_join` into a reusable
// evidence authority that enforces:
//   * a DENSE exact node order (one module completion record per scheduled node
//     strictly below the suspended frontier),
//   * exactly ONE observation per capability call site, in dense call-site
//     order, with the matching import ordinal,
//   * an OK host status matched by a module OK completion record,
//   * ZERO Live (and zero Injected) observations below the frontier -- the
//     no-reinvoke violation is `LiveBelowFrontier`,
//   * every MemoReplayed observation backed by a matching ledger memo.
//
// FOUNDATION only: there is NO Wasm VM, NO host, NO persistence, and NO
// HMAC/wire envelope here (the authenticated transport + flipping the release
// claim booleans to true remain blocked B2-E items). A verdict here is evidence
// ABOUT a run; it does not by itself set the conformance release claims.

#include <cstdint>
#include <span>
#include <variant>

#include "runtime/engine/core_wasm_node_events.hpp"   // NodeEventRecord (decoded framing)
#include "runtime/engine/core_wasm_resume_record.hpp" // CoreWasmResumeRecord (A1 ledger)
#include "runtime/engine/core_wasm_schema_module.hpp" // VerifiedCoreWasmSchemaModule (A2)

namespace ahfl::runtime::host_event_envelope {

// Reuse the A2 strong coordinate / ordinal types so an observation can never
// confuse a call-site cursor, a schedule position, or an import ordinal.
using core_wasm_node_events::NodeEventRecord;
using core_wasm_resume::CoreWasmResumeRecord;
using core_wasm_schema_module::CapabilityImportOrdinal;
using core_wasm_schema_module::ManifestCallSiteIndex;
using core_wasm_schema_module::ManifestNodeIndex;
using core_wasm_schema_module::VerifiedCoreWasmSchemaModule;

// Where the capability RESULT served at one import callback came from. This is
// the fact the module alone cannot witness. Recorded by the production host at
// the `ahfl_cap` boundary.
enum class CallbackSourceState : std::uint8_t {
    MemoReplayed = 0, // host returned a committed ledger memo (effect-free replay)
    Injected = 1,     // host supplied the fresh frontier injection
    Live = 2,         // host issued a real capability effect
};

// One host-recorded capability import. The canonical coordinate is the A2
// call-site index (the host import cursor); the schedule position, capability,
// and source symbol are all derivable from it via the A2 module, so they are
// NOT duplicated here (Principle 2: index identity, no parallel SSOT).
struct HostCallbackObservation {
    ManifestCallSiteIndex call_site{};
    CapabilityImportOrdinal import_ordinal{};
    CallbackSourceState source_state{CallbackSourceState::MemoReplayed};
    // The raw AHFL_CAP_* status the host returned/saw at the callback (0 = OK).
    std::uint32_t status{0};

    [[nodiscard]] friend bool operator==(const HostCallbackObservation &,
                                         const HostCallbackObservation &) noexcept = default;
};

// A typed, fail-closed reason the joined evidence is NOT an exact no-reinvoke
// replay. Every distinct order / status / source-state defect the brief lists
// is its own enumerator so a consumer never pattern-matches a string or bool.
enum class DivergenceReason : std::uint8_t {
    FrontierNotSuspended,         // ledger resume_state is not Suspended
    LedgerFrontierInvalid,        // ledger empty or its last node is not a pending capability
    ManifestTopologyInvalid,      // A2 schedule->call-site map not buildable (defensive)
    LedgerCoordinateMismatch,     // a ledger node/id/cap/symbol does not match the A2 manifest
    MemoNotInLedger,              // a below-frontier capability lacks its single ledger memo
    EventPrefixIncomplete,        // fewer completion records than the below-frontier prefix
    EventPrefixBeyondFrontier,    // completion records reach/ pass the suspended frontier
    EventCoordinateMismatch,      // a record kind/id/schedule/cap/symbol/status != A2
    ObservationCoordinateInvalid, // call-site index does not resolve in the A2 module
    ObservationExtra,             // observation for a node at/above the frontier
    ObservationDuplicate,         // two observations for one capability call site
    ObservationOrderGap,          // observations are not in strict dense call-site order
    ObservationMissing,           // a below-frontier capability call site has no observation
    ImportOrdinalMismatch,        // observed import ordinal != the A2 call site's ordinal
    ObservationStatusNotOk,       // an observation carried a non-OK AHFL_CAP_* status
    LiveBelowFrontier,            // a LIVE callback below the suspended frontier (reinvoke)
    InjectedBelowFrontier,        // an Injected callback at a strictly below-frontier node
};

// The exact verdict. Only emitted when every join gate passes. The two claims
// are HOST-INDEPENDENT verdicts about the supplied evidence:
//   * `runtime_node_order_observed` is always true on Exact (the dense module
//     completion order matched the manifest);
//   * `durable_resume_observed` additionally requires a NON-EMPTY replayed
//     (memo) prefix -- an all-identity prefix has no durable effect to attest.
struct EnvelopeExact {
    bool runtime_node_order_observed{true};
    bool durable_resume_observed{false};

    [[nodiscard]] friend bool operator==(const EnvelopeExact &,
                                         const EnvelopeExact &) noexcept = default;
};

struct EnvelopeDiverged {
    DivergenceReason reason{};

    [[nodiscard]] friend bool operator==(const EnvelopeDiverged &,
                                         const EnvelopeDiverged &) noexcept = default;
};

// The join result. Consume with an exhaustive `std::visit(Overloaded{...})`;
// adding a third alternative is a compile failure at every visitor.
using HostEventEnvelopeVerdict = std::variant<EnvelopeExact, EnvelopeDiverged>;

// Purely join the already-decoded module completion records and host callback
// observations against the A2 manifest and A1 ledger, and return the exact-order
// / no-reinvoke verdict.
//
// `node_events` are the records produced by
// `core_wasm_node_events::decode_node_events` (structural framing is that
// authority's job; a malformed buffer is rejected before this call). The ledger
// is the authenticated A1 record. Deterministic and allocation-bounded by the
// manifest/ledger/evidence sizes; it never touches a VM, a store, or a key.
[[nodiscard]] HostEventEnvelopeVerdict
join_host_observations(std::span<const HostCallbackObservation> observations,
                       std::span<const NodeEventRecord> node_events,
                       const VerifiedCoreWasmSchemaModule &module,
                       const CoreWasmResumeRecord &ledger);

} // namespace ahfl::runtime::host_event_envelope
