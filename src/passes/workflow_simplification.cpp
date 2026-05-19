#include "ahfl/passes/workflow_simplification.hpp"

#include "ahfl/ir/ir.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::passes {

namespace {

// Compute transitive closure of the DAG reachability.
// For each node, find all nodes transitively reachable via `after` edges.
void compute_transitive_reachable(
    const std::vector<ir::WorkflowNode> &nodes,
    std::unordered_map<std::string, std::unordered_set<std::string>> &reachable) {

    // Build adjacency: node -> direct dependencies (after)
    std::unordered_map<std::string, std::vector<std::string>> deps;
    for (const auto &n : nodes) {
        deps[n.name] = n.after;
    }

    // For each node, DFS to compute all transitive deps
    for (const auto &n : nodes) {
        auto &reach = reachable[n.name];
        // BFS/DFS from each direct dependency
        std::vector<std::string> stack(n.after.begin(), n.after.end());
        while (!stack.empty()) {
            auto current = stack.back();
            stack.pop_back();
            if (reach.find(current) != reach.end()) {
                continue;
            }
            reach.insert(current);
            auto it = deps.find(current);
            if (it != deps.end()) {
                for (const auto &dep : it->second) {
                    if (reach.find(dep) == reach.end()) {
                        stack.push_back(dep);
                    }
                }
            }
        }
    }
}

} // namespace

bool WorkflowSimplificationPass::run(ir::Program &program) {
    bool any_modified = false;

    for (auto &decl : program.declarations) {
        auto *wf = std::get_if<ir::WorkflowDecl>(&decl);
        if (wf == nullptr || wf->nodes.size() < 2) {
            continue;
        }

        // Compute transitive reachability
        std::unordered_map<std::string, std::unordered_set<std::string>> reachable;
        compute_transitive_reachable(wf->nodes, reachable);

        // For each node, remove `after` edges that are transitively implied
        for (auto &node : wf->nodes) {
            if (node.after.size() < 2) {
                continue;
            }

            std::vector<std::string> reduced;
            for (const auto &dep : node.after) {
                // Check if `dep` is transitively reachable through another direct dep
                bool redundant = false;
                for (const auto &other_dep : node.after) {
                    if (other_dep == dep) {
                        continue;
                    }
                    // Is `dep` reachable from `other_dep`?
                    auto it = reachable.find(other_dep);
                    if (it != reachable.end() &&
                        it->second.find(dep) != it->second.end()) {
                        redundant = true;
                        break;
                    }
                }
                if (!redundant) {
                    reduced.push_back(dep);
                }
            }

            if (reduced.size() < node.after.size()) {
                node.after = std::move(reduced);
                any_modified = true;
            }
        }
    }

    return any_modified;
}

} // namespace ahfl::passes
