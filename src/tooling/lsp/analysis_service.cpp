#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "base/support/sha256.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/project_discovery/discovery.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace ahfl::lsp {

namespace {

constexpr std::string_view kWorkspaceIndexSchemaVersion = "lsp-workspace-index-v1";
constexpr std::string_view kWorkspaceIndexIdentitySchemaVersion = "lsp-workspace-index-identity-v1";
constexpr std::size_t kExtraSourceUnitIdBase = std::size_t{1} << 48U;

[[nodiscard]] bool is_hex(char ch) noexcept {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] int hex_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return 0;
}

[[nodiscard]] std::string percent_decode(std::string_view text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size() && is_hex(text[index + 1]) &&
            is_hex(text[index + 2])) {
            const auto value = (hex_value(text[index + 1]) << 4) | hex_value(text[index + 2]);
            decoded.push_back(static_cast<char>(value));
            index += 2;
            continue;
        }
        decoded.push_back(text[index]);
    }
    return decoded;
}

[[nodiscard]] bool is_unreserved_uri_char(unsigned char ch) noexcept {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
}

[[nodiscard]] std::string percent_encode_path(std::string_view path) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size());
    for (const unsigned char ch : path) {
        if (is_unreserved_uri_char(ch)) {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(ch >> 4) & 0xF]);
        encoded.push_back(kHex[ch & 0xF]);
    }
    return encoded;
}

[[nodiscard]] bool path_is_equal_or_descendant(const std::filesystem::path &path,
                                               const std::filesystem::path &ancestor) {
    auto path_part = path.begin();
    for (auto ancestor_part = ancestor.begin(); ancestor_part != ancestor.end(); ++ancestor_part) {
        if (path_part == path.end() || *path_part != *ancestor_part) {
            return false;
        }
        ++path_part;
    }
    return true;
}

[[nodiscard]] std::string_view
toolchain_scope_name(project_discovery::ToolchainProfileScope scope) noexcept {
    switch (scope) {
    case project_discovery::ToolchainProfileScope::GlobalDefault:
        return "global-default";
    case project_discovery::ToolchainProfileScope::WorkspaceFolder:
        return "workspace-folder-uri";
    }
    return "global-default";
}

[[nodiscard]] Position to_lsp_position(const SourceFile &source, std::size_t offset) {
    const auto pos = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(pos.line > 0 ? pos.line - 1 : 0),
        .character = static_cast<std::uint32_t>(pos.column > 0 ? pos.column - 1 : 0),
    };
}

[[nodiscard]] Range to_lsp_range(const SourceFile &source, SourceRange range) {
    const auto bounded_begin = std::min(range.begin_offset, source.content.size());
    const auto bounded_end =
        std::max(bounded_begin, std::min(range.end_offset, source.content.size()));
    return Range{
        .start = to_lsp_position(source, bounded_begin),
        .end = to_lsp_position(source, bounded_end),
    };
}

[[nodiscard]] SourceRange fallback_range(const SourceFile &source) {
    return SourceRange{
        .begin_offset = 0,
        .end_offset = std::min<std::size_t>(source.content.size(), 1),
    };
}

