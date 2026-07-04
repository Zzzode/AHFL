#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/project_discovery/discovery.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <system_error>
#include <vector>

namespace ahfl::lsp {

namespace {

constexpr std::string_view kWorkspaceIndexSchemaVersion = "lsp-workspace-index-v1";

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

[[nodiscard]] std::filesystem::path module_path_from_target_symbol(std::string_view entry,
                                                                   std::string_view module_prefix) {
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start < entry.size()) {
        const auto separator = entry.find("::", start);
        if (separator == std::string_view::npos) {
            segments.push_back(entry.substr(start));
            break;
        }
        segments.push_back(entry.substr(start, separator - start));
        start = separator + 2;
    }
    if (segments.size() < 2 || segments.front() != module_prefix) {
        return {};
    }

    std::filesystem::path relative;
    for (std::size_t index = 1; index + 1 < segments.size(); ++index) {
        relative /= std::string(segments[index]);
    }
    if (relative.empty()) {
        return {};
    }
    relative += ".ahfl";
    return relative;
}

[[nodiscard]] std::filesystem::path lsp_target_entry_file(const package_graph::PackageNode &package,
                                                          const std::filesystem::path &fallback) {
    if (package.targets.empty() || package.targets.front().entry.empty()) {
        return fallback;
    }

    const auto &entry = package.targets.front().entry;
    if (entry.ends_with(".ahfl") || entry.find('/') != std::string::npos ||
        entry.find('\\') != std::string::npos) {
        return std::filesystem::path(
            AnalysisService::normalized_path_key(package.package_root / entry));
    }

    const auto relative_module = module_path_from_target_symbol(entry, package.module_prefix);
    if (!relative_module.empty()) {
        return std::filesystem::path(
            AnalysisService::normalized_path_key(package.module_root / relative_module));
    }

    return fallback;
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

void append_unique_entry_file(ProjectInput &input,
                              const std::filesystem::path &path,
                              bool require_existing_file = true) {
    const auto normalized = std::filesystem::path(AnalysisService::normalized_path_key(path));
    if (std::find(input.entry_files.begin(), input.entry_files.end(), normalized) !=
        input.entry_files.end()) {
        return;
    }

    std::error_code error;
    if (require_existing_file && (!std::filesystem::exists(normalized, error) || error)) {
        return;
    }

    input.entry_files.push_back(normalized);
}

[[nodiscard]] std::filesystem::path exported_module_path(const package_graph::PackageNode &package,
                                                         std::string_view module_key) {
    auto relative = std::filesystem::path(std::string(module_key));
    relative += ".ahfl";
    return package.module_root / relative;
}

void append_primitive_home_entry_files(ProjectInput &input,
                                       const package_graph::PackageGraph &graph) {
    constexpr std::string_view kPrimitiveHomeModules[] = {
        "bool",
        "int",
        "float",
        "string",
        "uuid",
        "time",
        "decimal",
    };

    const auto std_package = std::find_if(
        graph.packages.begin(),
        graph.packages.end(),
        [](const package_graph::PackageNode &package) { return package.module_prefix == "std"; });
    if (std_package == graph.packages.end()) {
        return;
    }

    for (const auto module_key : kPrimitiveHomeModules) {
        if (std::find(std_package->exported_modules.begin(),
                      std_package->exported_modules.end(),
                      module_key) == std_package->exported_modules.end()) {
            continue;
        }
        append_unique_entry_file(input, exported_module_path(*std_package, module_key));
    }
}

void append_exported_entry_files(ProjectInput &input,
                                 const package_graph::PackageNode &package,
                                 bool include_prelude) {
    for (const auto &module_key : package.exported_modules) {
        if (!include_prelude && package.module_prefix == "std" && module_key == "prelude") {
            continue;
        }
        append_unique_entry_file(input, exported_module_path(package, module_key));
    }
}

void seed_source_overlays_from_graph(ProjectInput &input, const SourceGraph &graph) {
    for (const auto &source : graph.sources) {
        const auto key = AnalysisService::normalized_path_key(source.path);
        input.source_overlays.try_emplace(key, source.source.content);
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

void append_open_overlay_entry_files(ProjectInput &input,
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
        append_unique_entry_file(input, overlay_path, false);
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
                                 LspProjectInputMode mode) {
    ProjectInput input;
    const auto *root_package = root_package_for_lsp(graph);
    const auto fallback_entry =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    if (mode != LspProjectInputMode::SysrootIndex) {
        input.entry_files.push_back(root_package == nullptr
                                        ? fallback_entry
                                        : lsp_target_entry_file(*root_package, fallback_entry));
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
    if (mode == LspProjectInputMode::Semantic) {
        append_primitive_home_entry_files(input, graph);
    } else if (mode == LspProjectInputMode::SysrootIndex) {
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                package.module_prefix == "std") {
                append_exported_entry_files(input, package, true);
                break;
            }
        }
    } else {
        const auto *requested_package = package_for_requested_file(graph, requested_file);
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                !(requested_package == &package && package.module_prefix == "std")) {
                continue;
            }
            append_exported_entry_files(input, package, true);
        }
        append_open_overlay_entry_files(input, graph, requested_file);
        append_primitive_home_entry_files(input, graph);
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

const LspAnalysisSnapshot *AnalysisService::snapshot_for_uri(const std::string &uri) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    const auto workspace_revision = store_.workspace_revision();
    const auto toolchain_cache_key = toolchain_cache_key_for_uri(uri);
    if (const auto existing = cache_.find(uri); existing != cache_.end()) {
        const auto &snapshot = *existing->second;
        if (snapshot.document_version == document->version &&
            snapshot.document_revision == *revision && snapshot.content_hash == *hash &&
            snapshot.workspace_revision == workspace_revision &&
            snapshot.toolchain_cache_key == toolchain_cache_key) {
            return existing->second.get();
        }
    }

    auto snapshot = build_snapshot(uri, toolchain_cache_key);
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

    const auto cache_key = key->std_manifest + "#" + key->std_identity + "#" +
                           key->index_schema_version + "#" +
                           std::to_string(store_.workspace_revision());
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
    auto index_input = LspWorkspaceIndexInput{
        .project = project_input_from_package_graph(project_context.context->graph,
                                                    *document_path,
                                                    open_document_overlays(),
                                                    LspProjectInputMode::SysrootIndex),
        .package_roots = index_package_roots_from_graph(project_context.context->graph),
        .revision = store_.workspace_revision(),
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

    const auto package_manifest = project_discovery::find_package_manifest_for_document(
        normalized_document, workspace_boundaries);
    if (!package_manifest.has_value()) {
        return std::nullopt;
    }

    const auto selection = project_discovery::select_toolchain_profile_for_document(
        toolchain_profiles_, normalized_document);
    if (!selection.has_value()) {
        return std::nullopt;
    }

    return LspToolchainCacheKey{
        .workspace_folder_uri =
            workspace_root.has_value() ? uri_from_path(*workspace_root) : std::string{},
        .root_manifest = normalized_path_key(*package_manifest),
        .std_manifest = normalized_path_key(selection->profile.std_manifest),
        .std_identity = selection->profile.std_identity,
        .scope = std::string{toolchain_scope_name(selection->profile.scope)},
        .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
    };
}

std::unique_ptr<LspAnalysisSnapshot>
AnalysisService::build_snapshot(const std::string &uri,
                                std::optional<LspToolchainCacheKey> toolchain_cache_key) {
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
            auto index_input = LspWorkspaceIndexInput{
                .project = project_input_from_package_graph(project_context.context->graph,
                                                            *document_path,
                                                            std::move(overlays),
                                                            LspProjectInputMode::WorkspaceIndex),
                .package_roots = index_package_roots_from_graph(project_context.context->graph),
                .revision = snapshot->workspace_revision,
            };
            auto project_result = ahfl::parse_project(frontend, project_input);
            snapshot->project_result =
                std::make_unique<ProjectParseResult>(std::move(project_result));
            seed_source_overlays_from_graph(index_input.project, snapshot->project_result->graph);
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

} // namespace ahfl::lsp
