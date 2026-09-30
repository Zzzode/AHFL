#pragma once

// RFC 0026 KR6.8 WH-4b: the session-local suspended-snapshot recorder.
//
// Origination (suspend run): the workflow session wraps the WH-3 capability
// import callback with this recorder. At each opaque import the wrapper
// derives (node, ordinal) from the call site and opens a current-import
// correlation; the wrapped invoker fills in arg_hash + result; the wrapper
// reads the exact reply wire bytes from guest memory and appends an
// ExactSidecar memo entry. A Pending reply stamps the pending coordinate so
// the session can build a SuspendedNodeState (node_input is nullopt on the
// wasm lane: the in-guest materialized input is not host-observable).
//
// Replay (resume run): the session loads the recovery snapshot into the
// recorder (memo index + frontier). At each import the wrapper classifies
// the coordinate (memo hit / frontier / post-frontier live / pre-frontier
// miss) and serves verbatim, injects, or fails closed -- the invoker is
// never called for a memo hit or the frontier, so a resumed run has zero
// live side effects before the pending call.
//
// The recorder is a state holder, not a decision authority: the session
// wrapper drives it and owns the engine / binding / diagnostic machinery.

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/value/value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ahfl::runtime::wasm_host {

class WasmResumeRecorder {
  public:
    // The pending-call coordinate stamped when an import returns Pending.
    // arg_hash is filled by the wrapped invoker from the decoded args.
    // schedule_pos is the node's dense position in the Kahn schedule (the
    // coordinate classification compares; see classify).
    struct PendingCoordinate {
        WorkflowNodeId node;
        std::size_t schedule_pos{0};
        std::uint64_t ordinal{0};
        std::size_t cap_id{0};
        std::uint64_t arg_hash{0};
    };

    // The current-import correlation. The import wrapper opens it before the
    // inner executor runs (node / schedule_pos / ordinal / cap_id); the
    // wrapped invoker fills arg_hash + result once the args are decoded and
    // the capability returns.
    struct CurrentImport {
        WorkflowNodeId node;
        std::size_t schedule_pos{0};
        std::uint64_t ordinal{0};
        std::size_t cap_id{0};
        std::uint64_t arg_hash{0};
        bool arg_hash_set{false};
        std::optional<Value> result;
        bool result_present{false};
        bool pending{false};
    };

    // Replay classification of an import coordinate on a resume run.
    enum class ReplayClass {
        MemoHit,        // (node, ordinal) is a recorded call before the frontier
        Frontier,       // (node, ordinal) == the pending call (inject here)
        PostFrontier,   // after the pending call (live; ReadyForLive)
        PreFrontierMiss // before the frontier but absent from the memo (divergence)
    };

    explicit WasmResumeRecorder(std::size_t node_count);

    // --- Origination (suspend run) ---

    // Open a current-import correlation and assign the per-node ordinal
    // (the memo key, reproducible across resume). Returns the ordinal.
    std::uint64_t begin_import(std::size_t schedule_pos, WorkflowNodeId node,
                               std::size_t cap_id);

    // The current import correlation (nullopt outside an import). The
    // wrapped invoker mutates it (arg_hash / result / pending).
    [[nodiscard]] CurrentImport *current_import() noexcept;

    // Append an ExactSidecar memo entry from the current import. Called by
    // the wrapper after a successful reply, with the exact wire bytes read
    // from guest memory (never re-serialized).
    void append_memo_entry(std::string authoritative_json);

    // Stamp the pending coordinate from the current import.
    void stamp_pending();

    [[nodiscard]] std::optional<PendingCoordinate> pending() const noexcept;

    // Move out the accumulated memo vector (sorted by ordinal per node).
    [[nodiscard]] std::vector<CapabilityMemoEntry> take_memo();

    // --- Replay (resume run) ---

    // Whether this recorder is in replay mode (load_replay succeeded). The
    // session wrapper uses this to decide whether to append memo entries
    // after a live call (origination only; replay post-frontier live calls
    // are not memoized).
    [[nodiscard]] bool is_replay() const noexcept { return replay_; }

    // Load a recovery snapshot's memo + frontier, taking ownership of the
    // memo vector (CapabilityMemoEntry is move-only: its native result Value
    // holds unique_ptr-backed alternatives). The session loads the snapshot
    // from the store and hands it over; the recorder becomes the replay
    // authority and the caller does not read it again. `node_to_schedule`
    // maps each WorkflowNodeId (dense source-order index) to its schedule
    // position; the recorder translates every persisted node coordinate to
    // schedule space ONCE here, so classify/memo_entry compare schedule
    // positions (the Kahn order the session actually walks), never source
    // order. Returns false (fail-closed) if any memo entry lacks a node
    // coordinate (the wasm consumer requires it on every entry; an
    // evaluator-originated snapshot has none and is rejected here, not
    // silently re-run) or a node coordinate is out of range for the
    // mapping.
    [[nodiscard]] bool
    load_replay(WorkflowRecoverySnapshot snapshot,
                std::span<const std::size_t> node_to_schedule);

    // Classify an import coordinate against the loaded memo + frontier.
    // schedule_pos is the node's dense position in the Kahn schedule (the
    // session derives it from the guest's node-event completion counter at
    // the import boundary), NOT the source-order WorkflowNodeId: the memo
    // hit / frontier / post-frontier decision must follow the order the
    // session actually walks (design 12.6.5 step 4: "nodes after the
    // suspended node in the schedule"), which can differ from declaration
    // order when a node depends on a later-declared node.
    [[nodiscard]] ReplayClass classify(std::size_t schedule_pos,
                                       std::uint64_t ordinal) const;

    // The memo entry for a MemoHit coordinate (nullptr on miss / wrong class).
    [[nodiscard]] const CapabilityMemoEntry *
    memo_entry(std::size_t schedule_pos, std::uint64_t ordinal) const;

    // The frontier (pending coordinate). nullptr when not a resume run.
    [[nodiscard]] const PendingCoordinate *frontier() const noexcept;

    // Mark the frontier injected (cleared; subsequent calls on the suspended
    // node are live).
    void mark_frontier_injected() noexcept;

    // Whether the frontier was ever hit. A resume run that completes without
    // hitting the frontier diverged (the fresh instance took a different
    // branch) and must fail closed.
    [[nodiscard]] bool frontier_was_hit() const noexcept;

  private:
    // Per-node import counters, indexed by dense schedule position.
    std::vector<std::uint64_t> per_node_counters_;
    // The accumulated memo (origination) or the loaded memo (replay).
    std::vector<CapabilityMemoEntry> memo_;
    // The stamped pending coordinate (origination).
    std::optional<PendingCoordinate> pending_;
    // The current-import correlation.
    std::optional<CurrentImport> current_;

    // Replay state (populated by load_replay).
    struct ReplayIndexEntry {
        std::size_t schedule_pos{0}; // Kahn schedule position (classification key)
        std::uint64_t ordinal{0};
        std::size_t memo_index{0}; // into memo_
    };
    std::vector<ReplayIndexEntry> replay_index_;
    std::optional<PendingCoordinate> frontier_;
    bool frontier_was_hit_{false};
    bool replay_{false};
};

} // namespace ahfl::runtime::wasm_host
