#include "runtime/engine/core_wasm_resume_controller.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ahfl/runtime/ahfl_host.h"                     // AHFL_CAP_OK/ERROR/PENDING (ABI SSOT)
#include "ahfl/base/support/overloaded.hpp"
#include "base/json/json_value.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_node_events.hpp"    // decode_node_events, event_region_heap_base
#include "runtime/engine/core_wire_canonical_size.hpp" // max_canonical_json_size
#include "runtime/engine/core_wire_codec.hpp"          // wire_codec::decode_json
#include "runtime/engine/durable_effect_intent.hpp"    // FrozenAuthorityNamespaceBuilder
#include "runtime/evaluator/value_json.hpp"            // hash_values, value_to_json

namespace ahfl::runtime::core_wasm_resume_controller {
namespace {

using core_wasm_resume::CoreWasmResumeRecord;
using core_wasm_resume::DigestHex;
using core_wasm_resume::InvocationOrdinal;
using core_wasm_resume::PayloadSlotId;
using core_wasm_resume::ResumeMemoEntry;
using core_wasm_resume::ResumeNode;
using core_wasm_resume::ResumeState;
using core_wasm_schema_module::ArtifactDigest;
using core_wasm_schema_module::ManifestCallSiteIndex;
using core_wasm_schema_module::ManifestNodeIndex;
using core_wasm_schema_module::VerifiedCoreWasmSchemaModule;

// A2 baseline coordinate gate constants: an identity node has cap_call_count 0, a
// capability call site has cap_call_count 1 (this slice supports no other cardinality,
// and every replayed invocation ordinal is 0 -- no per-node loops yet).
constexpr std::uint8_t kIdentityCallCount = 0;
constexpr std::uint8_t kCapabilityCallCount = 1;

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kU32Max = std::numeric_limits<std::uint32_t>::max();

// D2b-4: the host-bound dedup consultation context carried phase 1 -> prepared.
// The checkpoint namespace (wf, ckpt) is read from the snapshot's AUTHENTICATED
// manifest, never re-supplied; the authority id is host-bound once; the
// authority pointer is optional and borrowed (nullptr = pre-D2b-4 behavior).
struct DedupContext {
    core_wasm_idempotency_token::IdempotencyAuthorityId authority_id{};
    ir::core::CoreWorkflowId workflow{};
    payload_store::ResumeCheckpointId checkpoint{};
    const durable_effect_authority::DurableEffectAuthority *authority{nullptr};
};

// Convert one lowercase-hex nibble to its value, or 0xFF if it is not a lowercase-hex
// digit. Fixed-work: never early-exits on a bad digit (the caller ORs a validity flag),
// so a malformed hex field costs the same as a valid one.
[[nodiscard]] std::uint8_t hex_nibble(char c) noexcept {
    const auto uc = static_cast<unsigned char>(c);
    const bool is_digit = uc >= '0' && uc <= '9';
    const bool is_lower = uc >= 'a' && uc <= 'f';
    const auto digit = static_cast<std::uint8_t>(uc - '0');
    const auto lower = static_cast<std::uint8_t>(uc - 'a' + 10);
    if (is_digit) {
        return digit;
    }
    if (is_lower) {
        return lower;
    }
    return 0xFFu;
}

// Canonicalize a 64-char A1 DigestHex into 32 raw bytes, fixed-work. `ok` is set false
// (never toggled back true) if any nibble is not lowercase hex, so the digest gate can
// treat a malformed record digest as a mismatch without an early exit.
[[nodiscard]] ArtifactDigest hex_to_raw(const DigestHex &hex, bool &ok) noexcept {
    ArtifactDigest raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::uint8_t hi = hex_nibble(hex[2 * i]);
        const std::uint8_t lo = hex_nibble(hex[2 * i + 1]);
        ok = ok && (hi != 0xFFu) && (lo != 0xFFu);
        const std::uint8_t hi_v = (hi == 0xFFu) ? 0u : hi;
        const std::uint8_t lo_v = (lo == 0xFFu) ? 0u : lo;
        raw[i] = static_cast<std::uint8_t>((hi_v << 4) | lo_v);
    }
    return raw;
}

// Fixed-work equality over two 32-byte digests: always compares all 32 bytes.
[[nodiscard]] bool digest_equal_fixed(const ArtifactDigest &a, const ArtifactDigest &b) noexcept {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

// The whole controller phase machine. Every step / ACK / terminal API validates the
// current phase before acting; any fault moves to the terminal Failed phase.
enum class Phase : std::uint8_t {
    Replaying,
    AwaitingSlot,
    AwaitingPublishAck,
    AwaitingLiveResult,
    AwaitingConsumedAck,
    Failed,
    Consumed,
};

} // namespace

// ---------------------------------------------------------------------------
// Opaque state definitions.
// ---------------------------------------------------------------------------
namespace detail {

// Phase-1 state: the captured module (a cheap copy of the shared handle), the pinned
// snapshot moved in from the store, and the schedule_pos -> call-site-index map built
// ONCE from the module (carried into PreparedState so no second authority is rebuilt).
struct GatedState {
    VerifiedCoreWasmSchemaModule module;
    payload_store::ResumeSnapshot snapshot;
    std::vector<std::optional<std::size_t>> schedule_to_callsite;
    DedupContext dedup;

    GatedState(VerifiedCoreWasmSchemaModule m, payload_store::ResumeSnapshot s,
               std::vector<std::optional<std::size_t>> sched_map, DedupContext dedup_context)
        : module(std::move(m)), snapshot(std::move(s)),
          schedule_to_callsite(std::move(sched_map)), dedup(std::move(dedup_context)) {}
};

// The fully-preflighted replay state and its in-place phase machine.
struct PreparedState {
    VerifiedCoreWasmSchemaModule module;
    CoreWasmResumeRecord record;         // authenticated; mutated on a publish ACK
    std::uint64_t current_generation{0}; // last committed generation N
    Phase phase{Phase::Replaying};

    // D2b-4 token-aware ReadyForLive consultation (nullopt authority = legacy).
    DedupContext dedup;

