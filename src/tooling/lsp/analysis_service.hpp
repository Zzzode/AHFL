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
#include "tooling/lsp/document_store.hpp"
#include "tooling/lsp/hover_index.hpp"
#include "tooling/lsp/protocol_types.hpp"
#include "tooling/lsp/workspace_index.hpp"

namespace ahfl::lsp {

struct LspSourceSnapshot {
    std::string uri{};
    std::filesystem::path path{};
    const SourceFile *source{nullptr};
    const ast::Program *program{nullptr};
    std::optional<SourceId> source_id{};
};

struct LspToolchainCacheKey {
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
    void invalidate_all();
    void invalidate_paths(const std::vector<std::filesystem::path> &paths);

    [[nodiscard]] const LspAnalysisSnapshot *snapshot_for_uri(const std::string &uri);
    [[nodiscard]] const LspWorkspaceIndex *sysroot_index_for_uri(const std::string &uri);
    [[nodiscard]] std::vector<const LspAnalysisSnapshot *> workspace_snapshots();
    [[nodiscard]] std::size_t analysis_runs() const noexcept;

    [[nodiscard]] static std::optional<std::filesystem::path> path_from_uri(std::string_view uri);
    [[nodiscard]] static std::string uri_from_path(const std::filesystem::path &path);
    [[nodiscard]] static std::string normalized_path_key(const std::filesystem::path &path);

  private:
    [[nodiscard]] std::unique_ptr<LspAnalysisSnapshot>
    build_snapshot(const std::string &uri,
                   std::optional<LspToolchainCacheKey> toolchain_cache_key,
                   std::string open_document_overlay_revision_set);
    [[nodiscard]] std::optional<LspToolchainCacheKey>
    toolchain_cache_key_for_uri(const std::string &uri) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> open_document_overlays() const;
    [[nodiscard]] std::string open_document_overlay_revision_set() const;

    const DocumentStore &store_;
    std::vector<std::filesystem::path> workspace_folders_;
    project_discovery::ToolchainProfileSet toolchain_profiles_;
    std::unordered_map<std::string, std::unique_ptr<LspAnalysisSnapshot>> cache_;
    std::unordered_map<std::string, std::unique_ptr<LspWorkspaceIndex>> sysroot_index_cache_;
    std::unordered_map<std::string, SourceUnitId> source_unit_ids_by_path_;
    std::size_t next_source_unit_id_{0};
    std::size_t analysis_runs_{0};
};

} // namespace ahfl::lsp
