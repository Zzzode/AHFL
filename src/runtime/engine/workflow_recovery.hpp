#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"
#include "base/support/atomic_file.hpp"
#include "runtime/value/value.hpp"

namespace ahfl::runtime {

struct ExecutionCheckpointProjection;
struct WorkflowResult;

inline constexpr std::string_view kWorkflowRecoveryFormat{"ahfl.workflow-recovery"};

struct RecoveredNodeState {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<runtime::Value> output{};
};

struct InMemoryMemoResult {
    runtime::Value value{};
    bool present{true};
};
struct StoredMemoResult {
    std::string result_wire_json;
    bool present{true};
};
using MemoResult = std::variant<InMemoryMemoResult, StoredMemoResult>;

// The invocation coordinate is numeric. Stored results retain the exact wire
// spelling and are decoded only under the live capability's verified binding.
struct CapabilityMemoEntry {
    std::uint64_t ordinal{0};
    std::size_t cap_id{0};
    std::uint64_t arg_hash{0};
    MemoResult result{InMemoryMemoResult{}};
    WorkflowNodeId node{};
};

[[nodiscard]] bool memo_result_state_well_formed(const CapabilityMemoEntry &entry);

// The resume record for a node suspended on a pending capability call. Captures
// the node coordinate, informational input, pending invocation identity, and
// completed invocation results used by deterministic replay.
struct SuspendedNodeState {
    WorkflowNodeId node;
    AgentId agent;
    // The snapshot persists the node input for format completeness / potential observation,
    // but the wasm lane does NOT use it as a trust authority and does NOT restore
    // execution from it: on resume a FRESH wasm instance re-runs the whole module,
    // so the node-input expression is RE-DERIVED and capability calls up to the
    // pending one are replayed from `memo`. This field is therefore informational
    // today; treating it as authoritative would be a future change (and a residual
    // risk if a reader assumes it drives resume). See the capture site in
    // wasm_host/workflow_session.cpp.
    std::optional<runtime::Value> node_input{};
    std::size_t pending_cap_id{0};           // capability SymbolId of the pending call
    std::uint64_t pending_ordinal{0};        // its per-node invocation ordinal
    std::vector<CapabilityMemoEntry> memo{}; // append-only, sorted by ordinal
};

struct WorkflowRecoverySnapshot {
    WorkflowId workflow;
    CheckpointId checkpoint;
    std::vector<RecoveredNodeState> completed_nodes{};
    // A fresh module instance re-derives node input and replays completed calls
    // from memo before reaching the pending invocation.
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
// committed before a crash. The wasm lane (WH-4b) uses it so a host sees the
// same key for the same invocation across replay.
[[nodiscard]] std::uint64_t compute_idempotency_key(std::size_t workflow_index,
                                                    std::size_t node_index,
                                                    std::uint64_t ordinal,
                                                    std::size_t cap_symbol_id,
                                                    std::uint64_t arg_hash);

} // namespace ahfl::runtime