    // Controller-owned, join authority precomputed once at admission (O(1) lookups).
    std::vector<std::optional<std::size_t>> schedule_to_callsite; // by manifest schedule_pos
    std::size_t call_site_count{0};
    std::size_t frontier_call_site{0}; // call-site index of the frontier (suspended) node

    // Per-call-site replay authority, indexed by call-site index. `callsite_memo` holds
    // a NON-OWNING view of each committed capability's Verified-decoded result bytes
    // (absent for the frontier while Suspended, filled on publish ACK). A repeated
    // result_slot yields the SAME span (one authority in `admitted_slots`), though each
    // occurrence still runs its own Result decode. `callsite_arg_hash` holds the ledger
    // arg_hash (memo, or the frontier pending in the Suspended state).
    std::vector<std::optional<std::span<const std::uint8_t>>> callsite_memo;
    std::vector<std::optional<std::uint64_t>> callsite_arg_hash;

    // Controller-owned stable byte authorities. `admitted_slots` is the SOLE owner of
    // every committed slot payload (moved in once, never pushed/reallocated after mint);
    // `entry_span` and every `callsite_memo` view point into it. `injected_result_bytes`
    // is a SEPARATE controller-owned buffer for the frontier injection (assigned once,
    // never resized); after a publish ACK the frontier `callsite_memo` views it.
    std::vector<payload_store::ResolvedSlot> admitted_slots;
    std::span<const std::uint8_t> entry_span; // backs the EntryFrame (into admitted_slots)
    std::vector<std::uint8_t> injected_result_bytes;
    bool has_injected_result{false};
    PayloadSlotId entry_slot{};

    // Replay cursor: the next expected call-site index (schedule order). Advances on an
    // accepted ReturnMemo and on a publish ACK past the frontier.
    std::size_t replay_cursor{0};

    // Publish-handshake scratch (valid only across bind -> ack).
    std::optional<CoreWasmResumeRecord> pending_publish_record;
    std::vector<payload_store::Slot> pending_publish_slots;
    std::uint64_t pending_expected_generation{0};

    // Terminal scratch.
    std::uint64_t consume_expected_generation{0};

    explicit PreparedState(VerifiedCoreWasmSchemaModule m) : module(std::move(m)) {}

    // Reach into an opaque handle (PreparedState is a friend of the handle types).
    [[nodiscard]] static PreparedState &unwrap(PreparedResume &p) noexcept { return *p.state_; }

    [[nodiscard]] EntryFrame make_entry_frame() const noexcept {
        return EntryFrame(entry_span, entry_slot);
    }
    [[nodiscard]] static PreparedResume wrap(std::unique_ptr<PreparedState> state) noexcept {
        return PreparedResume(std::move(state));
    }
    [[nodiscard]] MarkConsumedPlan make_mark_consumed_plan(std::uint64_t expected) const noexcept {
        return MarkConsumedPlan(expected);
    }
    [[nodiscard]] PublishInjectedPlan
    make_publish_plan(std::uint64_t expected, const CoreWasmResumeRecord *rec,
                      std::span<const payload_store::Slot> slots) const noexcept {
        return PublishInjectedPlan(expected, rec, slots);
    }
};

// The Suspended-awaiting-injection state: holds the admitted intermediate authority
// plus the capacity, so the continuation can finish the preflight without re-opening
// the snapshot.
struct PendingState {
    std::unique_ptr<PreparedState> prepared; // admitted; missing only the final preflight
    LinearMemoryCapacityBytes capacity;

