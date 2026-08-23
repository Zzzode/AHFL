#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <tooling/incremental/cache_core.hpp>
#include <tooling/incremental/dependency_graph.hpp>
#include <tooling/incremental/ir_cache.hpp>
#include <vector>

namespace ahfl::incremental {

enum class CompileStatus {
    UpToDate,
    Recompiled,
    Failed
};

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
    // Number of times a recompile produced the same TypeEnvironment
    // signature fingerprint as the previous cache entry, so transitive
    // dependents were NOT invalidated and kept their cache entries
    // (comment/whitespace/body-only edits). Complements cache_hits: a
    // fingerprint skip means downstream modules hit the cache on the next
    // pass even though the upstream module was recompiled.
    std::size_t fingerprint_skipped = 0;
    // Number of hits served from the PersistentCache (disk) after the
    // in-memory IrCache tier missed. Always zero when no persistent cache is
    // configured.
    std::size_t persistent_cache_hits = 0;
};

// Configuration for the CacheCore-backed persistent layer (RFC 0016). When
// `persistent_cache` is null the compiler runs in legacy detached mode:
// string-keyed in-memory cache only, no disk persistence, no project
// identity.
struct IncrementalCompilerConfig {
    std::filesystem::path project_root;
    std::string toolchain_fingerprint;
    PersistentCache *persistent_cache{nullptr};
};

class IncrementalCompiler {
  public:
    explicit IncrementalCompiler(DependencyGraph &graph, IrCache &cache);
    IncrementalCompiler(DependencyGraph &graph,
                        IrCache &cache,
                        IncrementalCompilerConfig config);

    [[nodiscard]] std::vector<CompileResult>
    compile_changed(const std::vector<std::string> &changed_paths);

    [[nodiscard]] IncrementalStats stats() const;
    void reset_stats();

    // Drops every in-memory and persistent cache entry. Used by the daemon
    // when the project manifest changes (RFC 0016): the old graph structure
    // is stale, so every cached artifact is stale too.
    void invalidate_all();

  private:
    [[nodiscard]] CacheKey build_cache_key(const std::string &module_path,
                                           std::uint64_t content_hash) const;
    // Project-relative source path used as the unified cache identity. Falls
    // back to the raw module path when no project root is configured.
    [[nodiscard]] std::string source_path_for(const std::string &module_path) const;

    DependencyGraph &graph_;
    IrCache &cache_;
    IncrementalCompilerConfig config_;
    std::string project_root_hash_;
    IncrementalStats stats_{};
};

} // namespace ahfl::incremental
