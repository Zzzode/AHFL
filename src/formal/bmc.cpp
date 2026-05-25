#include "formal/bmc.hpp"

#include <algorithm>
#include <chrono>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::formal {
namespace {

struct AdjacencyGraph {
    std::unordered_map<std::string, std::vector<std::string>> edges;
};

[[nodiscard]] AdjacencyGraph build_graph(const BmcStateMachine &machine) {
    AdjacencyGraph g;
    for (const auto &t : machine.transitions) {
        g.edges[t.from].push_back(t.to);
    }
    return g;
}

/// Parse a property string to extract the "bad state" name.
/// Supported forms: "never(X)" -> X is bad, "reachable(X)" -> X is NOT bad.
struct PropertyInfo {
    std::string state_name;
    bool is_never{false}; // true = "never(X)", false = "reachable(X)"
};

[[nodiscard]] std::vector<PropertyInfo> parse_properties(const std::vector<std::string> &props) {
    std::vector<PropertyInfo> result;
    for (const auto &p : props) {
        if (p.size() > 6 && p.substr(0, 6) == "never(") {
            auto name = p.substr(6, p.size() - 7);
            result.push_back({name, true});
        } else if (p.size() > 10 && p.substr(0, 10) == "reachable(") {
            auto name = p.substr(10, p.size() - 11);
            result.push_back({name, false});
        }
    }
    return result;
}

/// BFS from initial_state up to max_depth, returns all reachable states at each depth
/// and the path trace for counterexample construction.
struct BfsResult {
    bool found_bad{false};
    std::size_t bad_depth{0};
    std::vector<std::string> trace;
    std::string violated_property;
    std::size_t max_depth_reached{0};
};

[[nodiscard]] BfsResult bfs_check(const AdjacencyGraph &graph,
                                  const std::string &start,
                                  std::size_t max_depth,
                                  const std::unordered_set<std::string> &bad_states,
                                  const std::string &property_name) {
    BfsResult res;
    // BFS with depth tracking and parent map
    std::queue<std::pair<std::string, std::size_t>> frontier;
    std::unordered_map<std::string, std::string> parent;
    std::unordered_set<std::string> visited;

    frontier.push({start, 0});
    visited.insert(start);
    parent[start] = "";

    while (!frontier.empty()) {
        auto [current, depth] = frontier.front();
        frontier.pop();

        if (depth > res.max_depth_reached) {
            res.max_depth_reached = depth;
        }

        if (bad_states.count(current) != 0) {
            res.found_bad = true;
            res.bad_depth = depth;
            res.violated_property = property_name;
            // Reconstruct trace
            std::vector<std::string> trace;
            std::string s = current;
            while (!s.empty()) {
                trace.push_back(s);
                s = parent[s];
            }
            std::reverse(trace.begin(), trace.end());
            res.trace = std::move(trace);
            return res;
        }

        if (depth >= max_depth) {
            continue;
        }

        auto it = graph.edges.find(current);
        if (it != graph.edges.end()) {
            for (const auto &next : it->second) {
                if (visited.count(next) == 0) {
                    visited.insert(next);
                    parent[next] = current;
                    frontier.push({next, depth + 1});
                }
            }
        }
    }

    return res;
}

/// Check reachability from ALL states (for k-induction)
[[nodiscard]] bool all_states_safe(const AdjacencyGraph &graph,
                                   const std::vector<std::string> &all_states,
                                   const std::unordered_set<std::string> &bad_states) {
    for (const auto &state : all_states) {
        if (bad_states.count(state) != 0) {
            return false;
        }
        auto it = graph.edges.find(state);
        if (it != graph.edges.end()) {
            for (const auto &next : it->second) {
                if (bad_states.count(next) != 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

[[nodiscard]] BmcResult run_bmc(const BmcStateMachine &machine, const BmcOptions &options) {
    auto start_time = std::chrono::steady_clock::now();
    BmcResult result;

    if (machine.states.empty() || machine.initial_state.empty()) {
        result.status = BmcStatus::Error;
        result.error_message = "Invalid state machine: no states or no initial state";
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    auto graph = build_graph(machine);
    auto props = parse_properties(machine.properties);

    // Collect bad states from "never(X)" properties
    std::unordered_set<std::string> bad_states;
    std::string property_desc;
    for (const auto &p : props) {
        if (p.is_never) {
            bad_states.insert(p.state_name);
            if (!property_desc.empty()) {
                property_desc += ", ";
            }
            property_desc += "never(" + p.state_name + ")";
        }
    }

    if (bad_states.empty()) {
        // No "never" properties means all states are safe by default
        result.status = BmcStatus::Safe;
        result.bound_reached = options.max_bound;
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    auto bfs =
        bfs_check(graph, machine.initial_state, options.max_bound, bad_states, property_desc);

    if (bfs.found_bad) {
        result.status = BmcStatus::Unsafe;
        result.bound_reached = bfs.bad_depth;
        BmcCounterexample cex;
        cex.depth = bfs.bad_depth;
        cex.trace_states = std::move(bfs.trace);
        cex.violated_property = bfs.violated_property;
        result.counterexample = std::move(cex);
    } else {
        result.status = BmcStatus::Safe;
        // If BFS exhausted all reachable states, property holds for all bounds
        result.bound_reached = options.max_bound;
    }

    auto end_time = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

[[nodiscard]] BmcResult run_k_induction(const BmcStateMachine &machine, const BmcOptions &options) {
    auto start_time = std::chrono::steady_clock::now();
    BmcResult result;

    if (machine.states.empty() || machine.initial_state.empty()) {
        result.status = BmcStatus::Error;
        result.error_message = "Invalid state machine: no states or no initial state";
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    auto graph = build_graph(machine);
    auto props = parse_properties(machine.properties);

    std::unordered_set<std::string> bad_states;
    for (const auto &p : props) {
        if (p.is_never) {
            bad_states.insert(p.state_name);
        }
    }

    if (bad_states.empty()) {
        result.status = BmcStatus::Safe;
        result.bound_reached = options.max_bound;
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    // Base case: check reachability from initial state
    auto bfs = bfs_check(graph, machine.initial_state, options.max_bound, bad_states, "");

    if (bfs.found_bad) {
        // Bad state reachable from initial — unsafe
        result.status = BmcStatus::Unsafe;
        result.bound_reached = bfs.bad_depth;
        BmcCounterexample cex;
        cex.depth = bfs.bad_depth;
        cex.trace_states = std::move(bfs.trace);
        cex.violated_property = "k-induction base case failure";
        result.counterexample = std::move(cex);
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    // Induction step: verify no bad state is reachable from ANY state
    if (all_states_safe(graph, machine.states, bad_states)) {
        result.status = BmcStatus::Safe;
        result.bound_reached = options.max_bound;
    } else {
        // Induction step fails — cannot prove safety
        result.status = BmcStatus::Unknown;
        result.bound_reached = bfs.max_depth_reached;
        result.error_message = "k-induction: base case passed but induction step inconclusive";
    }

    auto end_time = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

[[nodiscard]] BmcResult run_cegar(const BmcStateMachine &machine, const BmcOptions &options) {
    auto start_time = std::chrono::steady_clock::now();
    BmcResult result;

    if (machine.states.empty() || machine.initial_state.empty()) {
        result.status = BmcStatus::Error;
        result.error_message = "Invalid state machine: no states or no initial state";
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

    // CEGAR requires SAT solver integration for sound counterexample validation.
    // Without a SAT backend, we conservatively report Unknown.
    result.status = BmcStatus::Unknown;
    result.bound_reached = options.max_bound;
    result.error_message = "CEGAR: requires SAT solver backend for sound verification";

    auto end_time = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

} // namespace ahfl::formal
