#include "tooling/incremental/incremental_compiler.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace ahfl::incremental {

namespace {

namespace fs = std::filesystem;

std::uint64_t compute_content_hash(const std::string &file_path) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs)
        return 0;
    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset_basis;
    char byte = '\0';
    while (ifs.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= prime;
    }
    return hash;
}

// FNV-1a digest of the import graph structure: every module path and every
// (importer, imported) edge, sorted for determinism. Stored in the cache
// envelope as source_graph_revision so a structural graph change invalidates
// entries even when content hashes are unchanged.
[[nodiscard]] std::string compute_source_graph_revision(const DependencyGraph &graph) {
    std::vector<std::string> modules;
    std::vector<std::string> edges;
    for (const auto &path : graph.topological_order()) {
        modules.push_back(path);
        for (const auto &dep : graph.dependencies_of(path)) {
            edges.push_back(path + "->" + dep);
        }
    }
    std::sort(edges.begin(), edges.end());
    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    const auto mix = [&hash](std::string_view text) {
        for (const char byte : text) {
            hash ^= static_cast<unsigned char>(byte);
            hash *= prime;
        }
    };
    for (const auto &module : modules) {
        mix(module);
    }
    for (const auto &edge : edges) {
        mix(edge);
    }
    return std::to_string(hash);
}

// Compute transitive dependents via BFS
std::vector<std::string> transitive_dependents(const DependencyGraph &graph,
                                               const std::string &path) {
    std::unordered_set<std::string> visited;
    std::vector<std::string> queue;
    queue.push_back(path);
    visited.insert(path);

    for (std::size_t i = 0; i < queue.size(); ++i) {
        auto deps = graph.dependents_of(queue[i]);
        for (const auto &dep : deps) {
            if (visited.insert(dep).second) {
                queue.push_back(dep);
            }
        }
    }

    // Remove the original path from the result
    std::vector<std::string> result;
    for (std::size_t i = 1; i < queue.size(); ++i) {
        result.push_back(queue[i]);
    }
    return result;
}

} // namespace

IncrementalCompiler::IncrementalCompiler(DependencyGraph &graph, IrCache &cache)
    : IncrementalCompiler(graph, cache, IncrementalCompilerConfig{}) {}

IncrementalCompiler::IncrementalCompiler(DependencyGraph &graph,
                                         IrCache &cache,
                                         IncrementalCompilerConfig config)
    : graph_(graph), cache_(cache), config_(std::move(config)) {
    if (!config_.project_root.empty()) {
        project_root_hash_ = project_root_hash(config_.project_root);
    }
    if (config_.toolchain_fingerprint.empty()) {
        config_.toolchain_fingerprint = default_toolchain_fingerprint();
    }
}

CacheKey IncrementalCompiler::build_cache_key(const std::string &module_path,
                                              std::uint64_t content_hash) const {
    CacheKey key;
    key.project_root_hash = project_root_hash_;
    key.content_hash = content_hash;
    key.toolchain_fingerprint = config_.toolchain_fingerprint;
    if (config_.project_root.empty()) {
        key.source_path = module_path;
        return key;
    }
    std::error_code error;
    const auto relative =
        fs::relative(fs::path(module_path), config_.project_root, error);
    key.source_path = error ? module_path : relative.generic_string();
    return key;
}

void IncrementalCompiler::hydrate_from_persistent(
    const std::string &module_path,
    std::uint64_t content_hash,
    const PersistentCacheEntry &entry) {
    CacheEntry in_memory;
    in_memory.module_path = module_path;
    in_memory.content_hash = content_hash;
    in_memory.signature_fingerprint = entry.signature_fingerprint;
    in_memory.serialized_ir = entry.serialized_typed_hir;
    in_memory.cached_at = entry.cached_at;
    cache_.store(std::move(in_memory));
}

void IncrementalCompiler::persist_entry(const std::string &module_path,
                                        std::uint64_t content_hash,
                                        std::uint64_t signature_fingerprint,
                                        const std::string &serialized_ir,
                                        const std::string &source_graph_revision) {
    if (config_.persistent_cache == nullptr) {
        return;
    }
    PersistentCacheEntry record;
    record.key = build_cache_key(module_path, content_hash);
    record.source_graph_revision = source_graph_revision;
    // resolver_snapshot_version: canonical ResolveResult hashing is a
    // follow-up slice; the envelope field is reserved and stored empty.
    record.signature_fingerprint = signature_fingerprint;
    record.serialized_typed_hir = serialized_ir;
    record.cached_at = std::chrono::system_clock::now();
    config_.persistent_cache->store(record);
}