    static PendingInjection wrap(std::unique_ptr<PendingState> state) noexcept {
        return PendingInjection(std::move(state));
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Opaque handle special members (defined here so the state types are complete when
// the unique_ptr destructors are instantiated).
// ---------------------------------------------------------------------------
GatedResume::GatedResume(std::unique_ptr<detail::GatedState> state) noexcept
    : state_(std::move(state)) {}
GatedResume::GatedResume(GatedResume &&) noexcept = default;
GatedResume &GatedResume::operator=(GatedResume &&) noexcept = default;
GatedResume::~GatedResume() = default;

PreparedResume::PreparedResume(std::unique_ptr<detail::PreparedState> state) noexcept
    : state_(std::move(state)) {}
PreparedResume::PreparedResume(PreparedResume &&) noexcept = default;
PreparedResume &PreparedResume::operator=(PreparedResume &&) noexcept = default;
PreparedResume::~PreparedResume() = default;
EntryFrame PreparedResume::entry_frame() const noexcept { return state_->make_entry_frame(); }

PendingInjection::PendingInjection(std::unique_ptr<detail::PendingState> state) noexcept
    : state_(std::move(state)) {}
PendingInjection::PendingInjection(PendingInjection &&) noexcept = default;
PendingInjection &PendingInjection::operator=(PendingInjection &&) noexcept = default;
PendingInjection::~PendingInjection() = default;

namespace {

// ---- phase-1 gates ---------------------------------------------------------

// Run the three fixed-work digest compares (all complete before selecting) and return
// the first mismatch by priority Module -> WireSchema -> ExecManifest, or nullopt on
// full agreement.
[[nodiscard]] std::optional<ResumePrepareReason>
digest_gate(const CoreWasmResumeRecord &record, const VerifiedCoreWasmSchemaModule &module) {
    bool module_hex_ok = true;
    bool schema_hex_ok = true;
    bool manifest_hex_ok = true;
    const ArtifactDigest rec_module = hex_to_raw(record.module_sha256, module_hex_ok);
    const ArtifactDigest rec_schema = hex_to_raw(record.wire_schema_sha256, schema_hex_ok);
    const ArtifactDigest rec_manifest = hex_to_raw(record.exec_manifest_sha256, manifest_hex_ok);
    const bool module_ok = module_hex_ok && digest_equal_fixed(rec_module, module.module_sha256());
    const bool schema_ok =
        schema_hex_ok && digest_equal_fixed(rec_schema, module.wire_schema_sha256());
    const bool manifest_ok =
        manifest_hex_ok && digest_equal_fixed(rec_manifest, module.exec_manifest_sha256());
    if (!module_ok) {
        return ResumePrepareReason::ModuleDigestMismatch;
    }
    if (!schema_ok) {
        return ResumePrepareReason::WireSchemaDigestMismatch;
    }
    if (!manifest_ok) {
        return ResumePrepareReason::ExecManifestDigestMismatch;
    }
    return std::nullopt;
}

// Build the schedule_pos -> call-site-index map from the module alone (O(node_count +
// call_site_count)). Returns nullopt on a malformed manifest (out-of-range or duplicate
// schedule position). This is the SINGLE join authority, built once in phase 1.
[[nodiscard]] std::optional<std::vector<std::optional<std::size_t>>>
build_schedule_map(const VerifiedCoreWasmSchemaModule &module) {
    std::vector<std::optional<std::size_t>> map(module.node_count(), std::nullopt);
    const std::size_t count = module.call_site_count();
    for (std::size_t i = 0; i < count; ++i) {
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

// Compare a ledger capability coordinate (capability, source_symbol, invocation_ordinal
// == 0) against the A2 call site at schedule position `schedule_pos` via the prebuilt
// map (O(1)). Returns false on any mismatch or a nonzero ordinal (the A2 baseline this
// slice supports).
[[nodiscard]] bool
capability_identity_matches(const VerifiedCoreWasmSchemaModule &module,
                            const std::vector<std::optional<std::size_t>> &sched_map,
                            std::size_t schedule_pos, ir::core::CoreCapabilityId capability,
                            std::uint64_t source_symbol, InvocationOrdinal ordinal) {
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

// The full phase-1 coordinate / A2-baseline / record-state gate. Runs on the
// authenticated record WITHOUT any admitted slot, using the prebuilt schedule map for
// O(1) identity joins. Enforces: entry_id match; a dense prefix (nodes.size() <=
// module.node_count(), nodes[i].schedule_pos == i); the frontier (nodes.back()) is the
// suspended capability node; the per-node state matrix; and every capability memo/pending
// coordinate identity against its A2 call site.
[[nodiscard]] std::optional<ResumePrepareReason>
coordinate_gate(const CoreWasmResumeRecord &record, const VerifiedCoreWasmSchemaModule &module,
                const std::vector<std::optional<std::size_t>> &sched_map) {
    if (record.entry_id != module.entry_id()) {
        return ResumePrepareReason::CoordinateMismatch;
    }
    if (record.nodes.empty() || record.nodes.size() > module.node_count()) {
        return ResumePrepareReason::CoordinateMismatch;
    }
    const std::size_t frontier_index = record.nodes.size() - 1;
    const ResumeNode &frontier = record.nodes[frontier_index];
    if (frontier.node_kind != core_wasm_resume::NodeKind::Capability ||
        frontier.workflow_node_id != record.suspended_node_id) {
        return ResumePrepareReason::CoordinateMismatch;
    }

    std::size_t pending_seen = 0;
    for (std::size_t i = 0; i < record.nodes.size(); ++i) {
        const ResumeNode &n = record.nodes[i];
        if (n.schedule_pos != i) {
            return ResumePrepareReason::CoordinateMismatch;
        }
        const auto node = module.resolve_node(ManifestNodeIndex{i});
        if (!node.ok() || node.node->workflow_node_id() != n.workflow_node_id) {
            return ResumePrepareReason::CoordinateMismatch;
        }
        const bool is_capability = n.node_kind == core_wasm_resume::NodeKind::Capability;
        const std::uint8_t want = is_capability ? kCapabilityCallCount : kIdentityCallCount;
        if (node.node->cap_call_count() != want) {
            return ResumePrepareReason::CoordinateMismatch;
        }

        if (!is_capability) {
            // Identity node: empty memo, never a pending.
            if (!n.memo.empty() || n.pending.has_value()) {
                return ResumePrepareReason::CoordinateMismatch;
            }
            continue;
        }

        const bool is_frontier = i == frontier_index;
        if (is_frontier && record.resume_state == ResumeState::Suspended) {
            // Frontier, Suspended: empty memo + exactly one ordinal-0 pending.
            if (!n.memo.empty() || !n.pending.has_value()) {
                return ResumePrepareReason::CoordinateMismatch;
            }
            ++pending_seen;
            if (!capability_identity_matches(module, sched_map, i, n.pending->capability,
                                             n.pending->source_symbol,
                                             n.pending->invocation_ordinal)) {
                return ResumePrepareReason::CoordinateMismatch;
            }
        } else {
            // Non-frontier capability, OR the Injected frontier: exactly one ordinal-0
            // memo, no pending.
            if (n.memo.size() != 1 || n.pending.has_value()) {
                return ResumePrepareReason::CoordinateMismatch;
            }
            const ResumeMemoEntry &m = n.memo[0];
            if (!capability_identity_matches(module, sched_map, i, m.capability, m.source_symbol,
                                             m.invocation_ordinal)) {
                return ResumePrepareReason::CoordinateMismatch;
            }
        }
    }

    // Record-state cardinality: Suspended has exactly one pending (on the frontier);
    // Injected has none and a non-empty frontier memo.
    if (record.resume_state == ResumeState::Suspended) {
        if (pending_seen != 1) {
            return ResumePrepareReason::CoordinateMismatch;
        }
    } else {
        if (pending_seen != 0 || frontier.memo.empty()) {
            return ResumePrepareReason::CoordinateMismatch;
        }
    }
    return std::nullopt;
}

// ---- phase-2 admission helpers ---------------------------------------------

// A file-local slot-id -> admitted-slot index authority (O(1) lookup). Built once from
// the moved admitted set; a duplicate id is impossible after B1's SlotSetMismatch gate,
// so it is rejected defensively (fail-closed). `build_ok` is set false on a duplicate.
[[nodiscard]] std::unordered_map<std::uint64_t, std::size_t>
build_slot_index(const std::vector<payload_store::ResolvedSlot> &slots, bool &build_ok) {
    build_ok = true;
    std::unordered_map<std::uint64_t, std::size_t> index;
    index.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto [it, inserted] = index.emplace(slots[i].slot.value, i);
        (void)it;
        if (!inserted) {
            build_ok = false;
        }
    }
    return index;
}

// Build the PreparedState once slots are admitted: entry bytes, per-call-site memo
// (Verified-decoded per occurrence against the Result binding) and arg_hash authority,
// and the frontier call-site index. The schedule->call-site map is carried in from
// phase 1 (not rebuilt). Returns nullptr on a coordinate fault; sets `payload_invalid`
// when a memo fails its Verified decode.
[[nodiscard]] std::unique_ptr<detail::PreparedState>
build_prepared(VerifiedCoreWasmSchemaModule module,
               std::vector<std::optional<std::size_t>> sched_map,
               payload_store::ResolvedAvailable admitted, DedupContext dedup,
               bool &payload_invalid) {
    payload_invalid = false;
    auto st = std::make_unique<detail::PreparedState>(std::move(module));
    st->record = std::move(admitted.record);
    st->current_generation = admitted.generation;
    st->dedup = std::move(dedup);
    st->call_site_count = st->module.call_site_count();
    st->entry_slot = st->record.entry_input_slot;
    st->admitted_slots = std::move(admitted.slots);
    st->schedule_to_callsite = std::move(sched_map);

    // O(1) slot-id -> admitted index authority (built once).
    bool slot_index_ok = false;
    const auto slot_index = build_slot_index(st->admitted_slots, slot_index_ok);
    if (!slot_index_ok) {
        return nullptr; // impossible duplicate id (B1 already rejects); fail-closed
    }
    const auto payload_of = [&](PayloadSlotId id) -> const std::vector<std::uint8_t> * {
        const auto it = slot_index.find(id.value);
        if (it == slot_index.end()) {
            return nullptr;
        }
        return &st->admitted_slots[it->second].payload;
    };

    // Entry frame view: verbatim payload of entry_input_slot, viewed (not copied) from
    // the sole owner `admitted_slots`.
    const auto *entry = payload_of(st->record.entry_input_slot);
    if (entry == nullptr) {
        return nullptr;
    }
    st->entry_span = std::span<const std::uint8_t>(*entry);

    st->callsite_memo.assign(st->call_site_count, std::nullopt);
    st->callsite_arg_hash.assign(st->call_site_count, std::nullopt);

    // Per-occurrence memo authority. For every committed capability memo, Verified-decode
    // its result bytes against that call site's Result binding and record its arg_hash.
    // Repeated result_slot ids reuse the same authenticated bytes (O(1) lookup) but each
    // occurrence still runs its own binding decode.
    for (const ResumeNode &node : st->record.nodes) {
        if (node.node_kind != core_wasm_resume::NodeKind::Capability) {
            continue;
        }
        if (node.schedule_pos >= st->schedule_to_callsite.size()) {
            return nullptr;
        }
        const auto cs_index = st->schedule_to_callsite[node.schedule_pos];
        if (!cs_index.has_value()) {
            return nullptr;
        }
        const std::size_t j = *cs_index;
        auto cs = st->module.resolve(ManifestCallSiteIndex{j});
        if (!cs.ok()) {
            return nullptr;
        }
        if (!node.memo.empty()) {
            const ResumeMemoEntry &m = node.memo[0];
            const auto *payload = payload_of(m.result_slot);
            if (payload == nullptr) {
                return nullptr;
            }
            auto dom = json::parse_json(std::string_view(
                reinterpret_cast<const char *>(payload->data()), payload->size()));
            if (!dom.has_value()) {
                payload_invalid = true;
                return nullptr;
            }
            auto decoded = wire_codec::decode_json(**dom, cs.call_site->result_binding());
            if (!decoded.ok()) {
                payload_invalid = true;
                return nullptr;
            }
            st->callsite_memo[j] = std::span<const std::uint8_t>(*payload);
            st->callsite_arg_hash[j] = m.arg_hash;
        } else if (node.pending.has_value()) {
            st->callsite_arg_hash[j] = node.pending->arg_hash;
        }
    }

    // Frontier call-site index = the frontier (last) node's call site.
    const std::size_t frontier_sched = st->record.nodes.back().schedule_pos;
    if (frontier_sched >= st->schedule_to_callsite.size()) {
        return nullptr;
    }
    const auto frontier_cs = st->schedule_to_callsite[frontier_sched];
    if (!frontier_cs.has_value()) {
        return nullptr;
    }
    st->frontier_call_site = *frontier_cs;
    st->replay_cursor = 0;
    return st;
}

// Verified-decode an injected result against the frontier call-site's Result binding.
[[nodiscard]] bool injected_result_valid(const detail::PreparedState &st,
                                         std::span<const std::uint8_t> injected) {
    auto cs = st.module.resolve(ManifestCallSiteIndex{st.frontier_call_site});
    if (!cs.ok()) {
        return false;
    }
    auto dom = json::parse_json(std::string_view(
        reinterpret_cast<const char *>(injected.data()), injected.size()));
    if (!dom.has_value()) {
        return false;
    }
    auto decoded = wire_codec::decode_json(**dom, cs.call_site->result_binding());
    return decoded.ok();
}

// The two-pass TOTAL linear-memory preflight. Pass 1 scans every future-live Result
// binding (call sites STRICTLY after the frontier) recording Unbounded / SizeOverflow
// (Unbounded has priority, decided before any arithmetic). Pass 2 checked-u64 sums the
// event region heap_base (over module.node_count()) + entry actual + each committed memo
// occurrence actual + (the injected result actual, if any) + each future-live bound,
// then applies the u32 host-transfer-domain gate AND the supplied capacity verdict.
[[nodiscard]] std::optional<ResumePrepareReason>
total_preflight(const detail::PreparedState &st, LinearMemoryCapacityBytes capacity,
                std::optional<std::uint64_t> injected_actual) {
    // Pass 1: boundedness of every future-live call site's Result binding. Scan ALL
    // future-live bindings recording both flags, then select Unbounded-first (D1b0
    // all-binding scan; no early return on the first Unbounded).
    const std::size_t live_start = st.frontier_call_site + 1;
    std::vector<std::uint64_t> live_bounds;
    live_bounds.reserve(st.call_site_count);
    bool seen_unbounded = false;
    bool seen_size_overflow = false;
    for (std::size_t i = live_start; i < st.call_site_count; ++i) {
        auto cs = st.module.resolve(ManifestCallSiteIndex{i});
        if (!cs.ok()) {
            return ResumePrepareReason::CoordinateMismatch;
        }
        const auto bound =
            core_wire_canonical_size::max_canonical_json_size(cs.call_site->result_binding());
        if (!bound.has_value()) {
            if (bound.error() == core_wire_canonical_size::MaxCanonicalSizeError::Unbounded) {
                seen_unbounded = true;
            } else {
                seen_size_overflow = true;
            }
            continue;
        }
        live_bounds.push_back(*bound);
    }
    if (seen_unbounded) {
        return ResumePrepareReason::Unbounded; // priority over any size overflow
    }
    if (seen_size_overflow) {
        return ResumePrepareReason::ResourceExhausted;
    }

    // Pass 2: checked-u64 total. heap_base is the sole runtime layout authority and is
    // computed over the WHOLE module's node count (the module's actual event region).
    const auto heap_base = core_wasm_node_events::event_region_heap_base(st.module.node_count());
    if (!heap_base.has_value()) {
        return ResumePrepareReason::ResourceExhausted;
    }
    std::uint64_t total = *heap_base;
    const auto checked_add = [&](std::uint64_t addend) noexcept -> bool {
        if (total > kU64Max - addend) {
            return false;
        }
        total += addend;
        return true;
    };
    if (!checked_add(st.entry_span.size())) {
        return ResumePrepareReason::ResourceExhausted;
    }
    for (const auto &memo : st.callsite_memo) {
        if (memo.has_value() && !checked_add(memo->size())) {
            return ResumePrepareReason::ResourceExhausted;
        }
    }
    if (injected_actual.has_value() && !checked_add(*injected_actual)) {
        return ResumePrepareReason::ResourceExhausted;
    }
    for (const std::uint64_t b : live_bounds) {
        if (!checked_add(b)) {
            return ResumePrepareReason::ResourceExhausted;
        }
    }
    // The reservation must fit the wasm32/u32 host-transfer domain even if the caller
    // supplied a larger capacity, then it must fit the supplied capacity.
    if (total > kU32Max || total > capacity.value) {
        return ResumePrepareReason::ResourceExhausted;
    }
    return std::nullopt;
}

// ---- per-import join helpers -----------------------------------------------

// Re-decode + join the published node-event prefix against the manifest. `decode_node_events`
// runs over module.node_count(); any decoder failure is EventMalformed. A valid prefix
// whose count differs from `expected_count`, or any manifest join / kind mismatch, is
// CoordinateMismatch. All joins are O(1) via the precomputed schedule->call-site map.
[[nodiscard]] std::optional<ResumeStepReason>
event_join(const detail::PreparedState &st, std::span<const std::uint8_t> whole_memory,
           std::size_t expected_count) {
    auto decoded = core_wasm_node_events::decode_node_events(whole_memory, st.module.node_count());
    if (!decoded.has_value()) {
        return ResumeStepReason::EventMalformed;
    }
    const auto &recs = *decoded;
    if (recs.size() != expected_count) {
        return ResumeStepReason::CoordinateMismatch;
    }
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const auto &r = recs[i];
        const auto node = st.module.resolve_node(ManifestNodeIndex{i});
        if (!node.ok()) {
            return ResumeStepReason::CoordinateMismatch;
        }
        const bool manifest_is_cap = node.node->cap_call_count() == kCapabilityCallCount;
        const bool record_is_cap = r.kind == core_wasm_resume::NodeKind::Capability;
        if (manifest_is_cap != record_is_cap) {
            return ResumeStepReason::CoordinateMismatch;
        }
        if (r.workflow_node_id != node.node->workflow_node_id() ||
            r.schedule_pos != ManifestNodeIndex{i}) {
            return ResumeStepReason::CoordinateMismatch;
        }
        if (record_is_cap) {
            const auto cs_index = st.schedule_to_callsite[i];
            if (!cs_index.has_value()) {
                return ResumeStepReason::CoordinateMismatch;
            }
            auto cs = st.module.resolve(ManifestCallSiteIndex{*cs_index});
            if (!cs.ok() || r.capability != cs.call_site->capability() ||
                r.source_symbol != cs.call_site->source_symbol() ||
                r.invocation_ordinal != cs.call_site->invocation_ordinal()) {
                return ResumeStepReason::CoordinateMismatch;
            }
        }
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// PHASE 1.
// ---------------------------------------------------------------------------
std::expected<GatedResume, ResumePrepareError>
open_gated_resume(const VerifiedCoreWasmSchemaModule &module,
                  payload_store::ResumeSnapshot &&snapshot, GatedResumeOptions options) {
    const CoreWasmResumeRecord &record = snapshot.record();
    if (const auto d = digest_gate(record, module)) {
        return std::unexpected(ResumePrepareError{*d});
    }
    // Build the schedule->call-site map ONCE (the single join authority carried forward).
    auto sched_map = build_schedule_map(module);
    if (!sched_map.has_value()) {
        return std::unexpected(ResumePrepareError{ResumePrepareReason::CoordinateMismatch});
    }
    if (const auto c = coordinate_gate(record, module, *sched_map)) {
        return std::unexpected(ResumePrepareError{*c});
    }
    // The checkpoint namespace is the AUTHENTICATED manifest's, not the caller's
    // option; the store already rejected any wf/ckpt disagreement at open_snapshot.
    DedupContext dedup;
    dedup.authority_id = options.authority_id;
    dedup.workflow = snapshot.workflow_id();
    dedup.checkpoint = snapshot.checkpoint_id();
    dedup.authority = options.dedup_authority;
    return GatedResume(std::make_unique<detail::GatedState>(
        module, std::move(snapshot), std::move(*sched_map), std::move(dedup)));
}

// ---------------------------------------------------------------------------
// PHASE 2.
// ---------------------------------------------------------------------------
std::expected<AdmitOutcome, ResumePrepareError>
admit_and_preflight(GatedResume &&gated, std::span<const std::uint8_t, 16> key_id,
                    std::span<const std::uint8_t> key,
                    std::span<const std::uint8_t> injected_or_empty,
                    LinearMemoryCapacityBytes capacity) {
    std::unique_ptr<detail::GatedState> gstate = std::move(gated.state_);
    if (gstate == nullptr) {
        return std::unexpected(ResumePrepareError{ResumePrepareReason::TransitionInvalid});
    }
    const ResumeState resume_state = gstate->snapshot.record().resume_state;
    const bool has_injected = !injected_or_empty.empty();

    // PHASE 2 admission (one-shot): admit the exact slot set on the pinned fd.
    auto admitted = std::move(gstate->snapshot).admit_slots(key_id, key);
    if (!admitted.has_value()) {
        return std::unexpected(ResumePrepareError{admitted.error()});
    }
    // Stored-memo per-occurrence Verified decode happens inside build_prepared; a decode
    // fault (PayloadSchemaInvalid) is reported BEFORE the input-matrix / eligibility gate.
    bool payload_invalid = false;
    auto prepared =
        build_prepared(gstate->module, std::move(gstate->schedule_to_callsite),
                       std::move(*admitted), std::move(gstate->dedup), payload_invalid);
    if (prepared == nullptr) {
        return std::unexpected(ResumePrepareError{payload_invalid
                                                      ? ResumePrepareReason::PayloadSchemaInvalid
                                                      : ResumePrepareReason::CoordinateMismatch});
    }

    // Input-matrix / transition eligibility (AFTER admission + stored-memo decode): an
    // Injected record admits NO new input.
    if (resume_state == ResumeState::Injected && has_injected) {
        return std::unexpected(ResumePrepareError{ResumePrepareReason::TransitionInvalid});
    }

    if (resume_state == ResumeState::Suspended && !has_injected) {
        // Await the injected result. Hold the admitted authority + capacity.
        auto pending = std::make_unique<detail::PendingState>();
        pending->capacity = capacity;
        pending->prepared = std::move(prepared);
        return AdmitOutcome{detail::PendingState::wrap(std::move(pending))};
    }

    // Suspended + injected: Verified-decode the injection, store its bytes, keep the
    // record Suspended (the durable transition happens later via the publish handshake),
    // then run the FULL two-pass TOTAL preflight counting the injected actual length.
    std::optional<std::uint64_t> injected_actual;
    if (has_injected) {
        if (!injected_result_valid(*prepared, injected_or_empty)) {
            return std::unexpected(ResumePrepareError{ResumePrepareReason::PayloadSchemaInvalid});
        }
        prepared->injected_result_bytes.assign(injected_or_empty.begin(), injected_or_empty.end());
        prepared->has_injected_result = true;
        injected_actual = static_cast<std::uint64_t>(injected_or_empty.size());
    }

    if (const auto r = total_preflight(*prepared, capacity, injected_actual)) {
        return std::unexpected(ResumePrepareError{*r});
    }
    prepared->phase = Phase::Replaying;
    return AdmitOutcome{detail::PreparedState::wrap(std::move(prepared))};
}

std::expected<PreparedResume, ResumePrepareError>
supply_injected_result(PendingInjection &&pending, std::span<const std::uint8_t> injected_bytes) {
    std::unique_ptr<detail::PendingState> pstate = std::move(pending.state_);
    if (pstate == nullptr || pstate->prepared == nullptr) {
        return std::unexpected(ResumePrepareError{ResumePrepareReason::TransitionInvalid});
    }
    detail::PreparedState &st = *pstate->prepared;

    // Verified-decode the injected result against the frontier Result binding.
    if (!injected_result_valid(st, injected_bytes)) {
        return std::unexpected(ResumePrepareError{ResumePrepareReason::PayloadSchemaInvalid});
    }
    st.injected_result_bytes.assign(injected_bytes.begin(), injected_bytes.end());
    st.has_injected_result = true;

    // FULL two-pass TOTAL preflight, counting the injected result's actual length.
    const auto injected_actual = static_cast<std::uint64_t>(injected_bytes.size());
    if (const auto r = total_preflight(st, pstate->capacity, injected_actual)) {
        return std::unexpected(ResumePrepareError{*r});
    }
    st.phase = Phase::Replaying;
    return detail::PreparedState::wrap(std::move(pstate->prepared));
}

// ---------------------------------------------------------------------------
// Per-import decision.
// ---------------------------------------------------------------------------

namespace {

// D2b-4: the SHA-256 of one arity-1 Param's canonical typed bytes. The canonical
// byte form is the codebase's canonical wire-JSON SSOT (`value_to_json`, the
// same deterministic serialization `hash_values` length-delimits): it is what a
// future host seals and what this controller consults under, so there is exactly
// one canonicalization authority.
[[nodiscard]] support::Sha256Digest canonical_param_digest(const evaluator::Value &param) {
    const std::string canonical = evaluator::value_to_json(param);
    return support::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(canonical.data()),
                                      canonical.size()));
}

// D2b-4 READ-ONLY dedup consultation at one AFTER-frontier call site. With no
// authority bound this is the legacy verdict (ReadyForLive). With an authority,
// mint the D0 token from the AUTHENTICATED namespace + host-bound authority id +
// call-site coordinate + canonical-param digest and ask the authority for a
// preview (seals NOTHING). A recorded terminal/pending verdict suppresses the
// live command; a same-site param-digest divergence fails closed as
// CoordinateMismatch; a backend storage fault is carried verbatim. Every
// returned decision leaves the caller responsible for the AwaitingLiveResult
// transition; this helper never touches the phase.
[[nodiscard]] std::expected<ImportStepDecision, ResumeStepError>
consult_live_dedup(const DedupContext &dedup,
                   const core_wasm_schema_module::VerifiedCoreWasmCallSite &call_site,
                   std::uint64_t arg_hash, std::vector<evaluator::Value> params) {
    namespace dea = durable_effect_authority;
    namespace dei = durable_effect_intent;

    ReadyForLive live{call_site, arg_hash, std::move(params)};
    if (dedup.authority == nullptr) {
        return ImportStepDecision{std::move(live)};
    }
    // Arity-1 is enforced by next_import's decode path.
    const support::Sha256Digest param_digest = canonical_param_digest(live.params.front());

    dei::FrozenAuthorityNamespaceBuilder builder;
    if (!builder
             .bind(dedup.authority_id,
                   dei::CheckpointNamespace{dedup.workflow, dedup.checkpoint})
             .has_value()) {
        // The namespace ids come from the authenticated manifest (validated at
        // open_snapshot) and the authority id is bound once: a bind failure is
        // an internal fault, fail closed.
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    dei::IntentCoordinate coordinate;
    coordinate.checkpoint_namespace = {dedup.workflow, dedup.checkpoint};
    coordinate.node = call_site.workflow_node_id();
    coordinate.ordinal = call_site.invocation_ordinal();
    coordinate.capability = call_site.capability();
    coordinate.source_symbol = call_site.source_symbol();
    coordinate.param_digest = param_digest;
    auto minted = builder.mint(coordinate);
    if (!minted.has_value()) {
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }

    auto preview = dedup.authority->preview_begin(*minted);
    if (!preview.has_value()) {
        // Real backend storage fault (unavailable/corrupt): carry it verbatim
        // so the host fails closed into reconciliation, never as a live command.
        return std::unexpected(ResumeStepError{preview.error()});
    }
    using ConsultResult = std::expected<ImportStepDecision, ResumeStepError>;
    return std::visit(
        Overloaded{
            [&live](dea::PreviewFresh) -> ConsultResult {
                return ImportStepDecision{std::move(live)};
            },
            [](dea::PreviewReplayPending p) -> ConsultResult {
                return ImportStepDecision{RecoverPending{p.token}};
            },
            [](dea::PreviewReplaySucceeded p) -> ConsultResult {
                return ImportStepDecision{DedupReplay{p.token, p.handle}};
            },
            [](dea::PreviewReplayFailed p) -> ConsultResult {
                return ImportStepDecision{DedupReplayFailure{p.token, p.handle}};
            },
            [](dea::PreviewDiverged) -> ConsultResult {
                return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
            },
        },
        *preview);
}

} // namespace

std::expected<ImportStepDecision, ResumeStepError>
next_import(PreparedResume &prepared, const ImportStepInput &input) {
    detail::PreparedState &st = detail::PreparedState::unwrap(prepared);
    if (st.phase != Phase::Replaying) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    // Cursor authority: the expected call site is the one at the current cursor.
    if (st.replay_cursor >= st.call_site_count) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
    }
    auto expected = st.module.resolve(ManifestCallSiteIndex{st.replay_cursor});
    if (!expected.ok()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
    }
    // The module's observed import ordinal must match the expected call site's.
    if (expected.call_site->import_ordinal() != input.observed_ordinal) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
    }

    // Event join: the published prefix must be dense up to this call site's schedule pos.
    const std::size_t expected_sched = expected.call_site->schedule_pos().value;
    if (const auto e = event_join(st, input.whole_linear_memory, expected_sched)) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{*e});
    }

    // Param Verified-decode + arity-1 wire-canonical hash (before any branch).
    auto dom = json::parse_json(std::string_view(
        reinterpret_cast<const char *>(input.module_param_frame.data()),
        input.module_param_frame.size()));
    if (!dom.has_value()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::PayloadSchemaInvalid});
    }
    auto decoded = wire_codec::decode_json(**dom, expected.call_site->param_binding());
    if (!decoded.ok()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::PayloadSchemaInvalid});
    }
    std::vector<evaluator::Value> params;
    params.push_back(std::move(*decoded.value));
    const std::uint64_t arg_hash = evaluator::hash_values(params);