[[nodiscard]] const package_graph::PackageNode *
root_package_for_lsp(const package_graph::PackageGraph &graph) {
    const auto found =
        std::find_if(graph.packages.begin(), graph.packages.end(), [](const auto &package) {
            return package.source == package_graph::PackageSourceKind::Root;
        });
    return found == graph.packages.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<std::string>
dependency_prefixes_for_package(const package_graph::PackageGraph &graph,
                                package_graph::PackageId package_id) {
    std::vector<std::string> prefixes;
    for (const auto &dependency : graph.dependencies) {
        if (dependency.from != package_id) {
            continue;
        }

        const auto *target = graph.find_package(dependency.to);
        if (target != nullptr) {
            prefixes.push_back(target->module_prefix);
        }
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

[[nodiscard]] bool source_unit_has_role(const package_graph::SourceUnitNode &unit,
                                        package_graph::SourceUnitRole role) {
    return std::find(unit.roles.begin(), unit.roles.end(), role) != unit.roles.end();
}

[[nodiscard]] const package_graph::SourceUnitNode *
first_source_unit_for_role(const package_graph::PackageGraph &graph,
                           package_graph::PackageId package,
                           package_graph::SourceUnitRole role) {
    const auto found =
        std::find_if(graph.source_units.begin(), graph.source_units.end(), [&](const auto &unit) {
            return unit.package == package && source_unit_has_role(unit, role);
        });
    return found == graph.source_units.end() ? nullptr : &*found;
}

[[nodiscard]] std::filesystem::path
semantic_entry_file_from_package_graph(const package_graph::PackageGraph &graph,
                                       const package_graph::PackageNode &package,
                                       const std::filesystem::path &fallback) {
    if (const auto *source_unit = first_source_unit_for_role(
            graph, package.id, package_graph::SourceUnitRole::TargetEntry);
        source_unit != nullptr) {
        return source_unit->path;
    }
    return fallback;
}

using NavigationScopeKindMap =
    std::unordered_map<std::string, std::vector<LspNavigationIndexSourceKind>>;

[[nodiscard]] bool append_unique_entry_file(ProjectInput &input,
                                            const std::filesystem::path &path,
                                            bool require_existing_file = true) {
    const auto normalized = std::filesystem::path(AnalysisService::normalized_path_key(path));
    if (std::find(input.entry_files.begin(), input.entry_files.end(), normalized) !=
        input.entry_files.end()) {
        return true;
    }

    std::error_code error;
    if (require_existing_file && (!std::filesystem::exists(normalized, error) || error)) {
        return false;
    }

    input.entry_files.push_back(normalized);
    return true;
}

void record_navigation_scope_kind(NavigationScopeKindMap *scope_kinds,
                                  const std::filesystem::path &path,
                                  LspNavigationIndexSourceKind kind) {
    if (scope_kinds == nullptr) {
        return;
    }

    auto &kinds = (*scope_kinds)[AnalysisService::normalized_path_key(path)];
    if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
        kinds.push_back(kind);
    }
}

void append_scoped_entry_file(ProjectInput &input,
                              NavigationScopeKindMap *scope_kinds,
                              const std::filesystem::path &path,
                              LspNavigationIndexSourceKind kind,
                              bool require_existing_file = true) {
    if (append_unique_entry_file(input, path, require_existing_file)) {
        record_navigation_scope_kind(scope_kinds, path, kind);
    }
}

void append_exported_entry_files(
    ProjectInput &input,
    const package_graph::PackageGraph &graph,
    const package_graph::PackageNode &package,
    bool include_prelude,
    NavigationScopeKindMap *scope_kinds = nullptr,
    LspNavigationIndexSourceKind kind = LspNavigationIndexSourceKind::PackageExport) {
    for (const auto &source_unit : graph.source_units) {
        if (source_unit.package != package.id ||
            !source_unit_has_role(source_unit, package_graph::SourceUnitRole::Export)) {
            continue;
        }
        if (!include_prelude && package.module_prefix == "std" &&
            source_unit.module_path == "prelude") {
            continue;
        }
        append_scoped_entry_file(input, scope_kinds, source_unit.path, kind);
    }
}

void seed_source_cache_from_graph(ProjectInput &input, const SourceGraph &graph) {
    for (const auto &source : graph.sources) {
        const auto key = AnalysisService::normalized_path_key(source.path);
        input.source_cache.try_emplace(key, source.source.content);
    }
}

[[nodiscard]] std::vector<LspIndexPackageRoot>
index_package_roots_from_graph(const package_graph::PackageGraph &graph) {
    std::vector<LspIndexPackageRoot> roots;
    roots.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        roots.push_back(LspIndexPackageRoot{
            .package_id = package.id,
            .module_root = package.module_root,
        });
    }
    return roots;
}

[[nodiscard]] std::string
sysroot_index_cache_key(const LspToolchainCacheKey &key,
                        std::string_view open_document_overlay_revision_set) {
    return key.workspace_folder_uri + "#" + key.root_manifest + "#" + key.workspace_manifest + "#" +
           key.package_graph_identity + "#" + key.std_manifest + "#" + key.std_identity + "#" +
           key.scope + "#" + key.index_schema_version + "#" + key.index_identity_schema_version +
           "#" + std::string{open_document_overlay_revision_set};
}

[[nodiscard]] std::string package_graph_identity(const package_graph::PackageGraph &graph) {
    return "sha256:" + support::sha256_hex(package_graph::serialize_package_graph_json(graph));
}

[[nodiscard]] bool is_project_manifest_path(const std::filesystem::path &path) {
    const auto filename = path.filename().generic_string();
    return filename == "ahfl.toml" || filename == "ahfl.workspace.toml";
}

[[nodiscard]] bool source_units_reference_path(const LspWorkspaceIndex &index,
                                               const std::unordered_set<std::string> &path_keys) {
    for (const auto &source_unit : index.source_units()) {
        if (!source_unit.valid) {
            continue;
        }
        if (path_keys.contains(AnalysisService::normalized_path_key(source_unit.path))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool snapshot_references_path(const LspAnalysisSnapshot &snapshot,
                                            const std::unordered_set<std::string> &path_keys) {
    for (const auto &source : snapshot.sources) {
        if (path_keys.contains(AnalysisService::normalized_path_key(source.path))) {
            return true;
        }
    }
    return snapshot.workspace_index != nullptr &&
           source_units_reference_path(*snapshot.workspace_index, path_keys);
}

[[nodiscard]] const package_graph::PackageNode *
package_for_requested_file(const package_graph::PackageGraph &graph,
                           const std::filesystem::path &requested_file) {
    const auto normalized_request =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    const package_graph::PackageNode *best = nullptr;
    for (const auto &package : graph.packages) {
        const auto module_root =
            std::filesystem::path(AnalysisService::normalized_path_key(package.module_root));
        if (!path_is_equal_or_descendant(normalized_request, module_root)) {
            continue;
        }
        if (best == nullptr ||
            module_root.generic_string().size() >
                std::filesystem::path(AnalysisService::normalized_path_key(best->module_root))
                    .generic_string()
                    .size()) {
            best = &package;
        }
    }
    return best;
}

[[nodiscard]] std::string source_unit_registry_key(package_graph::PackageId package,
                                                   const std::filesystem::path &path) {
    return std::to_string(package.value) + '\0' + path.generic_string();
}

[[nodiscard]] std::vector<LspIndexSourceUnitSeed> index_source_units_from_scope_kinds(
    const package_graph::PackageGraph &graph,
    const NavigationScopeKindMap &scope_kinds,
    std::unordered_map<std::string, SourceUnitId> &extra_source_unit_ids_by_path,
    std::size_t &next_extra_source_unit_id) {
    std::unordered_map<std::string, std::vector<const package_graph::SourceUnitNode *>>
        graph_units_by_path;
    graph_units_by_path.reserve(graph.source_units.size());
    for (const auto &unit : graph.source_units) {
        graph_units_by_path[std::filesystem::path(AnalysisService::normalized_path_key(unit.path))
                                .generic_string()]
            .push_back(&unit);
    }

    std::vector<LspIndexSourceUnitSeed> source_units;
    source_units.reserve(scope_kinds.size());
    for (const auto &[path_key, kinds] : scope_kinds) {
        const auto path = std::filesystem::path(path_key);
        const auto normalized_path_key =
            std::filesystem::path(AnalysisService::normalized_path_key(path)).generic_string();
        const auto *package = package_for_requested_file(graph, path);
        if (const auto graph_units = graph_units_by_path.find(normalized_path_key);
            graph_units != graph_units_by_path.end()) {
            const auto unit = std::find_if(
                graph_units->second.begin(), graph_units->second.end(), [&](const auto *candidate) {
                    return package != nullptr && candidate->package == package->id;
                });
            const auto *graph_unit =
                unit == graph_units->second.end() ? graph_units->second.front() : *unit;
            source_units.push_back(LspIndexSourceUnitSeed{
                .source_unit_id = graph_unit->id,
                .package_id = graph_unit->package,
                .path = graph_unit->path,
                .scope_kinds = kinds,
            });
            continue;
        }

        source_units.push_back(LspIndexSourceUnitSeed{
            .source_unit_id = SourceUnitId{std::numeric_limits<std::size_t>::max()},
            .package_id = package == nullptr
                              ? package_graph::PackageId{std::numeric_limits<std::size_t>::max()}
                              : package->id,
            .path = path,
            .scope_kinds = kinds,
        });
    }
    std::sort(source_units.begin(),
              source_units.end(),
              [](const LspIndexSourceUnitSeed &lhs, const LspIndexSourceUnitSeed &rhs) {
                  if (lhs.source_unit_id.value != rhs.source_unit_id.value) {
                      return lhs.source_unit_id.value < rhs.source_unit_id.value;
                  }
                  if (lhs.package_id.value != rhs.package_id.value) {
                      return lhs.package_id.value < rhs.package_id.value;
                  }
                  return lhs.path.generic_string() < rhs.path.generic_string();
              });
    for (auto &source_unit : source_units) {
        if (graph.find_source_unit(source_unit.source_unit_id) != nullptr) {
            continue;
        }

        if (next_extra_source_unit_id < kExtraSourceUnitIdBase) {
            next_extra_source_unit_id = kExtraSourceUnitIdBase;
        }

        const auto path_key = source_unit_registry_key(source_unit.package_id, source_unit.path);
        auto [registry_entry, inserted] =
            extra_source_unit_ids_by_path.try_emplace(path_key, SourceUnitId{});
        if (inserted) {
            registry_entry->second = SourceUnitId{next_extra_source_unit_id++};
        }
        source_unit.source_unit_id = registry_entry->second;
    }
    std::sort(source_units.begin(),
              source_units.end(),
              [](const LspIndexSourceUnitSeed &lhs, const LspIndexSourceUnitSeed &rhs) {
                  return lhs.source_unit_id.value < rhs.source_unit_id.value;
              });
    return source_units;
}

void append_open_overlay_entry_files(ProjectInput &input,
                                     NavigationScopeKindMap *scope_kinds,
                                     const package_graph::PackageGraph &graph,
                                     const std::filesystem::path &requested_file) {
    const auto *requested_package = package_for_requested_file(graph, requested_file);
    for (const auto &[path_key, text] : input.source_overlays) {
        (void)text;
        const auto overlay_path = std::filesystem::path(path_key);
        const auto *overlay_package = package_for_requested_file(graph, overlay_path);
        if (overlay_package == nullptr) {
            continue;
        }
        if (overlay_package->source == package_graph::PackageSourceKind::Sysroot &&
            !(requested_package == overlay_package && overlay_package->module_prefix == "std")) {
            continue;
        }
        append_scoped_entry_file(
            input, scope_kinds, overlay_path, LspNavigationIndexSourceKind::OpenOverlay, false);
    }
}

enum class LspProjectInputMode {
    Semantic,
    WorkspaceIndex,
    SysrootIndex,
};

[[nodiscard]] ProjectInput
project_input_from_package_graph(const package_graph::PackageGraph &graph,
                                 const std::filesystem::path &requested_file,
                                 std::unordered_map<std::string, std::string> overlays,
                                 LspProjectInputMode mode,
                                 NavigationScopeKindMap *scope_kinds = nullptr) {
    ProjectInput input;
    const auto *root_package = root_package_for_lsp(graph);
    const auto fallback_entry =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    if (mode != LspProjectInputMode::SysrootIndex) {
        const auto semantic_entry =
            root_package == nullptr
                ? fallback_entry
                : semantic_entry_file_from_package_graph(graph, *root_package, fallback_entry);
        input.entry_files.push_back(semantic_entry);
        record_navigation_scope_kind(
            scope_kinds, semantic_entry, LspNavigationIndexSourceKind::SemanticEntry);
    }
    input.inject_prelude = false;
    input.source_overlays = std::move(overlays);
    input.enforce_package_dependencies = true;
    input.module_roots.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        input.module_roots.push_back(ProjectInput::ModuleRoot{
            .prefix = package.module_prefix,
            .root = package.module_root,
            .exported_modules = package.exported_modules,
            .dependency_prefixes = dependency_prefixes_for_package(graph, package.id),
            .compiler_intrinsics_allow = package.compiler_intrinsics_allow,
        });
    }
    if (mode == LspProjectInputMode::SysrootIndex) {
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                package.module_prefix == "std") {
                append_exported_entry_files(input,
                                            graph,
                                            package,
                                            true,
                                            scope_kinds,
                                            LspNavigationIndexSourceKind::SysrootExport);
                break;
            }
        }
    } else if (mode == LspProjectInputMode::WorkspaceIndex) {
        const auto *requested_package = package_for_requested_file(graph, requested_file);
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                !(requested_package == &package && package.module_prefix == "std")) {
                continue;
            }
            const auto kind = package.source == package_graph::PackageSourceKind::Sysroot
                                  ? LspNavigationIndexSourceKind::SysrootExport
                                  : LspNavigationIndexSourceKind::PackageExport;
            append_exported_entry_files(input, graph, package, true, scope_kinds, kind);
        }
        append_open_overlay_entry_files(input, scope_kinds, graph, requested_file);
    }
    return input;
}

