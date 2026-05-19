#include <ahfl/incremental/incremental_compiler.hpp>

#include <algorithm>
#include <unordered_set>

namespace ahfl::incremental {

IncrementalCompiler::IncrementalCompiler(DependencyGraph& graph, IrCache& cache)
    : graph_(graph), cache_(cache) {}

[[nodiscard]] std::vector<CompileResult> IncrementalCompiler::compile_changed(
    const std::vector<std::string>& changed_paths) {

    // 1. Find all transitively affected modules
    std::unordered_set<std::string> to_recompile;
    std::vector<std::string> worklist(changed_paths.begin(), changed_paths.end());

    while (!worklist.empty()) {
        auto current = worklist.back();
        worklist.pop_back();
        if (to_recompile.insert(current).second) {
            auto deps = graph_.dependents_of(current);
            for (const auto& dep : deps) {
                if (to_recompile.count(dep) == 0) {
                    worklist.push_back(dep);
                }
            }
        }
    }

    // 2. Get topological order and filter to affected modules
    auto topo = graph_.topological_order();
    std::vector<std::string> ordered;
    for (const auto& mod : topo) {
        if (to_recompile.count(mod) > 0) {
            ordered.push_back(mod);
        }
    }

    // 3. For each module, check cache and decide
    std::vector<CompileResult> results;
    for (const auto& mod_path : ordered) {
        ++stats_.modules_checked;

        // Look up a dummy hash (changed modules get hash 0 to force recompile)
        std::uint64_t hash = 0;
        // If it's not in the direct changed set, use existing hash
        bool directly_changed = std::find(changed_paths.begin(), changed_paths.end(), mod_path)
                                != changed_paths.end();

        if (!directly_changed) {
            // Use a new hash to signal dependency changed
            hash = 1;
        }

        auto lookup = cache_.lookup(mod_path, hash);

        if (lookup.kind == CacheHitKind::Hit) {
            ++stats_.cache_hits;
            results.push_back(CompileResult{
                CompileStatus::UpToDate, mod_path, ""});
        } else {
            ++stats_.cache_misses;
            ++stats_.modules_recompiled;

            // Store a new dummy cache entry
            CacheEntry new_entry;
            new_entry.module_path = mod_path;
            new_entry.content_hash = hash;
            new_entry.serialized_ir = R"({"module":")" + mod_path + R"(","ir":"compiled"})";
            new_entry.cached_at = std::chrono::system_clock::now();
            cache_.store(std::move(new_entry));

            results.push_back(CompileResult{
                CompileStatus::Recompiled, mod_path, ""});
        }
    }

    return results;
}

[[nodiscard]] IncrementalStats IncrementalCompiler::stats() const {
    return stats_;
}

void IncrementalCompiler::reset_stats() {
    stats_ = IncrementalStats{};
}

} // namespace ahfl::incremental
