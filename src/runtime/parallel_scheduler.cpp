#include "ahfl/runtime/parallel_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ahfl::runtime {

ParallelScheduler::ParallelScheduler(size_t thread_count, FailurePropagationMode mode)
    : thread_count_(thread_count == 0 ? 1 : thread_count), mode_(mode) {}

void ParallelScheduler::add_node(DagNode node) {
    nodes_.push_back(std::move(node));
}

bool ParallelScheduler::validate_dag() const {
    // Kahn's algorithm for cycle detection
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;

    for (const auto &node : nodes_) {
        if (in_degree.find(node.id) == in_degree.end()) {
            in_degree[node.id] = 0;
        }
        for (const auto &dep : node.dependencies) {
            adjacency[dep].push_back(node.id);
            in_degree[node.id]++;
        }
    }

    std::queue<std::string> queue;
    for (const auto &[id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(id);
        }
    }

    size_t visited = 0;
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        visited++;

        for (const auto &neighbor : adjacency[current]) {
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

    // Build graph structures
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, const DagNode *> node_map;

    for (const auto &node : nodes_) {
        node_map[node.id] = &node;
        if (in_degree.find(node.id) == in_degree.end()) {
            in_degree[node.id] = 0;
        }
        for (const auto &dep : node.dependencies) {
            adjacency[dep].push_back(node.id);
            in_degree[node.id]++;
        }
    }

    // Shared state protected by mutex
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<std::string> ready_queue;
    std::unordered_set<std::string> failed_nodes;
    std::unordered_map<std::string, NodeResult> results_map;
    size_t completed_count = 0;
    bool stop_requested = false;

    // Seed the ready queue with zero-dependency nodes
    for (const auto &[id, degree] : in_degree) {
        if (degree == 0) {
            ready_queue.push(id);
        }
    }

    // Worker function: each thread pulls from ready_queue and executes nodes
    auto worker = [&]() {
        while (true) {
            std::string node_id;

            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [&] {
                    return !ready_queue.empty() || stop_requested ||
                           completed_count == nodes_.size();
                });

                if (stop_requested || completed_count == nodes_.size()) {
                    return;
                }

                if (ready_queue.empty()) {
                    continue;
                }

                node_id = ready_queue.front();
                ready_queue.pop();
            }

            auto *node = node_map[node_id];
            NodeResult nr;
            nr.node_id = node_id;

            // Check if any dependency failed
            bool dep_failed = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                for (const auto &dep : node->dependencies) {
                    if (failed_nodes.count(dep)) {
                        dep_failed = true;
                        break;
                    }
                }
            }

            if (dep_failed) {
                nr.status = NodeStatus::Skipped;
                nr.error = "dependency failed";

                std::lock_guard<std::mutex> lock(mtx);
                results_map[node_id] = std::move(nr);
                failed_nodes.insert(node_id);
                completed_count++;

                // Enqueue successors so they can be skipped in turn
                for (const auto &successor : adjacency[node_id]) {
                    if (--in_degree[successor] == 0) {
                        ready_queue.push(successor);
                    }
                }
                cv.notify_all();
                continue;
            }

            // Execute node with retry + exponential backoff
            auto node_start = std::chrono::steady_clock::now();
            bool executed = false;

            for (int attempt = 0; attempt < node->retry.max_attempts; ++attempt) {
                nr.attempts = attempt + 1;

                // Exponential backoff between retries
                if (attempt > 0) {
                    auto backoff_ms = static_cast<long long>(
                        node->retry.initial_backoff.count() *
                        std::pow(node->retry.backoff_multiplier, attempt - 1));
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                }

                try {
                    if (node->execute) {
                        nr.output = node->execute();
                    }
                    nr.status = NodeStatus::Completed;
                    executed = true;
                    break;
                } catch (const std::exception &e) {
                    nr.error = e.what();
                }
            }

            auto node_end = std::chrono::steady_clock::now();
            nr.duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(node_end - node_start);

            if (!executed) {
                nr.status = NodeStatus::Failed;
            }

            // Commit result to shared state
            {
                std::lock_guard<std::mutex> lock(mtx);
                results_map[node_id] = nr;
                completed_count++;

                if (!executed) {
                    failed_nodes.insert(node_id);
                    if (mode_ == FailurePropagationMode::FailFast) {
                        stop_requested = true;
                        cv.notify_all();
                        return;
                    }
                }

                // Enqueue newly ready successors
                for (const auto &successor : adjacency[node_id]) {
                    if (--in_degree[successor] == 0) {
                        ready_queue.push(successor);
                    }
                }
            }
            cv.notify_all();
        }
    };

    // Launch thread pool (cap at node count since no benefit beyond that)
    size_t actual_threads = std::min(thread_count_, nodes_.size());
    std::vector<std::thread> threads;
    threads.reserve(actual_threads);

    for (size_t i = 0; i < actual_threads; ++i) {
        threads.emplace_back(worker);
    }

    // Join all workers
    for (auto &t : threads) {
        t.join();
    }

    // Collect results preserving original node order
    for (const auto &node : nodes_) {
        auto it = results_map.find(node.id);
        if (it != results_map.end()) {
            result.node_results.push_back(std::move(it->second));
        } else {
            // Node never scheduled (stop_requested before it became ready)
            NodeResult nr;
            nr.node_id = node.id;
            nr.status = NodeStatus::Skipped;
            nr.error = "execution stopped before scheduling";
            result.node_results.push_back(std::move(nr));
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
