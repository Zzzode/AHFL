#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"
#include "base/support/atomic_file.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

struct ExecutionCheckpointProjection;
struct WorkflowResult;

inline constexpr std::string_view kWorkflowRecoverySchema{"ahfl.workflow-recovery.v1"};

// RFC 0022 (durable resume): schema v2 adds the resume record — a suspended
// node's input Value plus the memo table of capability results already produced
// within that node — so a workflow that suspended on an AHFL_CAP_PENDING
// capability call can be re-run deterministically from the node input, replaying
// completed capability calls from the memo instead of re-invoking them.
inline constexpr std::string_view kWorkflowRecoverySchemaV2{"ahfl.workflow-recovery.v2"};

struct RecoveredNodeState {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<evaluator::Value> output{};
};

// One completed capability call within a suspended node, keyed by its per-node
// invocation ordinal (RFC 0022 §Design). ordinal is the memo key; cap_id +
// arg_hash are redundant integrity cross-checks asserted on each replay hit
// (mismatch => fail-closed, never a live re-invoke). All identity is index/id-
// based (Principle 2): cap_id is a capability SymbolId, ordinal an InvocationId
// ordinal, never a name.
struct CapabilityMemoEntry {
    std::uint64_t ordinal{0};       // per-node invocation ordinal (memo key)
    std::size_t cap_id{0};          // capability SymbolId (integrity cross-check)
    std::uint64_t arg_hash{0};      // hash of resolved args (integrity cross-check)
    evaluator::Value result{};      // the memoized capability result
};

// The resume record for a node suspended on a pending capability call. Captures
// the minimal deterministic-replay state: the node coordinate, its input Value
// (NOT captured by v1, which only stored output), the pending call's identity,
// and the append-only memo table of all completed calls in the node so far.
struct SuspendedNodeState {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<evaluator::Value> node_input{};        // v2: replay re-runs from this
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

} // namespace ahfl::runtime
