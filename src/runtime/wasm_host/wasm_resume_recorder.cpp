// RFC 0026 KR6.8 WH-4b: the session-local suspended-snapshot recorder.
// See the header comment for the full contract.

#include "runtime/wasm_host/wasm_resume_recorder.hpp"

#include <algorithm>
#include <utility>

namespace ahfl::runtime::wasm_host {

WasmResumeRecorder::WasmResumeRecorder(std::size_t node_count)
    : per_node_counters_(node_count, 0) {}

std::uint64_t WasmResumeRecorder::begin_import(std::size_t schedule_pos,
                                               WorkflowNodeId node,
                                               std::size_t cap_id) {
    const std::uint64_t ordinal = per_node_counters_.at(schedule_pos)++;
    current_ = CurrentImport{
        .node = node,
        .schedule_pos = schedule_pos,
        .ordinal = ordinal,
        .cap_id = cap_id,
        .arg_hash = 0,
        .arg_hash_set = false,
        .result = std::nullopt,
        .result_present = false,
        .pending = false,
    };
    return ordinal;
}

WasmResumeRecorder::CurrentImport *
WasmResumeRecorder::current_import() noexcept {
    return current_ ? &*current_ : nullptr;
}

void WasmResumeRecorder::append_memo_entry(
    std::string authoritative_json,
    PersistedMemoResultSource source) {
    if (!current_.has_value() || !current_->arg_hash_set) {
        return; // state violation; the wrapper guards this
    }
    CapabilityMemoEntry entry{
        .ordinal = current_->ordinal,
        .cap_id = current_->cap_id,
        .arg_hash = current_->arg_hash,
        .result = current_->result.has_value()
                      ? runtime::clone_value(*current_->result)
                      : runtime::Value{runtime::NoneValue{}},
        .source = source,
        .result_present = current_->result_present,
        .node = current_->node,
    };
    if (source == PersistedMemoResultSource::ExactSidecar) {
        entry.authoritative_json = std::move(authoritative_json);
    }
    memo_.push_back(std::move(entry));
}

void WasmResumeRecorder::stamp_pending() {
    if (!current_.has_value()) {
        return;
    }
    pending_ = PendingCoordinate{
        .node = current_->node,
        .schedule_pos = current_->schedule_pos,
        .ordinal = current_->ordinal,
        .cap_id = current_->cap_id,
        .arg_hash = current_->arg_hash,
    };
}

std::optional<WasmResumeRecorder::PendingCoordinate>
WasmResumeRecorder::pending() const noexcept {
    return pending_;
}

std::vector<CapabilityMemoEntry> WasmResumeRecorder::take_memo() {
    return std::move(memo_);
}

bool WasmResumeRecorder::load_replay(
    WorkflowRecoverySnapshot snapshot,
    std::span<const std::size_t> node_to_schedule) {
    if (!snapshot.suspended.has_value()) {
        return false;
    }
    auto suspended = std::move(*snapshot.suspended);
    // Translate the suspended node's source-order WorkflowNodeId to its
    // schedule position ONCE (classification compares schedule positions).
    if (suspended.node.index() >= node_to_schedule.size()) {
        return false;
    }
    frontier_ = PendingCoordinate{
        .node = suspended.node,
        .schedule_pos = node_to_schedule[suspended.node.index()],
        .ordinal = suspended.pending_ordinal,
        .cap_id = suspended.pending_cap_id,
        .arg_hash = 0, // the pending record carries no arg_hash
    };
    memo_ = std::move(suspended.memo); // take ownership: the recorder is the replay authority
    replay_index_.clear();
    replay_index_.reserve(memo_.size());
    for (std::size_t i = 0; i < memo_.size(); ++i) {
        const auto &entry = memo_[i];
        // The wasm consumer requires a node coordinate on every memo entry
        // (whole-workflow memo for fresh-instance replay). An evaluator-
        // originated snapshot has none -> fail closed.
        if (!entry.node.has_value()) {
            return false;
        }
        if (entry.node->index() >= node_to_schedule.size()) {
            return false;
        }
        replay_index_.push_back(
            ReplayIndexEntry{.schedule_pos =
                                 node_to_schedule[entry.node->index()],
                             .ordinal = entry.ordinal,
                             .memo_index = i});
    }
    std::sort(replay_index_.begin(), replay_index_.end(),
              [](const ReplayIndexEntry &lhs, const ReplayIndexEntry &rhs) {
                  if (lhs.schedule_pos != rhs.schedule_pos) {
                      return lhs.schedule_pos < rhs.schedule_pos;
                  }
                  return lhs.ordinal < rhs.ordinal;
              });
    replay_ = true;
    return true;
}

WasmResumeRecorder::ReplayClass
WasmResumeRecorder::classify(std::size_t schedule_pos,
                             std::uint64_t ordinal) const {
    if (!frontier_.has_value()) {
        return ReplayClass::PostFrontier; // not a resume run: everything live
    }
    // Frontier hit?
    if (frontier_->schedule_pos == schedule_pos &&
        frontier_->ordinal == ordinal) {
        return ReplayClass::Frontier;
    }
    // Before the frontier (same node, lower ordinal; or a node that precedes
    // the suspended node in SCHEDULE order)? The comparison is on schedule
    // positions, not source-order WorkflowNodeIds: the Kahn schedule can
    // differ from declaration order (design 12.6.5 step 4), and a memoized
    // call on an earlier-EXECUTED node with a higher source id must still be
    // a MemoHit, not a PostFrontier live re-execution.
    const bool before_frontier =
        schedule_pos < frontier_->schedule_pos ||
        (schedule_pos == frontier_->schedule_pos &&
         ordinal < frontier_->ordinal);
    if (!before_frontier) {
        return ReplayClass::PostFrontier;
    }
    // Before the frontier: must be a memo hit, else divergence.
    return memo_entry(schedule_pos, ordinal) != nullptr
               ? ReplayClass::MemoHit
               : ReplayClass::PreFrontierMiss;
}

const CapabilityMemoEntry *
WasmResumeRecorder::memo_entry(std::size_t schedule_pos,
                               std::uint64_t ordinal) const {
    // Linear scan over the (small) sorted replay index.
    for (const auto &entry : replay_index_) {
        if (entry.schedule_pos == schedule_pos &&
            entry.ordinal == ordinal) {
            return &memo_[entry.memo_index];
        }
    }
    return nullptr;
}

const WasmResumeRecorder::PendingCoordinate *
WasmResumeRecorder::frontier() const noexcept {
    return frontier_ ? &*frontier_ : nullptr;
}

void WasmResumeRecorder::mark_frontier_injected() noexcept {
    frontier_was_hit_ = true;
    frontier_.reset();
}

bool WasmResumeRecorder::frontier_was_hit() const noexcept {
    return frontier_was_hit_;
}

} // namespace ahfl::runtime::wasm_host