[[nodiscard]] DiagnosticSeverity to_lsp_severity(ahfl::DiagnosticSeverity severity) {
    switch (severity) {
    case ahfl::DiagnosticSeverity::Error:
        return DiagnosticSeverity::Error;
    case ahfl::DiagnosticSeverity::Warning:
        return DiagnosticSeverity::Warning;
    case ahfl::DiagnosticSeverity::Note:
        return DiagnosticSeverity::Information;
    }
    return DiagnosticSeverity::Error;
}

[[nodiscard]] std::optional<LspDiagnostic> convert_diagnostic(const Diagnostic &diagnostic,
                                                              const LspAnalysisSnapshot &snapshot,
                                                              const LspSourceSnapshot &source,
                                                              std::string_view fallback_code) {
    LspDiagnostic result;
    result.severity = to_lsp_severity(diagnostic.severity);
    result.message = diagnostic.message;
    result.code = diagnostic.code.value_or(std::string(fallback_code));
    result.source = "ahfl";
    const auto range = diagnostic.range.value_or(fallback_range(*source.source));
    result.range = to_lsp_range(*source.source, range);

    for (const auto &related : diagnostic.related) {
        if (!related.range.has_value()) {
            continue;
        }
        // A related note may live in a different source unit than the primary
        // diagnostic (e.g. "other declaration in module M" surfaced across
        // module boundaries). Prefer source_id, then fall back to the primary
        // source's URI + range conversion when no id is recorded.
        const LspSourceSnapshot *related_source = &source;
        if (related.source_id.has_value()) {
            const auto *by_id = snapshot.source_for_id(*related.source_id);
            if (by_id != nullptr) {
                related_source = by_id;
            }
        }
        if (related_source->source == nullptr) {
            continue;
        }
        LspDiagnostic::RelatedInformation info;
        info.location.uri = related_source->uri;
        info.location.range = to_lsp_range(*related_source->source, *related.range);
        info.message = related.message;
        result.related_information.push_back(std::move(info));
    }

    (void)snapshot;
    return result;
}

