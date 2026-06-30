#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/ast.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <sstream>
#include <system_error>

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

[[nodiscard]] bool descriptor_contains_source(const ProjectDescriptor &descriptor,
                                              const std::filesystem::path &source_path) {
    const auto normalized_source =
        std::filesystem::path(AnalysisService::normalized_path_key(source_path));
    const auto project_root = std::filesystem::path(
        AnalysisService::normalized_path_key(descriptor.descriptor_path.parent_path()));
    if (path_has_prefix(project_root, normalized_source)) {
        return true;
    }
    for (const auto &entry : descriptor.entry_files) {
        if (std::filesystem::path(AnalysisService::normalized_path_key(entry)) ==
            normalized_source) {
            return true;
        }
    }
    for (const auto &root : descriptor.search_roots) {
        if (path_has_prefix(std::filesystem::path(AnalysisService::normalized_path_key(root)),
                            normalized_source)) {
            return true;
        }
    }
    return false;
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
    const auto project_descriptor =
        document_path.has_value() ? find_project_descriptor(*document_path) : std::nullopt;

    Frontend frontend;
    Resolver resolver;
    TypeChecker type_checker;
    Validator validator;

    if (project_descriptor.has_value()) {
        auto descriptor_result = frontend.load_project_descriptor(*project_descriptor);
        if (!descriptor_result.has_errors() && descriptor_result.descriptor.has_value()) {
            snapshot->project_aware = true;
            snapshot->project_descriptor = *project_descriptor;

            auto project_result = frontend.parse_project(ProjectInput{
                .entry_files = descriptor_result.descriptor->entry_files,
                .search_roots = descriptor_result.descriptor->search_roots,
                .source_overlays = open_document_overlays(),
            });
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
        const auto inferred_roots = infer_descriptorless_search_roots(*document_path, parse_result);
        if (!inferred_roots.empty()) {
            auto project_result = frontend.parse_project(ProjectInput{
                .entry_files = {std::filesystem::path(normalized_path_key(*document_path))},
                .search_roots = inferred_roots,
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

std::optional<std::filesystem::path>
AnalysisService::find_project_descriptor(const std::filesystem::path &source_path) const {
    std::vector<std::filesystem::path> starts;
    if (!source_path.empty()) {
        starts.push_back(source_path.has_filename() ? source_path.parent_path() : source_path);
    }
    for (const auto &root : workspace_roots_) {
        starts.push_back(root);
    }

    for (auto start : starts) {
        if (start.empty()) {
            continue;
        }
        start = std::filesystem::path(normalized_path_key(start));
        while (!start.empty()) {
            const auto descriptor = start / "ahfl.project.json";
            std::error_code error;
            if (std::filesystem::is_regular_file(descriptor, error)) {
                return std::filesystem::path(normalized_path_key(descriptor));
            }

            const auto workspace_descriptor = start / "ahfl.workspace.json";
            if (std::filesystem::is_regular_file(workspace_descriptor, error)) {
                Frontend frontend;
                auto workspace_result = frontend.load_workspace_descriptor(workspace_descriptor);
                if (!workspace_result.has_errors() && workspace_result.descriptor.has_value()) {
                    for (const auto &project_path : workspace_result.descriptor->projects) {
                        auto project_result = frontend.load_project_descriptor(project_path);
                        if (project_result.has_errors() || !project_result.descriptor.has_value()) {
                            continue;
                        }

                        if (descriptor_contains_source(*project_result.descriptor, source_path)) {
                            return std::filesystem::path(
                                normalized_path_key(project_result.descriptor->descriptor_path));
                        }
                    }
                }
            }

            const auto parent = start.parent_path();
            if (parent == start || parent.empty()) {
                break;
            }
            start = parent;
        }
    }

    return std::nullopt;
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
