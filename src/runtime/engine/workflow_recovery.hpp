#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"
#include "base/support/atomic_file.hpp"
#include "runtime/value/value.hpp"

namespace ahfl::runtime {

struct ExecutionCheckpointProjection;
struct WorkflowResult;

inline constexpr std::string_view kWorkflowRecoverySchema{"ahfl.workflow-recovery.v1"};

// RFC 0022 (durable resume): schema v2 adds the resume record — a suspended
// node's input Value plus the memo table of capability results already produced
// within that node — so a workflow that suspended on an AHFL_CAP_PENDING
// capability call can be re-run deterministically from the node input, replaying
// completed capability calls from the memo instead of re-invoking them.
//
// STABLE ARTIFACT (RFC 0022 stabilized): v2 resume records are backward-compatible.
// A persisted v2 snapshot MUST stay loadable by future runtime versions. Schema
// evolution is append-only — new fields are optional and ignored by older readers;
// any breaking change requires a new schema version string, never a redefinition
// of v2. v1 remains an untouched subset; both load paths coexist.
inline constexpr std::string_view kWorkflowRecoverySchemaV2{"ahfl.workflow-recovery.v2"};

struct RecoveredNodeState {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<runtime::Value> output{};
};

// RFC 0026 C2b stage3 (P0-14/16/17/18): how a memo result's TRUST AUTHORITY is
// represented across persistence. A schema-free value_to_json/value_from_json
// round-trip loses type on Decimal/Duration/Set/Map/Option/Unit and cannot
// distinguish integral Float (persists as a bare int) or an explicit Unit from a
// valueless success (both spell JSON null). So a memo result carries its exact
// wire spelling out-of-band; the native `result` Value is a compatibility
// projection for old in-process readers, trusted ONLY under NativeOnly.
//
//  NativeOnly   — a programmatic / in-process entry (host-built or a freshly
//                 replay-decoded canonical Value). The native `result` IS the
//                 authority; authoritative_json is absent; result_present is set.
//  LegacyV2     — an old v2 snapshot that predates the sidecar. authoritative_json
//                 is the raw legacy `result` JSON substring; result_present is
//                 UNKNOWN (nullopt) until a per-ordinal binding decode resolves it.
//  ExactSidecar — a new v2 snapshot. authoritative_json is the sidecar
//                 `result_wire_json` (value_to_json bytes preserved verbatim);
//                 result_present is set (the persisted presence bit).
enum class PersistedMemoResultSource {
    NativeOnly,
    LegacyV2,
    ExactSidecar,
};

// One completed capability call within a suspended node, keyed by its per-node
// invocation ordinal (RFC 0022 §Design). ordinal is the memo key; cap_id +
// arg_hash are redundant integrity cross-checks asserted on each replay hit
// (mismatch => fail-closed, never a live re-invoke). All identity is index/id-
// based (Principle 2): cap_id is a capability SymbolId, ordinal an InvocationId
// ordinal, never a name.
//
// WH-4b (RFC 0026 KR6.8): the optional `node` coordinate generalizes the memo
// from a single suspended node to the WHOLE workflow. The evaluator suspends
// per node and its memo covers only the suspended node, so it never sets this
// field (every entry is nullopt == the suspended node, unchanged semantics).
// The wasm lane resumes on a FRESH instance that re-runs the whole module, so
// it must memo-supply capability calls from EVERY node before the pending one;
// it sets `node` on every entry. The wasm consumer REQUIRES it (an entry
// without `node` fails closed at load); the evaluator consumer ignores it.
// `node` is orthogonal to the result three-state (NativeOnly/LegacyV2/
// ExactSidecar): it records WHERE the call happened, not how its result is
// trusted.
struct CapabilityMemoEntry {
    std::uint64_t ordinal{0};       // per-node invocation ordinal (memo key)
    std::size_t cap_id{0};          // capability SymbolId (integrity cross-check)
    std::uint64_t arg_hash{0};      // hash of resolved args (integrity cross-check)
    runtime::Value result{};      // native compat projection (authority iff NativeOnly)
    // RFC 0026 C2b stage3 result trust-authority state (see PersistedMemoResultSource).
    PersistedMemoResultSource source{PersistedMemoResultSource::NativeOnly};
    std::optional<std::string> authoritative_json{}; // exact wire spelling (Legacy/Sidecar)
    std::optional<bool> result_present{true};         // presence bit (P0-17/18); nullopt iff LegacyV2
    // WH-4b: the workflow node this call belongs to. nullopt == the suspended
    // node (evaluator per-node memo semantics, unchanged). The wasm lane always
    // sets it (whole-workflow memo for fresh-instance replay).
    std::optional<WorkflowNodeId> node{};
};

// P0-18 three-state well-formedness (structural only — parseability of
// authoritative_json is verified where it is parsed). Enforced fail-closed at the
// real save / load / consume entry points, NOT merely by a construction helper
// (CapabilityMemoEntry is a public aggregate any caller can hand-build):
//  NativeOnly   => authoritative_json ABSENT, result_present SET
//  LegacyV2     => authoritative_json PRESENT (non-empty), result_present ABSENT
//  ExactSidecar => authoritative_json PRESENT (non-empty), result_present SET
//
// WH-4b: the optional `node` coordinate is orthogonal to this three-state — it
// records WHERE the call happened (whole-workflow memo for the wasm lane), not
// how its result is trusted. The well-formedness gate does not inspect `node`.
[[nodiscard]] bool memo_result_state_well_formed(const CapabilityMemoEntry &entry);

// The resume record for a node suspended on a pending capability call. Captures
// the minimal deterministic-replay state: the node coordinate, its input Value
// (NOT captured by v1, which only stored output), the pending call's identity,
// and the append-only memo table of all completed calls in the node so far.
struct SuspendedNodeState {
    WorkflowNodeId node;
    AgentId agent;
    // v2 PERSISTS the node input for format completeness / potential observation,
    // but the current WorkflowRuntime does NOT use it as a trust authority and does
    // NOT restore execution from it: on resume the node-input expression is
    // RE-EVALUATED and its capability calls are replayed from `memo`. This field is
    // therefore informational today; treating it as authoritative would be a future
    // change (and a residual risk if a reader assumes it drives resume). See the
    // node_input_snapshot capture site in workflow_runtime.cpp.
    std::optional<runtime::Value> node_input{};
    std::size_t pending_cap_id{0};                       // capability SymbolId of the pending call
    std::uint64_t pending_ordinal{0};                    // its per-node invocation ordinal
    std::vector<CapabilityMemoEntry> memo{};             // append-only, sorted by ordinal
};

struct WorkflowRecoverySnapshot {
    WorkflowId workflow;
    CheckpointId checkpoint;
    std::vector<RecoveredNodeState> completed_nodes{};
    // RFC 0022 v2: present iff the workflow suspended on a pending capability
    // call. When set, resume re-runs `suspended->node` from its captured input,
    // replaying `suspended->memo` for completed calls and continuing past the
    // pending call once the host supplies its result.
    std::optional<SuspendedNodeState> suspended{};
};

enum class WorkflowRecoveryError {
    Missing,
    InvalidSnapshot,
    ReadFailed,
    WriteFailed,
};

class WorkflowRecoveryStore final {
  public:
    explicit WorkflowRecoveryStore(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path &path() const noexcept;

    [[nodiscard]] std::expected<void, WorkflowRecoveryError>
    save(const WorkflowRecoverySnapshot &snapshot,
         const support::AtomicReplaceOptions &options = {}) const;

    [[nodiscard]] std::expected<WorkflowRecoverySnapshot, WorkflowRecoveryError> load() const;

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::expected<WorkflowRecoverySnapshot, WorkflowRecoveryError>
materialize_workflow_recovery_snapshot(const WorkflowResult &result,
                                       const ExecutionCheckpointProjection &checkpoint);

// RFC 0022 slice 4 (exactly-once): a stable per-invocation idempotency key. The
// same FNV-1a mix used for arg hashing, folded over the invocation coordinate.
// Must be reproducible across resume — every input is index/id-based (workflow,
// node, the stable per-node ordinal, capability SymbolId) plus the
// resolved-argument hash — so a host can dedup a durable_write effect that
// committed before a crash. Shared by the evaluator and the wasm lane (WH-4b)
// so a host sees the same key for the same invocation on either engine.
[[nodiscard]] std::uint64_t compute_idempotency_key(std::size_t workflow_index,
                                                    std::size_t node_index,
                                                    std::uint64_t ordinal,
                                                    std::size_t cap_symbol_id,
                                                    std::uint64_t arg_hash);

} // namespace ahfl::runtime