    const std::size_t j = st.replay_cursor;
    const bool below_frontier = j < st.frontier_call_site;
    const bool at_frontier = j == st.frontier_call_site;

    if (below_frontier || (at_frontier && st.record.resume_state == ResumeState::Injected)) {
        // Memoized: the replayed args must match the ledgered invocation coordinate.
        if (!st.callsite_arg_hash[j].has_value() || *st.callsite_arg_hash[j] != arg_hash ||
            !st.callsite_memo[j].has_value()) {
            st.phase = Phase::Failed;
            return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
        }
        st.replay_cursor += 1;
        return ImportStepDecision{ReturnMemo{std::span<const std::uint8_t>(*st.callsite_memo[j])}};
    }

    if (at_frontier) { // Suspended frontier: needs an injected slot bound.
        if (!st.callsite_arg_hash[j].has_value() || *st.callsite_arg_hash[j] != arg_hash) {
            st.phase = Phase::Failed;
            return std::unexpected(ResumeStepError{ResumeStepReason::CoordinateMismatch});
        }
        if (!st.has_injected_result) {
            st.phase = Phase::Failed;
            return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
        }
        st.phase = Phase::AwaitingSlot;
        return ImportStepDecision{NeedInjectedSlot{}};
    }

    // After the frontier: D2b-4 token-aware decision. With no authority bound
    // this is the legacy ReadyForLive verdict; with one, a READ-ONLY authority
    // preview may instead suppress the live call (DedupReplay /
    // DedupReplayFailure / RecoverPending) or fail closed on a same-site
    // param-digest divergence / backend fault. Every verdict (live or replay)
    // parks in AwaitingLiveResult -- the follow-on live-response API is blocked.
    auto verdict = consult_live_dedup(st.dedup, *expected.call_site, arg_hash, std::move(params));
    if (!verdict.has_value()) {
        st.phase = Phase::Failed;
        return std::unexpected(verdict.error());
    }
    st.phase = Phase::AwaitingLiveResult;
    return verdict;
}

