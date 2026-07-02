#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "compiler/manifest/manifest.hpp"
#include "compiler/package_graph/package_graph.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace ahfl::lsp {

namespace {

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

[[nodiscard]] bool path_has_prefix(const std::filesystem::path &prefix,
                                   const std::filesystem::path &candidate) {
    auto prefix_it = prefix.begin();
    auto candidate_it = candidate.begin();
    while (prefix_it != prefix.end() && candidate_it != candidate.end()) {
        if (*prefix_it != *candidate_it) {
            return false;
        }
        ++prefix_it;
        ++candidate_it;
    }
    return prefix_it == prefix.end();
}

[[nodiscard]] bool read_plain_file(const std::filesystem::path &path, std::string &content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path>
find_nearest_named_file(const std::filesystem::path &start, std::string_view filename) {
    if (start.empty()) {
        return std::nullopt;
    }

    auto current = std::filesystem::path(AnalysisService::normalized_path_key(start));
    while (!current.empty()) {
        const auto candidate = current / std::string{filename};
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return std::filesystem::path(AnalysisService::normalized_path_key(candidate));
        }

        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
find_sysroot_manifest_from_directory(const std::filesystem::path &start) {
    const auto candidate = std::filesystem::path(
        AnalysisService::normalized_path_key(std::filesystem::path(start) / "std" / "ahfl.toml"));
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error) && !error) {
        return candidate;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
find_lsp_sysroot_manifest(const std::vector<std::filesystem::path> &workspace_roots) {
    if (const char *env_root = std::getenv("AHFL_SYSROOT");
        env_root != nullptr && *env_root != '\0') {
        if (auto manifest = find_sysroot_manifest_from_directory(env_root); manifest.has_value()) {
            return manifest;
        }
    }

    for (const auto &root : workspace_roots) {
        if (auto manifest = find_sysroot_manifest_from_directory(root); manifest.has_value()) {
            return manifest;
        }
    }

    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (error) {
        return std::nullopt;
    }
    current = std::filesystem::path(AnalysisService::normalized_path_key(current));
    while (!current.empty()) {
        if (auto manifest = find_sysroot_manifest_from_directory(current); manifest.has_value()) {
            return manifest;
        }
        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<manifest::PackageManifest>
load_lsp_package_manifest(const std::filesystem::path &manifest_path) {
    std::string text;
    if (!read_plain_file(manifest_path, text)) {
        return std::nullopt;
    }
    auto parsed = manifest::parse_package_manifest(text);
    if (parsed.has_errors() || !parsed.manifest.has_value()) {
        return std::nullopt;
    }
    return std::move(*parsed.manifest);
}

[[nodiscard]] std::optional<manifest::WorkspaceManifest>
load_lsp_workspace_manifest(const std::filesystem::path &manifest_path) {
    std::string text;
    if (!read_plain_file(manifest_path, text)) {
        return std::nullopt;
    }
    auto parsed = manifest::parse_workspace_manifest(text);
    if (parsed.has_errors() || !parsed.manifest.has_value()) {
        return std::nullopt;
    }
    return std::move(*parsed.manifest);
}

[[nodiscard]] bool
workspace_contains_package_manifest(const manifest::WorkspaceManifest &workspace,
                                    const std::filesystem::path &workspace_manifest_path,
                                    const std::filesystem::path &package_manifest_path) {
    const auto workspace_root = std::filesystem::path(
        AnalysisService::normalized_path_key(workspace_manifest_path.parent_path()));
    const auto package_manifest =
        std::filesystem::path(AnalysisService::normalized_path_key(package_manifest_path));
    for (const auto &member : workspace.members) {
        const auto member_manifest = std::filesystem::path(
            AnalysisService::normalized_path_key(workspace_root / member / "ahfl.toml"));
        if (member_manifest == package_manifest) {
            return true;
        }
    }
    return false;
}

struct LspPackageGraphInput {
    package_graph::PackageGraph graph;
    std::filesystem::path manifest_path;
};

[[nodiscard]] std::vector<std::string>
exported_module_paths(const std::vector<manifest::ExportedModuleManifest> &modules) {
    std::vector<std::string> paths;
    paths.reserve(modules.size());
    for (const auto &module : modules) {
        paths.push_back(module.module_path);
    }
    return paths;
}

[[nodiscard]] package_graph::PackageGraph
sysroot_only_graph_for_lsp(const manifest::PackageManifest &manifest,
                           const std::filesystem::path &manifest_path) {
    const auto normalized_manifest =
        std::filesystem::path(AnalysisService::normalized_path_key(manifest_path));
    const auto package_root = std::filesystem::path(
        AnalysisService::normalized_path_key(normalized_manifest.parent_path()));
    const auto module_root =
        std::filesystem::path(AnalysisService::normalized_path_key(package_root /
                                                                   manifest.module_root));

    package_graph::PackageGraph graph;
    graph.packages.push_back(package_graph::PackageNode{
        .id = package_graph::PackageId{0},
        .source = package_graph::PackageSourceKind::Sysroot,
        .name = manifest.package_name,
        .version = manifest.package_version,
        .kind = manifest.package_kind,
        .module_prefix = manifest.module_prefix,
        .package_root = package_root,
        .module_root = module_root,
        .manifest_path = normalized_manifest,
        .checksum = {},
        .exported_modules = exported_module_paths(manifest.exported_modules),
        .compiler_intrinsics_allow = manifest.compiler_intrinsics_allow,
        .targets = {},
    });
    graph.module_roots.push_back(package_graph::ModuleRootEntry{
        .prefix = manifest.module_prefix,
        .package = package_graph::PackageId{0},
        .root = module_root,
    });
    return graph;
}

[[nodiscard]] std::optional<LspPackageGraphInput>
build_lsp_package_graph_for_source(const std::filesystem::path &source_path,
                                   const std::vector<std::filesystem::path> &workspace_roots) {
    const auto package_manifest_path =
        find_nearest_named_file(source_path.parent_path(), "ahfl.toml");
    if (!package_manifest_path.has_value()) {
        return std::nullopt;
    }

    const auto package_manifest = load_lsp_package_manifest(*package_manifest_path);
    if (!package_manifest.has_value()) {
        return std::nullopt;
    }

    const auto sysroot_manifest = find_lsp_sysroot_manifest(workspace_roots);
    if (!sysroot_manifest.has_value()) {
        return std::nullopt;
    }
    const auto normalized_package_manifest =
        std::filesystem::path(AnalysisService::normalized_path_key(*package_manifest_path));
    const auto normalized_sysroot_manifest =
        std::filesystem::path(AnalysisService::normalized_path_key(*sysroot_manifest));
    if (normalized_package_manifest == normalized_sysroot_manifest) {
        return LspPackageGraphInput{
            .graph = sysroot_only_graph_for_lsp(*package_manifest, normalized_sysroot_manifest),
            .manifest_path = normalized_sysroot_manifest,
        };
    }

    auto workspace_manifest_path =
        find_nearest_named_file(package_manifest_path->parent_path(), "ahfl.workspace.toml");
    while (workspace_manifest_path.has_value()) {
        const auto workspace_manifest = load_lsp_workspace_manifest(*workspace_manifest_path);
        if (workspace_manifest.has_value() &&
            workspace_contains_package_manifest(
                *workspace_manifest, *workspace_manifest_path, *package_manifest_path)) {
            auto result = package_graph::build_package_graph_from_workspace(
                package_graph::WorkspaceBuildInput{
                    .workspace_manifest_path = *workspace_manifest_path,
                    .package_name = package_manifest->package_name,
                    .sysroot_manifest_path = *sysroot_manifest,
                });
            if (!result.has_errors() && result.graph.has_value()) {
                return LspPackageGraphInput{
                    .graph = std::move(*result.graph),
                    .manifest_path = *workspace_manifest_path,
                };
            }
            return std::nullopt;
        }

        workspace_manifest_path = find_nearest_named_file(
            workspace_manifest_path->parent_path().parent_path(), "ahfl.workspace.toml");
    }

    auto result =
        package_graph::build_package_graph_from_manifests(package_graph::ManifestBuildInput{
            .root_manifest_path = *package_manifest_path,
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (result.has_errors() || !result.graph.has_value()) {
        return std::nullopt;
    }

    return LspPackageGraphInput{
        .graph = std::move(*result.graph),
        .manifest_path = *package_manifest_path,
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

[[nodiscard]] ProjectInput
project_input_from_package_graph(const package_graph::PackageGraph &graph,
                                 const std::filesystem::path &requested_file,
                                 std::unordered_map<std::string, std::string> overlays) {
    ProjectInput input;
    const auto *root_package = root_package_for_lsp(graph);
    const auto fallback_entry =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    input.entry_files.push_back(root_package == nullptr ? fallback_entry
                                                        : lsp_target_entry_file(*root_package,
                                                                                fallback_entry));
    input.include_stdlib = false;
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
    return input;
}

[[nodiscard]] std::optional<std::string> single_module_name(const ast::Program &program) {
    std::optional<std::string> result;
    for (const auto &declaration : program.declarations) {
        if (!declaration || declaration->kind != ast::NodeKind::ModuleDecl) {
            continue;
        }

        const auto &module = static_cast<const ast::ModuleDecl &>(*declaration);
        if (!module.name) {
            return std::nullopt;
        }
        if (result.has_value()) {
            return std::nullopt;
        }
        result = module.name->spelling();
    }
    return result;
}

[[nodiscard]] std::filesystem::path module_path(std::string_view module_name) {
    std::filesystem::path relative;
    std::size_t start = 0;
    while (start < module_name.size()) {
        const auto separator = module_name.find("::", start);
        if (separator == std::string_view::npos) {
            relative /= std::string(module_name.substr(start));
            break;
        }
        relative /= std::string(module_name.substr(start, separator - start));
        start = separator + 2;
    }
    relative += ".ahfl";
    return relative;
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

void AnalysisService::set_workspace_roots(std::vector<std::filesystem::path> roots) {
    workspace_roots_.clear();
    workspace_roots_.reserve(roots.size());
    for (const auto &root : roots) {
        if (!root.empty()) {
            workspace_roots_.push_back(std::filesystem::path(normalized_path_key(root)));
        }
    }
    invalidate_all();
}

void AnalysisService::invalidate_all() {
    cache_.clear();
}

const LspAnalysisSnapshot *AnalysisService::snapshot_for_uri(const std::string &uri) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    const auto workspace_revision = store_.workspace_revision();
    if (const auto existing = cache_.find(uri); existing != cache_.end()) {
        const auto &snapshot = *existing->second;
        if (snapshot.document_version == document->version &&
            snapshot.document_revision == *revision && snapshot.content_hash == *hash &&
            snapshot.workspace_revision == workspace_revision) {
            return existing->second.get();
        }
    }

    auto snapshot = build_snapshot(uri);
    if (!snapshot) {
        return nullptr;
    }

    ++analysis_runs_;
    auto *snapshot_ptr = snapshot.get();
    cache_[uri] = std::move(snapshot);
    return snapshot_ptr;
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
    if (std::filesystem::exists(candidate, error)) {
        const auto canonical = std::filesystem::weakly_canonical(candidate, error);
        if (!error) {
            candidate = canonical.lexically_normal();
        }
    }
    return candidate.generic_string();
}

std::unique_ptr<LspAnalysisSnapshot> AnalysisService::build_snapshot(const std::string &uri) {
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

    const auto document_path = path_from_uri(uri);

    Frontend frontend;
    Resolver resolver;
    TypeChecker type_checker;
    Validator validator;

    if (document_path.has_value()) {
        const auto package_graph_input =
            build_lsp_package_graph_for_source(*document_path, workspace_roots_);
        if (package_graph_input.has_value()) {
            snapshot->project_aware = true;
            snapshot->package_graph_manifest = package_graph_input->manifest_path;

            auto project_input = project_input_from_package_graph(
                package_graph_input->graph, *document_path, open_document_overlays());
            auto project_result = frontend.parse_project(project_input);
            snapshot->project_result =
                std::make_unique<ProjectParseResult>(std::move(project_result));

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
    }

    auto parse_result = frontend.parse_text(document->uri, document->text);
    if (document_path.has_value() && parse_result.program && !parse_result.has_errors()) {
        const auto module_name = single_module_name(*parse_result.program);
        const auto is_std_source =
            module_name.has_value() && (*module_name == "std" || module_name->starts_with("std::"));
        const auto inferred_roots = infer_descriptorless_search_roots(*document_path, parse_result);
        if (!inferred_roots.empty()) {
            auto project_result = frontend.parse_project(ProjectInput{
                .entry_files = {std::filesystem::path(normalized_path_key(*document_path))},
                .search_roots = inferred_roots,
                .include_stdlib = !is_std_source,
                .source_overlays = open_document_overlays(),
            });
            snapshot->project_aware = true;
            snapshot->project_result =
                std::make_unique<ProjectParseResult>(std::move(project_result));

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
    }

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

std::vector<std::filesystem::path>
AnalysisService::infer_descriptorless_search_roots(const std::filesystem::path &source_path,
                                                   const ParseResult &parse_result) const {
    if (!parse_result.program) {
        return {};
    }

    const auto module_name = single_module_name(*parse_result.program);
    if (!module_name.has_value() || module_name->empty()) {
        return {};
    }

    const auto normalized_source = std::filesystem::path(normalized_path_key(source_path));
    auto relative = module_path(*module_name).lexically_normal();
    auto candidate = normalized_source;
    std::vector<std::filesystem::path> module_parts;
    for (const auto &part : relative) {
        module_parts.push_back(part);
    }
    if (module_parts.empty()) {
        return {};
    }

    for (auto it = module_parts.rbegin(); it != module_parts.rend(); ++it) {
        if (!candidate.has_filename() || candidate.filename() != *it) {
            return {};
        }
        candidate = candidate.parent_path();
    }

    std::vector<std::filesystem::path> roots;
    const auto inferred_root = std::filesystem::path(normalized_path_key(candidate));
    roots.push_back(inferred_root);

    for (const auto &workspace_root : workspace_roots_) {
        if (path_has_prefix(workspace_root, normalized_source) &&
            std::find(roots.begin(), roots.end(), workspace_root) == roots.end()) {
            roots.push_back(workspace_root);
        }
    }

    return roots;
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
