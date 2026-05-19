#pragma once
#include <string>
#include <vector>
#include <functional>
#include <variant>
#include <unordered_map>
#include <chrono>

namespace ahfl::runtime {

enum class FailurePropagationMode {
    FailFast,       // Stop all on first failure
    FailGraceful,   // Complete running tasks, then stop
    ContinueOnError // Continue, collect all failures
};

struct RetryPolicy {
    int max_attempts = 1;
    std::chrono::milliseconds initial_backoff{100};
    double backoff_multiplier = 2.0;
};

struct DagNode {
    std::string id;
    std::vector<std::string> dependencies;
    std::function<std::string()> execute;  // returns output value
    RetryPolicy retry{};
};

enum class NodeStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Skipped
};

struct NodeResult {
    std::string node_id;
    NodeStatus status = NodeStatus::Pending;
    std::string output;
    std::string error;
    std::chrono::milliseconds duration{0};
    int attempts = 0;
};

struct SchedulerResult {
    bool success = false;
    std::vector<NodeResult> node_results;
    std::chrono::milliseconds total_duration{0};
};

class ParallelScheduler {
public:
    explicit ParallelScheduler(size_t thread_count = 4,
                               FailurePropagationMode mode = FailurePropagationMode::FailFast);

    void add_node(DagNode node);

    [[nodiscard]] bool validate_dag() const;  // check for cycles

    [[nodiscard]] SchedulerResult execute();

    [[nodiscard]] size_t node_count() const;

private:
    std::vector<DagNode> nodes_;
    size_t thread_count_;
    FailurePropagationMode mode_;
};

} // namespace ahfl::runtime