// ---------------------------------------------------------------------------
// Publish handshake.
// ---------------------------------------------------------------------------
std::expected<PublishInjectedPlan, ResumeStepError>
bind_publish_injected(PreparedResume &prepared, PayloadSlotId chosen_slot) {
    detail::PreparedState &st = detail::PreparedState::unwrap(prepared);
    if (st.phase != Phase::AwaitingSlot) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    if (!st.has_injected_result) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    // Validate the chosen slot: non-invalid, distinct from the entry slot and every
    // existing memo result slot.
    if (chosen_slot.value == PayloadSlotId::kInvalid || chosen_slot == st.entry_slot) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    for (const ResumeNode &node : st.record.nodes) {
        for (const ResumeMemoEntry &memo : node.memo) {
            if (memo.result_slot == chosen_slot) {
                st.phase = Phase::Failed;
                return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
            }
        }
    }

    // Build the updated record: Suspended -> Injected; promote the frontier pending entry
    // to an ordinal-0 memo bound to the chosen slot; clear pending.
    CoreWasmResumeRecord updated = st.record;
    updated.resume_state = ResumeState::Injected;
    ResumeNode &fnode = updated.nodes.back();
    if (!fnode.pending.has_value() || !fnode.memo.empty()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    ResumeMemoEntry entry;
    entry.invocation_ordinal = fnode.pending->invocation_ordinal;
    entry.capability = fnode.pending->capability;
    entry.source_symbol = fnode.pending->source_symbol;
    entry.arg_hash = fnode.pending->arg_hash;
    entry.result_slot = chosen_slot;
    fnode.memo.push_back(entry);
    fnode.pending.reset();

    // Build the exact distinct slot set: the admitted distinct set (already deduplicated
    // by id) + {chosen: real injected bytes}. All spans point into controller-owned
    // storage stable at least until the ACK.
    st.pending_publish_slots.clear();
    st.pending_publish_slots.reserve(st.admitted_slots.size() + 1);
    for (const auto &s : st.admitted_slots) {
        st.pending_publish_slots.push_back(
            payload_store::Slot{s.slot, std::span<const std::uint8_t>(s.payload)});
    }
    st.pending_publish_slots.push_back(payload_store::Slot{
        chosen_slot, std::span<const std::uint8_t>(st.injected_result_bytes)});

    st.pending_publish_record = std::move(updated);
    st.pending_expected_generation = st.current_generation;
    st.phase = Phase::AwaitingPublishAck;
    return st.make_publish_plan(st.pending_expected_generation, &*st.pending_publish_record,
                                std::span<const payload_store::Slot>(st.pending_publish_slots));
}

std::expected<std::span<const std::uint8_t>, ResumeStepError>
ack_publish_injected(PreparedResume &prepared,
                     std::expected<std::uint64_t, payload_store::PayloadStoreError> store_result) {
    detail::PreparedState &st = detail::PreparedState::unwrap(prepared);
    if (st.phase != Phase::AwaitingPublishAck) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    if (!store_result.has_value()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{store_result.error()});
    }
    const std::uint64_t m = *store_result;
    const std::uint64_t n = st.pending_expected_generation;
    if (n == kU64Max || m != n + 1) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    // Commit: adopt the updated record + generation; the frontier call site's memo is now
    // the injected result (stable controller-owned buffer); advance the cursor past the
    // frontier; return to Replaying. The returned span is stable for the handle lifetime.
    st.record = std::move(*st.pending_publish_record);
    st.pending_publish_record.reset();
    st.pending_publish_slots.clear();
    st.current_generation = m;
    const std::size_t j = st.frontier_call_site;
    st.callsite_memo[j] = std::span<const std::uint8_t>(st.injected_result_bytes);
    st.callsite_arg_hash[j] = st.record.nodes.back().memo.back().arg_hash;
    const std::span<const std::uint8_t> injected_span(*st.callsite_memo[j]);
    st.replay_cursor = j + 1;
    st.phase = Phase::Replaying;
    return injected_span;
}

