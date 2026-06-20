#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::incremental {

struct ModuleNode {
    std::string module_path;
    std::uint64_t content_hash;
    std::chrono::system_clock::time_point last_modified;
    std::vector<std::string> imports;
};

class DependencyGraph {
  public:
    void add_module(ModuleNode node);
    void remove_module(const std::string &path);
    [[nodiscard]] std::vector<std::string> dependents_of(const std::string &path) const;
    [[nodiscard]] std::vector<std::string> dependencies_of(const std::string &path) const;
    [[nodiscard]] std::vector<std::string> topological_order() const;
    [[nodiscard]] bool has_cycle() const;
    [[nodiscard]] std::size_t module_count() const;
    void clear();

  private:
    std::unordered_map<std::string, ModuleNode> modules_;
};

} // namespace ahfl::incremental