std::vector<CompileResult>
IncrementalCompiler::compile_changed(const std::vector<std::string> &changed_paths) {

    std::vector<CompileResult> results;

    // Compute all affected modules (changed + transitive dependents)
    std::vector<std::string> affected;
    for (const auto &path : changed_paths) {
        affected.push_back(path);
        auto dependents = transitive_dependents(graph_, path);
        for (const auto &dep : dependents) {
            if (std::find(affected.begin(), affected.end(), dep) == affected.end()) {
                affected.push_back(dep);
            }
        }
    }

    // Sort in topological order for correct compilation
    auto topo_order = graph_.topological_order();
    std::vector<std::string> ordered;
    for (const auto &mod : topo_order) {
        if (std::find(affected.begin(), affected.end(), mod) != affected.end()) {
            ordered.push_back(mod);
        }
    }

    // The graph revision is stable for the duration of this pass; compute it
    // once so every envelope written below carries the same value.
    const std::string source_graph_revision =
        config_.persistent_cache != nullptr ? compute_source_graph_revision(graph_)
                                            : std::string{};

    // Compile each module
    for (const auto &mod_path : ordered) {
        ++stats_.modules_checked;

        // Check cache validity
        auto content_hash = compute_content_hash(mod_path);
        auto cached = cache_.lookup(mod_path, content_hash);
        if (cached.kind == CacheHitKind::Hit) {
            ++stats_.cache_hits;
            results.push_back(CompileResult{CompileStatus::UpToDate, mod_path, ""});
            continue;
        }

        // In-memory miss: consult the persistent cache (RFC 0016) before
        // falling back to a full recompile. A persistent hit hydrates the
        // in-memory cache so subsequent lookups are fast.
        if (config_.persistent_cache != nullptr) {
            const auto key = build_cache_key(mod_path, content_hash);
            auto persistent = config_.persistent_cache->lookup(key);
            if (persistent.kind == PersistentCacheHitKind::Hit && persistent.entry.has_value()) {
                hydrate_from_persistent(mod_path, content_hash, *persistent.entry);
                ++stats_.cache_hits;
                ++stats_.persistent_cache_hits;
                results.push_back(CompileResult{CompileStatus::UpToDate, mod_path, ""});
                continue;
            }
        }
        ++stats_.cache_misses;

        // Run real compilation pipeline
        std::string serialized_ir;
        CompileStatus status = CompileStatus::Recompiled;
        std::string error_msg;
        std::uint64_t new_signature_fingerprint = 0;

        ahfl::Frontend frontend;
        auto parse_result = frontend.parse_file(mod_path);
        if (parse_result.has_errors() || !parse_result.program) {
            std::ostringstream err;
            for (const auto &diag : parse_result.diagnostics.entries()) {
                err << diag.message << "; ";
            }
            status = CompileStatus::Failed;
            error_msg = err.str();
        } else {
            ahfl::Resolver resolver;
            auto resolve_result = resolver.resolve(*parse_result.program);
            if (resolve_result.has_errors()) {
                std::ostringstream err;
                for (const auto &diag : resolve_result.diagnostics.entries()) {
                    err << diag.message << "; ";
                }
                status = CompileStatus::Failed;
                error_msg = err.str();
            } else {
                ahfl::TypeChecker checker;
                auto tc_result = checker.check(*parse_result.program, resolve_result);
                if (tc_result.has_errors()) {
                    std::ostringstream err;
                    for (const auto &diag : tc_result.diagnostics.entries()) {
                        err << diag.message << "; ";
                    }
                    status = CompileStatus::Failed;
                    error_msg = err.str();
                } else {
                    // Aggregate the per-symbol signature fingerprints into a
                    // single 64-bit digest of the module's type environment.
                    // Downstream cache decisions can later compare this
                    // against an earlier run to detect semantic shape
                    // changes that survive content-only edits.
                    constexpr std::uint64_t kFprMixPrime = 0x100000001b3ULL;
                    std::uint64_t fingerprint = 0xcbf29ce484222325ULL;
                    for (const auto &[id, info] : tc_result.environment.structs()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }
                    for (const auto &[id, info] : tc_result.environment.enums()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }
                    for (const auto &[id, info] : tc_result.environment.capabilities()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }
                    for (const auto &[id, info] : tc_result.environment.predicates()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }
                    for (const auto &[id, info] : tc_result.environment.agents()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }
                    for (const auto &[id, info] : tc_result.environment.workflows()) {
                        (void)info;
                        if (const auto fp =
                                tc_result.environment.signature_fingerprint(SymbolId{id});
                            fp.has_value()) {
                            fingerprint ^= *fp;
                            fingerprint *= kFprMixPrime;
                        }
                    }

                    auto ir_program =
                        ahfl::lower_program_ir(*parse_result.program, resolve_result, tc_result);
                    std::ostringstream ir_json;
                    ahfl::print_program_ir_json(ir_program, ir_json);
                    serialized_ir = ir_json.str();

                    // Look up the previous cache entry to track whether
                    // the recompile actually shifted the type environment.
                    if (const auto previous = cache_.lookup(mod_path, 0);
                        previous.entry.has_value() &&
                        previous.entry->signature_fingerprint == fingerprint) {
                        ++stats_.fingerprint_unchanged;
                    }

                    new_signature_fingerprint = fingerprint;
                }
            }
        }

        if (status == CompileStatus::Recompiled) {
            CacheEntry new_entry;
            new_entry.module_path = mod_path;
            new_entry.content_hash = content_hash;
            new_entry.signature_fingerprint = new_signature_fingerprint;
            new_entry.serialized_ir = serialized_ir;
            new_entry.cached_at = std::chrono::system_clock::now();
            cache_.store(std::move(new_entry));
            persist_entry(mod_path,
                          content_hash,
                          new_signature_fingerprint,
                          serialized_ir,
                          source_graph_revision);
        } else {
            cache_.invalidate(mod_path);
            if (config_.persistent_cache != nullptr) {
                config_.persistent_cache->invalidate(
                    build_cache_key(mod_path, content_hash).source_path);
            }
        }

        ++stats_.modules_recompiled;
        results.push_back(CompileResult{status, mod_path, error_msg});
    }

    return results;
}

IncrementalStats IncrementalCompiler::stats() const {
    return stats_;
}

void IncrementalCompiler::reset_stats() {
    stats_ = {};
}

} // namespace ahfl::incremental
