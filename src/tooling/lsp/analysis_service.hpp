#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/project_discovery/discovery.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "tooling/incremental/cache_core.hpp"
#include "tooling/lsp/document_store.hpp"
#include "tooling/lsp/hover_index.hpp"
#include "tooling/lsp/protocol_types.hpp"
#include "tooling/lsp/sysroot_primitive_index.hpp"
#include "tooling/lsp/workspace_index.hpp"

namespace ahfl::lsp {

struct LspSourceSnapshot {
    std::string uri{};
    std::filesystem::path path{};
    const SourceFile *source{nullptr};
    const ast::Program *program{nullptr};
    std::optional<SourceId> source_id{};
};

enum class LspAnalysisMode {
    PackageGraph,
    SourceSysroot,
    DetachedSourceUnit,
};

struct LspToolchainCacheKey {
    std::string analysis_mode;
    std::string workspace_folder_uri;
    std::string root_manifest;
    std::string workspace_manifest;
    std::string package_graph_identity;
    std::string std_manifest;
    std::string std_identity;
    std::string scope;
    std::string index_schema_version;
    std::string index_identity_schema_version;

    [[nodiscard]] friend bool operator==(const LspToolchainCacheKey &lhs,
                                         const LspToolchainCacheKey &rhs) = default;
};

struct LspAnalysisSnapshot {
    std::string requested_uri;
    int document_version{0};
    std::uint64_t document_revision{0};
    std::uint64_t content_hash{0};
    std::uint64_t workspace_revision{0};
    std::string open_document_overlay_revision_set;
    std::optional<LspToolchainCacheKey> toolchain_cache_key;
    LspAnalysisMode analysis_mode{LspAnalysisMode::DetachedSourceUnit};
    bool project_aware{false};
    std::optional<std::filesystem::path> package_graph_manifest;
    std::vector<LspDiagnostic> project_diagnostics;

    std::unique_ptr<ParseResult> parse_result;
    std::unique_ptr<ProjectParseResult> project_result;
    ResolveResult resolve_result;
    std::unique_ptr<TypeCheckResult> type_check_result;
    std::unique_ptr<ValidationResult> validation_result;
    std::unique_ptr<LspWorkspaceIndex> workspace_index;

    std::vector<LspSourceSnapshot> sources;
    std::unordered_map<std::string, std::size_t> source_by_uri;
    std::unordered_map<std::string, std::size_t> source_by_display_name;
    std::unordered_map<std::size_t, std::size_t> source_by_id;
    std::unordered_map<std::size_t, DefId> workspace_def_by_symbol;
    std::unordered_map<std::size_t, HoverTargetIndex> hover_indices;

    [[nodiscard]] const LspSourceSnapshot *source_for_uri(std::string_view uri) const;
    [[nodiscard]] const LspSourceSnapshot *source_for_id(SourceId id) const;
    [[nodiscard]] const LspSourceSnapshot *source_for_display_name(std::string_view name) const;
    [[nodiscard]] std::optional<DefId> workspace_def_for_symbol(SymbolId symbol) const;
    [[nodiscard]] const TypedProgram *typed_program() const noexcept;
    [[nodiscard]] std::vector<LspDiagnostic> diagnostics_for_uri(std::string_view uri) const;
};

class AnalysisService {
  public:
    explicit AnalysisService(const DocumentStore &store);

    void set_workspace_folders(std::vector<std::filesystem::path> roots);
    void set_toolchain_profiles(project_discovery::ToolchainProfileSet profiles);

    // Enables or disables the persistent typed-HIR cache (RFC 0016). When
    // enabled, a cold start (no in-memory snapshot for a URI) consults the
    // persistent cache before falling back to full analysis, and a successful
    // full analysis persists its typed program. The cache lives under the
    // XDG cache home (<cache>/ahfl/<project-root-hash>/), shared with the
    // standalone incremental compiler.
    //
    // The cold-start snapshot is a *degraded* snapshot: only the typed program
    // is round-trippable through the current cache envelope, so environment-
    // dependent features (hover, completion, references) are unavailable on a
    // cold-start hit until a full analysis rebuilds the snapshot. The cache is
    // therefore opt-in; the LSP server does not enable it yet.
    void set_persistent_cache_enabled(bool enabled);

    void invalidate_all();
    void invalidate_paths(const std::vector<std::filesystem::path> &paths);