void collect_diagnostics_for_uri(std::vector<LspDiagnostic> &out,
                                 const DiagnosticBag &bag,
                                 const LspAnalysisSnapshot &snapshot,
                                 std::string_view target_uri,
                                 std::string_view fallback_code) {
    for (const auto &diagnostic : bag.entries()) {
        const LspSourceSnapshot *source = nullptr;
        if (diagnostic.source_name.has_value()) {
            source = snapshot.source_for_display_name(*diagnostic.source_name);
            if (source == nullptr || source->uri != target_uri) {
                continue;
            }
        } else {
            source = snapshot.source_for_uri(target_uri);
            if (source == nullptr) {
                continue;
            }
        }

        auto converted = convert_diagnostic(diagnostic, snapshot, *source, fallback_code);
        if (converted.has_value()) {
            out.push_back(std::move(*converted));
        }
    }
}

[[nodiscard]] LspDiagnostic
project_discovery_diagnostic(const package_graph::Diagnostic &diagnostic,
                             const LspSourceSnapshot &source) {
    LspDiagnostic result;
    result.severity = DiagnosticSeverity::Error;
    result.message = diagnostic.message;
    result.code = diagnostic.code.empty() ? "project.discovery" : diagnostic.code;
    result.source = "ahfl";
    result.range = source.source == nullptr
                       ? Range{}
                       : to_lsp_range(*source.source, fallback_range(*source.source));
    for (const auto &related : diagnostic.related) {
        if (related.path.empty()) {
            continue;
        }
        LspDiagnostic::RelatedInformation info;
        info.location.uri = AnalysisService::uri_from_path(related.path);
        info.location.range = Range{};
        info.message = related.message;
        result.related_information.push_back(std::move(info));
    }
    return result;
}