// ---------------------------------------------------------------------------
// Terminal.
// ---------------------------------------------------------------------------
std::expected<MarkConsumedPlan, ResumeStepError>
finish_run(PreparedResume &prepared, const Run2Exit &exit) {
    detail::PreparedState &st = detail::PreparedState::unwrap(prepared);
    if (st.phase != Phase::Replaying) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    if (std::holds_alternative<Run2Trapped>(exit)) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::ModuleTrap});
    }
    const auto &ret = std::get<Run2Returned>(exit);
    // ABI status classification (fail-closed: any unknown raw status is an error).
    if (ret.raw_status == AHFL_CAP_PENDING) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    if (ret.raw_status != AHFL_CAP_OK) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::ModuleError});
    }
    // OK is legal ONLY after every expected import decision was observed: the replay
    // cursor must have advanced through every call site. An immediate or partial finish
    // is an ineligible transition.
    if (st.replay_cursor != st.call_site_count) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    // OK: the whole published prefix must join the manifest for EVERY node.
    if (const auto e = event_join(st, ret.whole_linear_memory, st.module.node_count())) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{*e});
    }
    st.consume_expected_generation = st.current_generation;
    st.phase = Phase::AwaitingConsumedAck;
    return st.make_mark_consumed_plan(st.consume_expected_generation);
}

std::expected<void, ResumeStepError>
ack_mark_consumed(PreparedResume &prepared,
                  std::expected<std::uint64_t, payload_store::PayloadStoreError> store_result) {
    detail::PreparedState &st = detail::PreparedState::unwrap(prepared);
    if (st.phase != Phase::AwaitingConsumedAck) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    if (!store_result.has_value()) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{store_result.error()});
    }
    const std::uint64_t m = *store_result;
    const std::uint64_t n = st.consume_expected_generation;
    if (n == kU64Max || m != n + 1) {
        st.phase = Phase::Failed;
        return std::unexpected(ResumeStepError{ResumeStepReason::TransitionInvalid});
    }
    st.current_generation = m;
    st.phase = Phase::Consumed;
    return {};
}

} // namespace ahfl::runtime::core_wasm_resume_controller