    [[nodiscard]] const LspAnalysisSnapshot *snapshot_for_uri(const std::string &uri);
    [[nodiscard]] const SysrootPrimitiveIndex *
    sysroot_primitive_index_for_uri(const std::string &uri);
    [[nodiscard]] const LspWorkspaceIndex *sysroot_index_for_uri(const std::string &uri);
    [[nodiscard]] std::vector<const LspWorkspaceIndex *> workspace_root_indices();
    [[nodiscard]] std::vector<const LspAnalysisSnapshot *> workspace_snapshots();
    [[nodiscard]] std::size_t analysis_runs() const noexcept;

    [[nodiscard]] static std::optional<std::filesystem::path> path_from_uri(std::string_view uri);
    [[nodiscard]] static std::string uri_from_path(const std::filesystem::path &path);
    [[nodiscard]] static std::string normalized_path_key(const std::filesystem::path &path);

  private:
    [[nodiscard]] std::unique_ptr<LspAnalysisSnapshot>
    build_snapshot(const std::string &uri,
                   std::optional<LspToolchainCacheKey> toolchain_cache_key,
                   const LspWorkspaceIndex *previous_index);
    [[nodiscard]] std::optional<LspToolchainCacheKey>
    toolchain_cache_key_for_uri(const std::string &uri) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> open_document_overlays() const;
    [[nodiscard]] std::string open_document_overlay_revision_set_for_paths(
        const std::vector<std::filesystem::path> &paths) const;
    [[nodiscard]] std::string
    open_document_overlay_revision_set_for_snapshot(const LspAnalysisSnapshot &snapshot) const;

    // Builds the unified RFC 0016 CacheKey for a URI from the LSP toolchain
    // key. Returns nullopt when the persistent cache is disabled, the file is
    // detached (no project manifest), or the document is unavailable.
    [[nodiscard]] std::optional<incremental::CacheKey>
    persistent_cache_key_for_uri(const std::string &uri,
                                 const std::optional<LspToolchainCacheKey> &toolchain_key) const;
    // Returns the persistent cache for a project root, creating it on first
    // use. The cache directory is <cache-root>/<project-root-hash>/.
    [[nodiscard]] incremental::PersistentCache *
    persistent_cache_for_project(const std::filesystem::path &project_root);
    // Builds a degraded snapshot from a persistent cache hit: the document is
    // parsed (cheap) and the typed program is deserialized, but resolve,
    // typecheck, workspace index, and hover indices are skipped. Returns
    // nullptr on any deserialization failure so the caller falls back to full
    // analysis (graceful fallback).
    [[nodiscard]] std::unique_ptr<LspAnalysisSnapshot>
    build_snapshot_from_persistent(const std::string &uri,
                                   const LspToolchainCacheKey &toolchain_key,
                                   const incremental::PersistentCacheEntry &entry);
    // Serializes the snapshot's typed program into the cache envelope and
    // stores it. No-op when the snapshot has no typed program.
    void persist_typed_program(const LspAnalysisSnapshot &snapshot,
                               const incremental::CacheKey &key,
                               const std::filesystem::path &project_root);
    // Removes the persistent entries for the given absolute path keys. Used by
    // invalidate_paths so stale typed HIR is never reloaded on cold start.
    void invalidate_persistent_paths(const std::unordered_set<std::string> &path_keys);
    // Clears every persistent cache managed by this service.
    void invalidate_persistent_all();

    const DocumentStore &store_;
    std::vector<std::filesystem::path> workspace_folders_;
    project_discovery::ToolchainProfileSet toolchain_profiles_;
    std::unordered_map<std::string, std::unique_ptr<LspAnalysisSnapshot>> cache_;
    std::unordered_map<std::string, std::unique_ptr<SysrootPrimitiveIndex>>
        sysroot_primitive_index_cache_;
    std::unordered_map<std::string, std::unique_ptr<LspWorkspaceIndex>> sysroot_index_cache_;
    std::unordered_map<std::string, std::unique_ptr<LspWorkspaceIndex>> workspace_root_index_cache_;
    std::unordered_map<std::string, SourceUnitId> extra_source_unit_ids_by_path_;
    std::size_t next_extra_source_unit_id_{0};
    std::size_t analysis_runs_{0};
    bool persistent_cache_enabled_{false};
    std::unordered_map<std::string, std::unique_ptr<incremental::PersistentCache>>
        persistent_caches_;
};

} // namespace ahfl::lsp