void append_project_diagnostics(LspAnalysisSnapshot &snapshot,
                                const std::string &uri,
                                const std::vector<package_graph::Diagnostic> &diagnostics) {
    const auto *source = snapshot.source_for_uri(uri);
    if (source == nullptr) {
        return;
    }
    snapshot.project_diagnostics.reserve(snapshot.project_diagnostics.size() + diagnostics.size());
    for (const auto &diagnostic : diagnostics) {
        snapshot.project_diagnostics.push_back(project_discovery_diagnostic(diagnostic, *source));
    }
}

void index_source(LspAnalysisSnapshot &snapshot, LspSourceSnapshot source) {
    const auto index = snapshot.sources.size();
    if (!source.uri.empty()) {
        snapshot.source_by_uri[source.uri] = index;
    }
    if (source.source != nullptr) {
        snapshot.source_by_display_name[source.source->display_name] = index;
    }
    if (!source.path.empty()) {
        snapshot.source_by_display_name[source.path.generic_string()] = index;
        snapshot.source_by_display_name[display_path(source.path)] = index;
    }
    if (source.source_id.has_value()) {
        snapshot.source_by_id[source.source_id->value] = index;
    }
    snapshot.sources.push_back(std::move(source));
}

[[nodiscard]] bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

void build_workspace_def_remap(LspAnalysisSnapshot &snapshot) {
    snapshot.workspace_def_by_symbol.clear();
    if (snapshot.workspace_index == nullptr) {
        return;
    }

    const auto &index_symbols = snapshot.workspace_index->symbols();
    for (const auto &semantic_symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (!semantic_symbol.source_id.has_value()) {
            continue;
        }
        const auto *source = snapshot.source_for_id(*semantic_symbol.source_id);
        if (source == nullptr) {
            continue;
        }

        const auto index_symbol =
            std::find_if(index_symbols.begin(), index_symbols.end(), [&](const SymbolFact &fact) {
                return fact.kind == semantic_symbol.kind && fact.location.uri == source->uri &&
                       same_range(fact.declaration_range, semantic_symbol.declaration_range);
            });
        if (index_symbol != index_symbols.end()) {
            snapshot.workspace_def_by_symbol.emplace(semantic_symbol.id.value,
                                                     index_symbol->def_id);
        }
    }
}

} // namespace

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_uri(std::string_view uri) const {
    const auto it = source_by_uri.find(std::string(uri));
    if (it == source_by_uri.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_id(SourceId id) const {
    const auto it = source_by_id.find(id.value);
    if (it == source_by_id.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_display_name(std::string_view name) const {
    const auto it = source_by_display_name.find(std::string(name));
    if (it == source_by_display_name.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

std::optional<DefId> LspAnalysisSnapshot::workspace_def_for_symbol(SymbolId symbol) const {
    const auto found = workspace_def_by_symbol.find(symbol.value);
    if (found == workspace_def_by_symbol.end()) {
        return std::nullopt;
    }
    return found->second;
}

const TypedProgram *LspAnalysisSnapshot::typed_program() const noexcept {
    return type_check_result ? &type_check_result->typed_program : nullptr;
}

std::vector<LspDiagnostic> LspAnalysisSnapshot::diagnostics_for_uri(std::string_view uri) const {
    std::vector<LspDiagnostic> diagnostics;

    if (uri == requested_uri) {
        diagnostics.insert(
            diagnostics.end(), project_diagnostics.begin(), project_diagnostics.end());
    }
    if (project_result) {
        collect_diagnostics_for_uri(
            diagnostics, project_result->diagnostics, *this, uri, "parse.diagnostic");
    }
    if (parse_result) {
        collect_diagnostics_for_uri(
            diagnostics, parse_result->diagnostics, *this, uri, "parse.diagnostic");
    }
    collect_diagnostics_for_uri(
        diagnostics, resolve_result.diagnostics, *this, uri, "resolve.diagnostic");
    if (type_check_result) {
        collect_diagnostics_for_uri(
            diagnostics, type_check_result->diagnostics, *this, uri, "typecheck.diagnostic");
    }
    if (validation_result) {
        collect_diagnostics_for_uri(
            diagnostics, validation_result->diagnostics, *this, uri, "validation.diagnostic");
    }

    return diagnostics;
}

AnalysisService::AnalysisService(const DocumentStore &store) : store_(store) {}

void AnalysisService::set_workspace_folders(std::vector<std::filesystem::path> roots) {
    workspace_folders_.clear();
    workspace_folders_.reserve(roots.size());
    for (const auto &root : roots) {
        if (!root.empty()) {
            workspace_folders_.push_back(std::filesystem::path(normalized_path_key(root)));
        }
    }
    invalidate_all();
}

void AnalysisService::set_toolchain_profiles(project_discovery::ToolchainProfileSet profiles) {
    toolchain_profiles_ = std::move(profiles);
    invalidate_all();
}

void AnalysisService::invalidate_all() {
    cache_.clear();
    sysroot_index_cache_.clear();
}

void AnalysisService::invalidate_paths(const std::vector<std::filesystem::path> &paths) {
    std::unordered_set<std::string> path_keys;
    path_keys.reserve(paths.size());
    for (const auto &path : paths) {
        const auto normalized = std::filesystem::path(normalized_path_key(path));
        if (is_project_manifest_path(normalized)) {
            invalidate_all();
            return;
        }
        path_keys.insert(normalized.generic_string());
    }
    if (path_keys.empty()) {
        return;
    }

    for (auto iter = cache_.begin(); iter != cache_.end();) {
        if (iter->second != nullptr && snapshot_references_path(*iter->second, path_keys)) {
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }

    for (auto iter = sysroot_index_cache_.begin(); iter != sysroot_index_cache_.end();) {
        if (iter->second != nullptr && source_units_reference_path(*iter->second, path_keys)) {
            iter = sysroot_index_cache_.erase(iter);
        } else {
            ++iter;
        }
    }
}

const LspAnalysisSnapshot *AnalysisService::snapshot_for_uri(const std::string &uri) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    const auto workspace_revision = store_.workspace_revision();
    const auto overlay_revision_set = open_document_overlay_revision_set();
    const auto toolchain_cache_key = toolchain_cache_key_for_uri(uri);
    if (const auto existing = cache_.find(uri); existing != cache_.end()) {
        const auto &snapshot = *existing->second;
        if (snapshot.document_version == document->version &&
            snapshot.document_revision == *revision && snapshot.content_hash == *hash &&
            snapshot.workspace_revision == workspace_revision &&
            snapshot.open_document_overlay_revision_set == overlay_revision_set &&
            snapshot.toolchain_cache_key == toolchain_cache_key) {
            return existing->second.get();
        }
    }

    auto snapshot = build_snapshot(uri, toolchain_cache_key, overlay_revision_set);
    if (!snapshot) {
        return nullptr;
    }

    ++analysis_runs_;
    auto *snapshot_ptr = snapshot.get();
    cache_[uri] = std::move(snapshot);
    return snapshot_ptr;
}

const LspWorkspaceIndex *AnalysisService::sysroot_index_for_uri(const std::string &uri) {
    const auto key = toolchain_cache_key_for_uri(uri);
    if (!key.has_value()) {
        return nullptr;
    }

    const auto overlay_revision_set = open_document_overlay_revision_set();
    const auto cache_key = sysroot_index_cache_key(*key, overlay_revision_set);
    if (const auto existing = sysroot_index_cache_.find(cache_key);
        existing != sysroot_index_cache_.end()) {
        return existing->second.get();
    }

    const auto document_path = path_from_uri(uri);
    if (!document_path.has_value()) {
        return nullptr;
    }

    std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
    workspace_boundaries.reserve(workspace_folders_.size());
    for (const auto &root : workspace_folders_) {
        workspace_boundaries.push_back(project_discovery::WorkspaceBoundary{.root = root});
    }

    auto project_context =
        project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
            .document_path = *document_path,
            .workspace_boundaries = std::move(workspace_boundaries),
            .toolchains = toolchain_profiles_,
        });
    if (!project_context.context.has_value()) {
        return nullptr;
    }

    Frontend frontend;
    NavigationScopeKindMap sysroot_scope_kinds;
    auto index_input = LspWorkspaceIndexInput{
        .project = project_input_from_package_graph(project_context.context->graph,
                                                    *document_path,
                                                    open_document_overlays(),
                                                    LspProjectInputMode::SysrootIndex,
                                                    &sysroot_scope_kinds),
        .scope =
            NavigationIndexScope{
                .package_roots = index_package_roots_from_graph(project_context.context->graph),
                .source_units =
                    index_source_units_from_scope_kinds(project_context.context->graph,
                                                        sysroot_scope_kinds,
                                                        extra_source_unit_ids_by_path_,
                                                        next_extra_source_unit_id_),
            },
        .metadata =
            NavigationIndexMetadata{
                .revision = store_.workspace_revision(),
                .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
                .index_identity_schema_version = std::string{kWorkspaceIndexIdentitySchemaVersion},
            },
    };
    auto index = std::make_unique<LspWorkspaceIndex>(
        build_lsp_workspace_index(frontend, std::move(index_input)));
    const auto *result = index.get();
    sysroot_index_cache_[cache_key] = std::move(index);
    return result;
}

std::vector<const LspAnalysisSnapshot *> AnalysisService::workspace_snapshots() {
    std::vector<const LspAnalysisSnapshot *> snapshots;
    for (const auto &uri : store_.all_uris()) {
        if (const auto *snapshot = snapshot_for_uri(uri); snapshot != nullptr) {
            snapshots.push_back(snapshot);
        }
    }
    return snapshots;
}

std::size_t AnalysisService::analysis_runs() const noexcept {
    return analysis_runs_;
}

std::optional<std::filesystem::path> AnalysisService::path_from_uri(std::string_view uri) {
    constexpr std::string_view kFilePrefix = "file://";
    if (!uri.starts_with(kFilePrefix)) {
        return std::nullopt;
    }

    auto path_part = uri.substr(kFilePrefix.size());
    if (!path_part.empty() && path_part.front() != '/') {
        const auto slash = path_part.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        path_part = path_part.substr(slash);
    }

    return std::filesystem::path(percent_decode(path_part));
}

std::string AnalysisService::uri_from_path(const std::filesystem::path &path) {
    const auto normalized = std::filesystem::path(normalized_path_key(path)).generic_string();
    return "file://" + percent_encode_path(normalized);
}

std::string AnalysisService::normalized_path_key(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (!error) {
        candidate = canonical.lexically_normal();
    }
    return candidate.generic_string();
}

std::optional<LspToolchainCacheKey>
AnalysisService::toolchain_cache_key_for_uri(const std::string &uri) const {
    const auto document_path = path_from_uri(uri);
    if (!document_path.has_value()) {
        return std::nullopt;
    }

    const auto normalized_document = std::filesystem::path(normalized_path_key(*document_path));

    std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
    workspace_boundaries.reserve(workspace_folders_.size());
    std::optional<std::filesystem::path> workspace_root;
    for (const auto &root : workspace_folders_) {
        const auto normalized_root = std::filesystem::path(normalized_path_key(root));
        workspace_boundaries.push_back(
            project_discovery::WorkspaceBoundary{.root = normalized_root});
        if (path_is_equal_or_descendant(normalized_document, normalized_root) &&
            (!workspace_root.has_value() ||
             normalized_root.generic_string().size() > workspace_root->generic_string().size())) {
            workspace_root = normalized_root;
        }
    }

    const auto selection = project_discovery::select_toolchain_profile_for_document(
        toolchain_profiles_, normalized_document);
    if (!selection.has_value()) {
        return std::nullopt;
    }
    const auto project_context =
        project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
            .document_path = normalized_document,
            .workspace_boundaries = workspace_boundaries,
            .toolchains = toolchain_profiles_,
        });
    if (!project_context.context.has_value()) {
        return std::nullopt;
    }

    return LspToolchainCacheKey{
        .workspace_folder_uri =
            workspace_root.has_value() ? uri_from_path(*workspace_root) : std::string{},
        .root_manifest = normalized_path_key(project_context.context->package_manifest_path),
        .workspace_manifest =
            project_context.context->workspace_manifest_path.has_value()
                ? normalized_path_key(*project_context.context->workspace_manifest_path)
                : std::string{},
        .package_graph_identity = package_graph_identity(project_context.context->graph),
        .std_manifest = normalized_path_key(selection->profile.std_manifest),
        .std_identity = selection->profile.std_identity,
        .scope = std::string{toolchain_scope_name(selection->profile.scope)},
        .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
        .index_identity_schema_version = std::string{kWorkspaceIndexIdentitySchemaVersion},
    };
}

std::unique_ptr<LspAnalysisSnapshot>
AnalysisService::build_snapshot(const std::string &uri,
                                std::optional<LspToolchainCacheKey> toolchain_cache_key,
                                std::string open_document_overlay_revision_set) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    auto snapshot = std::make_unique<LspAnalysisSnapshot>();
    snapshot->requested_uri = uri;
    snapshot->document_version = document->version;
    snapshot->document_revision = *revision;
    snapshot->content_hash = *hash;
    snapshot->workspace_revision = store_.workspace_revision();
    snapshot->open_document_overlay_revision_set = std::move(open_document_overlay_revision_set);
    snapshot->toolchain_cache_key = std::move(toolchain_cache_key);

    const auto document_path = path_from_uri(uri);

    Frontend frontend;
    Resolver resolver;
    TypeChecker type_checker;
    Validator validator;

    if (document_path.has_value()) {
        std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
        workspace_boundaries.reserve(workspace_folders_.size());
        for (const auto &root : workspace_folders_) {
            workspace_boundaries.push_back(project_discovery::WorkspaceBoundary{.root = root});
        }
        auto project_context =
            project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
                .document_path = *document_path,
                .workspace_boundaries = std::move(workspace_boundaries),
                .toolchains = toolchain_profiles_,
            });
        if (project_context.context.has_value()) {
            snapshot->project_aware = true;
            snapshot->package_graph_manifest = project_context.context->graph_manifest_path;

            auto overlays = open_document_overlays();
            auto project_input = project_input_from_package_graph(project_context.context->graph,
                                                                  *document_path,
                                                                  overlays,
                                                                  LspProjectInputMode::Semantic);
            NavigationScopeKindMap workspace_scope_kinds;
            auto index_input = LspWorkspaceIndexInput{
                .project = project_input_from_package_graph(project_context.context->graph,
                                                            *document_path,
                                                            std::move(overlays),
                                                            LspProjectInputMode::WorkspaceIndex,
                                                            &workspace_scope_kinds),
                .scope =
                    NavigationIndexScope{
                        .package_roots =
                            index_package_roots_from_graph(project_context.context->graph),
                        .source_units = index_source_units_from_scope_kinds(
                            project_context.context->graph,
                            workspace_scope_kinds,
                            extra_source_unit_ids_by_path_,
                            next_extra_source_unit_id_),
                    },
                .metadata =
                    NavigationIndexMetadata{
                        .revision = snapshot->workspace_revision,
                        .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
                        .index_identity_schema_version =
                            std::string{kWorkspaceIndexIdentitySchemaVersion},
                    },
            };
            auto project_result = ahfl::parse_project(frontend, project_input);
            snapshot->project_result =
                std::make_unique<ProjectParseResult>(std::move(project_result));
            seed_source_cache_from_graph(index_input.project, snapshot->project_result->graph);
            auto workspace_index = build_lsp_workspace_index(frontend, std::move(index_input));
            snapshot->workspace_index =
                std::make_unique<LspWorkspaceIndex>(std::move(workspace_index));

            for (const auto &source : snapshot->project_result->graph.sources) {
                index_source(*snapshot,
                             LspSourceSnapshot{
                                 .uri = uri_from_path(source.path),
                                 .path = source.path,
                                 .source = &source.source,
                                 .program = source.program.get(),
                                 .source_id = source.id,
                             });
            }

            if (!snapshot->project_result->has_errors()) {
                snapshot->resolve_result = resolver.resolve(snapshot->project_result->graph);
                build_workspace_def_remap(*snapshot);
                if (!snapshot->resolve_result.has_errors()) {
                    auto type_result = type_checker.check(snapshot->project_result->graph,
                                                          snapshot->resolve_result);
                    snapshot->type_check_result =
                        std::make_unique<TypeCheckResult>(std::move(type_result));
                    if (!snapshot->type_check_result->has_errors()) {
                        auto validation_result = validator.validate(snapshot->project_result->graph,
                                                                    snapshot->resolve_result,
                                                                    *snapshot->type_check_result);
                        snapshot->validation_result =
                            std::make_unique<ValidationResult>(std::move(validation_result));
                    }
                }
            }

            build_hover_indices(*snapshot);
            return snapshot;
        }

        if (project_context.project_manifest_found || project_context.has_errors()) {
            snapshot->project_aware = project_context.project_manifest_found;
            auto parse_result = frontend.parse_text(document->uri, document->text);
            snapshot->parse_result = std::make_unique<ParseResult>(std::move(parse_result));
            index_source(*snapshot,
                         LspSourceSnapshot{
                             .uri = uri,
                             .path = *document_path,
                             .source = &snapshot->parse_result->source,
                             .program = snapshot->parse_result->program.get(),
                             .source_id = std::nullopt,
                         });
            append_project_diagnostics(*snapshot, uri, project_context.diagnostics);
            build_hover_indices(*snapshot);
            return snapshot;
        }
    }

    auto parse_result = frontend.parse_text(document->uri, document->text);
    snapshot->parse_result = std::make_unique<ParseResult>(std::move(parse_result));
    index_source(*snapshot,
                 LspSourceSnapshot{
                     .uri = uri,
                     .source = &snapshot->parse_result->source,
                     .program = snapshot->parse_result->program.get(),
                     .source_id = std::nullopt,
                 });

    if (!snapshot->parse_result->has_errors() && snapshot->parse_result->program) {
        snapshot->resolve_result = resolver.resolve(*snapshot->parse_result->program);
        if (!snapshot->resolve_result.has_errors()) {
            auto type_result =
                type_checker.check(*snapshot->parse_result->program, snapshot->resolve_result);
            snapshot->type_check_result = std::make_unique<TypeCheckResult>(std::move(type_result));
            if (!snapshot->type_check_result->has_errors()) {
                auto validation_result = validator.validate(*snapshot->parse_result->program,
                                                            snapshot->resolve_result,
                                                            *snapshot->type_check_result);
                snapshot->validation_result =
                    std::make_unique<ValidationResult>(std::move(validation_result));
            }
        }
    }

    build_hover_indices(*snapshot);
    return snapshot;
}

std::unordered_map<std::string, std::string> AnalysisService::open_document_overlays() const {
    std::unordered_map<std::string, std::string> overlays;
    for (const auto &uri : store_.all_uris()) {
        const auto *document = store_.get(uri);
        const auto path = path_from_uri(uri);
        if (document == nullptr || !path.has_value()) {
            continue;
        }
        overlays.emplace(normalized_path_key(*path), document->text);
    }
    return overlays;
}

std::string AnalysisService::open_document_overlay_revision_set() const {
    auto uris = store_.all_uris();
    std::sort(uris.begin(), uris.end());

    std::string key;
    for (const auto &uri : uris) {
        const auto revision = store_.revision(uri);
        const auto hash = store_.content_hash(uri);
        if (!revision.has_value() || !hash.has_value()) {
            continue;
        }
        key += uri;
        key += "@";
        key += std::to_string(*revision);
        key += ":";
        key += std::to_string(*hash);
        key += ";";
    }
    return key;
}

} // namespace ahfl::lsp
