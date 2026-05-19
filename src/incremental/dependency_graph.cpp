#include <ahfl/incremental/dependency_graph.hpp>

#include <algorithm>
#include <unordered_set>

namespace ahfl::incremental {

void DependencyGraph::add_module(ModuleNode node) {
    modules_[node.module_path] = std::move(node);
}

void DependencyGraph::remove_module(const std::string& path) {
    modules_.erase(path);
}

[[nodiscard]] std::vector<std::string> DependencyGraph::dependents_of(const std::string& path) const {
    std::vector<std::string> result;
    for (const auto& [mod_path, node] : modules_) {
        for (const auto& imp : node.imports) {
            if (imp == path) {
                result.push_back(mod_path);
                break;
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] std::vector<std::string> DependencyGraph::dependencies_of(const std::string& path) const {
    auto it = modules_.find(path);
    if (it == modules_.end()) {
        return {};
    }
    auto deps = it->second.imports;
    std::sort(deps.begin(), deps.end());
    return deps;
}

[[nodiscard]] std::vector<std::string> DependencyGraph::topological_order() const {
    std::vector<std::string> result;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> in_stack;

    auto dfs = [&](auto& self, const std::string& path) -> bool {
        if (in_stack.count(path) > 0) {
            return false;  // cycle
        }
        if (visited.count(path) > 0) {
            return true;
        }
        in_stack.insert(path);
        visited.insert(path);

        auto it = modules_.find(path);
        if (it != modules_.end()) {
            for (const auto& dep : it->second.imports) {
                if (modules_.count(dep) > 0) {
                    if (!self(self, dep)) {
                        return false;
                    }
                }
            }
        }
        in_stack.erase(path);
        result.push_back(path);
        return true;
    };

    // Collect and sort keys for deterministic output
    std::vector<std::string> keys;
    keys.reserve(modules_.size());
    for (const auto& [k, v] : modules_) {
        (void)v;
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        if (visited.count(key) == 0) {
            dfs(dfs, key);
        }
    }
    return result;
}

[[nodiscard]] bool DependencyGraph::has_cycle() const {
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> in_stack;

    auto dfs = [&](auto& self, const std::string& path) -> bool {
        if (in_stack.count(path) > 0) {
            return true;  // cycle found
        }
        if (visited.count(path) > 0) {
            return false;
        }
        in_stack.insert(path);
        visited.insert(path);

        auto it = modules_.find(path);
        if (it != modules_.end()) {
            for (const auto& dep : it->second.imports) {
                if (modules_.count(dep) > 0) {
                    if (self(self, dep)) {
                        return true;
                    }
                }
            }
        }
        in_stack.erase(path);
        return false;
    };

    for (const auto& [path, node] : modules_) {
        (void)node;
        if (visited.count(path) == 0) {
            if (dfs(dfs, path)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::size_t DependencyGraph::module_count() const {
    return modules_.size();
}

void DependencyGraph::clear() {
    modules_.clear();
}

} // namespace ahfl::incremental
