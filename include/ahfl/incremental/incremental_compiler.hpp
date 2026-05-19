#pragma once

#include <ahfl/incremental/dependency_graph.hpp>
#include <ahfl/incremental/ir_cache.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace ahfl::incremental {

enum class CompileStatus { UpToDate, Recompiled, Failed };

struct CompileResult {
    CompileStatus status;
    std::string module_path;
    std::string error_message;
};

struct IncrementalStats {
    std::size_t modules_checked = 0;
    std::size_t modules_recompiled = 0;
    std::size_t cache_hits = 0;
    std::size_t cache_misses = 0;
};

class IncrementalCompiler {
public:
    explicit IncrementalCompiler(DependencyGraph& graph, IrCache& cache);

    [[nodiscard]] std::vector<CompileResult> compile_changed(
        const std::vector<std::string>& changed_paths);

    [[nodiscard]] IncrementalStats stats() const;
    void reset_stats();

private:
    DependencyGraph& graph_;
    IrCache& cache_;
    IncrementalStats stats_{};
};

} // namespace ahfl::incremental
