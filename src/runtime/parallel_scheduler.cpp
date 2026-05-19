#include "ahfl/runtime/parallel_scheduler.hpp"
#include <queue>
#include <unordered_set>
#include <algorithm>

namespace ahfl::runtime {

ParallelScheduler::ParallelScheduler(size_t thread_count, FailurePropagationMode mode)
    : thread_count_(thread_count), mode_(mode) {
    (void)thread_count_;
}

void ParallelScheduler::add_node(DagNode node) {
    nodes_.push_back(std::move(node));
}

bool ParallelScheduler::validate_dag() const {
    // Kahn's algorithm for cycle detection
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;

    for (const auto& node : nodes_) {
        if (in_degree.find(node.id) == in_degree.end()) {
            in_degree[node.id] = 0;
        }
        for (const auto& dep : node.dependencies) {
            adjacency[dep].push_back(node.id);
            in_degree[node.id]++;
        }
    }

    std::queue<std::string> queue;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(id);
        }
    }

    size_t visited = 0;
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        visited++;

        for (const auto& neighbor : adjacency[current]) {
            if (--in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    return visited == nodes_.size();
}

SchedulerResult ParallelScheduler::execute() {
    auto start = std::chrono::steady_clock::now();
    SchedulerResult result;

    if (!validate_dag()) {
        result.success = false;
        return result;
    }

    // Topological sort
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, const DagNode*> node_map;

    for (const auto& node : nodes_) {
        node_map[node.id] = &node;
        if (in_degree.find(node.id) == in_degree.end()) {
            in_degree[node.id] = 0;
        }
        for (const auto& dep : node.dependencies) {
            adjacency[dep].push_back(node.id);
            in_degree[node.id]++;
        }
    }

    std::queue<std::string> ready;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            ready.push(id);
        }
    }

    std::unordered_set<std::string> failed_nodes;
    bool should_stop = false;

    while (!ready.empty() && !should_stop) {
        auto node_id = ready.front();
        ready.pop();

        auto* node = node_map[node_id];
        NodeResult nr;
        nr.node_id = node_id;

        // Check if dependencies failed
        bool dep_failed = false;
        for (const auto& dep : node->dependencies) {
            if (failed_nodes.count(dep)) {
                dep_failed = true;
                break;
            }
        }

        if (dep_failed) {
            nr.status = NodeStatus::Skipped;
            nr.error = "dependency failed";
            result.node_results.push_back(std::move(nr));
            // Still enqueue successors so they can be skipped too
            for (const auto& successor : adjacency[node_id]) {
                if (--in_degree[successor] == 0) {
                    ready.push(successor);
                }
            }
            failed_nodes.insert(node_id);
            continue;
        }

        // Execute with retry
        auto node_start = std::chrono::steady_clock::now();
        bool executed = false;
        for (int attempt = 0; attempt < node->retry.max_attempts; ++attempt) {
            nr.attempts = attempt + 1;
            try {
                if (node->execute) {
                    nr.output = node->execute();
                }
                nr.status = NodeStatus::Completed;
                executed = true;
                break;
            } catch (const std::exception& e) {
                nr.error = e.what();
            }
        }

        if (!executed) {
            nr.status = NodeStatus::Failed;
            failed_nodes.insert(node_id);
            if (mode_ == FailurePropagationMode::FailFast) {
                should_stop = true;
            }
        }

        auto node_end = std::chrono::steady_clock::now();
        nr.duration = std::chrono::duration_cast<std::chrono::milliseconds>(node_end - node_start);
        result.node_results.push_back(std::move(nr));

        // Enqueue ready successors
        for (const auto& successor : adjacency[node_id]) {
            if (--in_degree[successor] == 0) {
                ready.push(successor);
            }
        }
    }

    auto end = std::chrono::steady_clock::now();
    result.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    result.success = failed_nodes.empty();

    return result;
}

size_t ParallelScheduler::node_count() const {
    return nodes_.size();
}

} // namespace ahfl::runtime
