#pragma once

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

struct RecoveredNodeState {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<evaluator::Value> output{};
};

struct WorkflowRecoverySnapshot {
    WorkflowId workflow;
    CheckpointId checkpoint;
    std::vector<RecoveredNodeState> completed_nodes{};
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
