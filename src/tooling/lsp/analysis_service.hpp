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
#include "tooling/lsp/document_store.hpp"
#include "tooling/lsp/hover_index.hpp"
#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

struct LspSourceSnapshot {
    std::string uri{};
    std::filesystem::path path{};
    const SourceFile *source{nullptr};
    const ast::Program *program{nullptr};
    std::optional<SourceId> source_id{};
};

struct LspAnalysisSnapshot {
    std::string requested_uri;
    int document_version{0};
    std::uint64_t document_revision{0};
    std::uint64_t content_hash{0};
    std::uint64_t workspace_revision{0};
    bool project_aware{false};
    std::optional<std::filesystem::path> package_graph_manifest;

    std::unique_ptr<ParseResult> parse_result;
    std::unique_ptr<ProjectParseResult> project_result;
    ResolveResult resolve_result;
    std::unique_ptr<TypeCheckResult> type_check_result;
    std::unique_ptr<ValidationResult> validation_result;

    std::vector<LspSourceSnapshot> sources;
    std::unordered_map<std::string, std::size_t> source_by_uri;
    std::unordered_map<std::string, std::size_t> source_by_display_name;
    std::unordered_map<std::size_t, std::size_t> source_by_id;
    std::unordered_map<std::size_t, HoverTargetIndex> hover_indices;

    [[nodiscard]] const LspSourceSnapshot *source_for_uri(std::string_view uri) const;
    [[nodiscard]] const LspSourceSnapshot *source_for_id(SourceId id) const;
    [[nodiscard]] const LspSourceSnapshot *source_for_display_name(std::string_view name) const;
    [[nodiscard]] const TypedProgram *typed_program() const noexcept;
    [[nodiscard]] std::vector<LspDiagnostic> diagnostics_for_uri(std::string_view uri) const;
};

class AnalysisService {
  public:
    explicit AnalysisService(const DocumentStore &store);

    void set_workspace_roots(std::vector<std::filesystem::path> roots);
    void invalidate_all();

    [[nodiscard]] const LspAnalysisSnapshot *snapshot_for_uri(const std::string &uri);
    [[nodiscard]] std::vector<const LspAnalysisSnapshot *> workspace_snapshots();
    [[nodiscard]] std::size_t analysis_runs() const noexcept;

    [[nodiscard]] static std::optional<std::filesystem::path> path_from_uri(std::string_view uri);
    [[nodiscard]] static std::string uri_from_path(const std::filesystem::path &path);
    [[nodiscard]] static std::string normalized_path_key(const std::filesystem::path &path);

  private:
    [[nodiscard]] std::unique_ptr<LspAnalysisSnapshot> build_snapshot(const std::string &uri);
    [[nodiscard]] std::vector<std::filesystem::path>
    infer_descriptorless_search_roots(const std::filesystem::path &source_path,
                                      const ParseResult &parse_result) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> open_document_overlays() const;

    const DocumentStore &store_;
    std::vector<std::filesystem::path> workspace_roots_;
    std::unordered_map<std::string, std::unique_ptr<LspAnalysisSnapshot>> cache_;
    std::size_t analysis_runs_{0};
};

} // namespace ahfl::lsp
