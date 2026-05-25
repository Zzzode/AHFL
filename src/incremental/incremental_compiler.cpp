#include "incremental/incremental_compiler.hpp"

#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace ahfl::incremental {

namespace {

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
    : graph_(graph), cache_(cache) {}

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
        ++stats_.cache_misses;

        // Run real compilation pipeline
        std::string serialized_ir;
        CompileStatus status = CompileStatus::Recompiled;
        std::string error_msg;

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
                    auto ir_program =
                        ahfl::lower_program_ir(*parse_result.program, resolve_result, tc_result);
                    std::ostringstream ir_json;
                    ahfl::print_program_ir_json(ir_program, ir_json);
                    serialized_ir = ir_json.str();
                }
            }
        }

        if (status == CompileStatus::Recompiled) {
            CacheEntry new_entry;
            new_entry.module_path = mod_path;
            new_entry.content_hash = content_hash;
            new_entry.serialized_ir = serialized_ir;
            new_entry.cached_at = std::chrono::system_clock::now();
            cache_.store(std::move(new_entry));
        } else {
            cache_.invalidate(mod_path);
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
