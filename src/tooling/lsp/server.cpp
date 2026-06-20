#include "tooling/lsp/server.hpp"

#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "tooling/formatter/formatter.hpp"
#include "tooling/lsp/code_action.hpp"
#include "tooling/lsp/code_lens.hpp"
#include "tooling/lsp/document_highlight.hpp"
#include "tooling/lsp/hover_service.hpp"
#include "tooling/lsp/selection_range.hpp"
#include "tooling/lsp/semantic_tokens.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace ahfl::lsp {

namespace {

constexpr std::string_view kTopLevelKeywords[] = {
    "module",
    "import",
    "struct",
    "enum",
    "type",
    "const",
    "capability",
    "predicate",
    "agent",
    "contract",
    "flow",
    "workflow",
};

constexpr std::string_view kExpressionKeywords[] = {
    "true",
    "false",
    "none",
    "some",
    "if",
    "else",
    "match",
    "return",
    "called",
    "running",
    "completed",
    "in_state",
    "always",
    "eventually",
};

constexpr std::string_view kAllKeywords[] = {
    "agent",   "workflow", "flow",     "contract",   "capability", "predicate",
    "struct",  "enum",     "const",    "import",     "module",     "state",
    "handler", "on",       "emit",     "transition", "always",     "eventually",
    "until",   "implies",  "requires", "ensures",    "invariant",  "after",
    "true",    "false",    "if",       "else",       "match",      "return",
};

[[nodiscard]] bool is_keyword(std::string_view text) {
    return std::find(std::begin(kAllKeywords), std::end(kAllKeywords), text) !=
           std::end(kAllKeywords);
}

[[nodiscard]] bool is_identifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(text.front());
    if (std::isalpha(first) == 0 && text.front() != '_') {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(), [](char ch) {
        const auto value = static_cast<unsigned char>(ch);
        return std::isalnum(value) != 0 || ch == '_';
    });
}

[[nodiscard]] bool is_identifier_char(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] bool contains(SourceRange range, std::size_t offset) noexcept {
    return range.begin_offset <= offset && offset <= range.end_offset;
}

[[nodiscard]] bool same_source(std::optional<SourceId> lhs, std::optional<SourceId> rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || *lhs == *rhs;
}

[[nodiscard]] Position to_lsp_position(const SourceFile &source, std::size_t offset) {
    const auto pos = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(pos.line > 0 ? pos.line - 1 : 0),
        .character = static_cast<std::uint32_t>(pos.column > 0 ? pos.column - 1 : 0),
    };
}

[[nodiscard]] Range to_lsp_range(const SourceFile &source, SourceRange range) {
    const auto begin = std::min(range.begin_offset, source.content.size());
    const auto end = std::max(begin, std::min(range.end_offset, source.content.size()));
    return Range{
        .start = to_lsp_position(source, begin),
        .end = to_lsp_position(source, end),
    };
}

[[nodiscard]] std::size_t offset_at(const SourceFile &source, Position pos) {
    return source.offset_of(pos.line + 1, pos.character + 1);
}

[[nodiscard]] LspSymbolKind to_lsp_symbol_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
        return LspSymbolKind::Struct;
    case SymbolKind::Enum:
        return LspSymbolKind::Enum;
    case SymbolKind::TypeAlias:
        return LspSymbolKind::Class;
    case SymbolKind::Const:
        return LspSymbolKind::Constant;
    case SymbolKind::Capability:
        return LspSymbolKind::Interface;
    case SymbolKind::Predicate:
        return LspSymbolKind::Function;
    case SymbolKind::Agent:
        return LspSymbolKind::Class;
    case SymbolKind::Workflow:
        return LspSymbolKind::Function;
    }
    return LspSymbolKind::Variable;
}

[[nodiscard]] CompletionItemKind to_completion_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
        return CompletionItemKind::Struct;
    case SymbolKind::Enum:
        return CompletionItemKind::Enum;
    case SymbolKind::Capability:
        return CompletionItemKind::Interface;
    case SymbolKind::Predicate:
        return CompletionItemKind::Function;
    case SymbolKind::Const:
        return CompletionItemKind::Constant;
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
    case SymbolKind::TypeAlias:
        return CompletionItemKind::Variable;
    }
    return CompletionItemKind::Text;
}

[[nodiscard]] std::string symbol_detail(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
        return "struct";
    case SymbolKind::Enum:
        return "enum";
    case SymbolKind::TypeAlias:
        return "type alias";
    case SymbolKind::Const:
        return "const";
    case SymbolKind::Capability:
        return "capability";
    case SymbolKind::Predicate:
        return "predicate";
    case SymbolKind::Agent:
        return "agent";
    case SymbolKind::Workflow:
        return "workflow";
    }
    return {};
}

[[nodiscard]] const LspSourceSnapshot *symbol_source(const LspAnalysisSnapshot &snapshot,
                                                     const Symbol &symbol,
                                                     const LspSourceSnapshot &fallback) {
    if (symbol.source_id.has_value()) {
        if (const auto *source = snapshot.source_for_id(*symbol.source_id); source != nullptr) {
            return source;
        }
    }
    return &fallback;
}

[[nodiscard]] std::optional<SourceRange>
symbol_selection_source_range(const Symbol &symbol, const LspSourceSnapshot &source) {
    if (source.source == nullptr || symbol.local_name.empty()) {
        return std::nullopt;
    }

    const auto &content = source.source->content;
    const auto begin = std::min(symbol.declaration_range.begin_offset, content.size());
    const auto end = std::max(begin, std::min(symbol.declaration_range.end_offset, content.size()));
    std::size_t cursor = begin;
    while (cursor < end) {
        const auto found = content.find(symbol.local_name, cursor);
        if (found == std::string::npos || found + symbol.local_name.size() > end) {
            return std::nullopt;
        }

        const auto before_ok = found == 0 || !is_identifier_char(content[found - 1]);
        const auto after = found + symbol.local_name.size();
        const auto after_ok = after >= content.size() || !is_identifier_char(content[after]);
        if (before_ok && after_ok) {
            return SourceRange{
                .begin_offset = found,
                .end_offset = after,
            };
        }

        cursor = found + 1;
    }

    return std::nullopt;
}

[[nodiscard]] SourceRange symbol_navigation_source_range(const Symbol &symbol,
                                                         const LspSourceSnapshot &source) {
    if (const auto selection = symbol_selection_source_range(symbol, source);
        selection.has_value()) {
        return *selection;
    }
    return symbol.declaration_range;
}

[[nodiscard]] const LspSourceSnapshot *reference_source(const LspAnalysisSnapshot &snapshot,
                                                        const ResolvedReference &reference,
                                                        const LspSourceSnapshot &fallback) {
    if (reference.source_id.has_value()) {
        if (const auto *source = snapshot.source_for_id(*reference.source_id); source != nullptr) {
            return source;
        }
    }
    return &fallback;
}

[[nodiscard]] std::optional<Location> symbol_location(const LspAnalysisSnapshot &snapshot,
                                                      const Symbol &symbol,
                                                      const LspSourceSnapshot &fallback) {
    const auto *source = symbol_source(snapshot, symbol, fallback);
    if (source == nullptr || source->source == nullptr) {
        return std::nullopt;
    }
    return Location{
        .uri = source->uri,
        .range = to_lsp_range(*source->source, symbol_navigation_source_range(symbol, *source)),
    };
}

[[nodiscard]] std::optional<Location> reference_location(const LspAnalysisSnapshot &snapshot,
                                                         const ResolvedReference &reference,
                                                         const LspSourceSnapshot &fallback) {
    const auto *source = reference_source(snapshot, reference, fallback);
    if (source == nullptr || source->source == nullptr) {
        return std::nullopt;
    }
    return Location{
        .uri = source->uri,
        .range = to_lsp_range(*source->source, reference.range),
    };
}

[[nodiscard]] std::optional<SymbolId> symbol_at(const LspAnalysisSnapshot &snapshot,
                                                const LspSourceSnapshot &source,
                                                std::size_t offset) {
    for (const auto &reference : snapshot.resolve_result.references()) {
        if (same_source(reference.source_id, source.source_id) &&
            contains(reference.range, offset)) {
            return reference.target;
        }
    }

    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (!same_source(symbol.source_id, source.source_id)) {
            continue;
        }
        const auto selection = symbol_selection_source_range(symbol, source);
        if (selection.has_value() && contains(*selection, offset)) {
            return symbol.id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<Location> rename_location_at(const LspAnalysisSnapshot &snapshot,
                                                         const LspSourceSnapshot &source,
                                                         std::size_t offset) {
    for (const auto &reference : snapshot.resolve_result.references()) {
        if (same_source(reference.source_id, source.source_id) &&
            contains(reference.range, offset)) {
            return reference_location(snapshot, reference, source);
        }
    }

    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (!same_source(symbol.source_id, source.source_id)) {
            continue;
        }
        const auto selection = symbol_selection_source_range(symbol, source);
        if (selection.has_value() && contains(*selection, offset) && source.source != nullptr) {
            return Location{
                .uri = source.uri,
                .range = to_lsp_range(*source.source, *selection),
            };
        }
    }

    return std::nullopt;
}

void send_null(JsonRpcTransport &transport, const std::string &id) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = json::JsonValue::make_null();
    transport.send_response(resp);
}

void send_empty_array(JsonRpcTransport &transport, const std::string &id) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = json::JsonValue::make_array();
    transport.send_response(resp);
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_full_diagnostic_report(const std::vector<LspDiagnostic> &diagnostics,
                                 std::optional<std::string_view> result_id = std::nullopt) {
    auto report = json::JsonValue::make_object();
    report->set("kind", json::JsonValue::make_string("full"));

    auto items = json::JsonValue::make_array();
    for (const auto &diagnostic : diagnostics) {
        items->push(serialize_diagnostic(diagnostic));
    }
    report->set("items", std::move(items));

    if (result_id.has_value()) {
        report->set("resultId", json::JsonValue::make_string(std::string(*result_id)));
    }
    return report;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_workspace_diagnostic_item(
    std::string_view uri,
    std::optional<int> version,
    const std::vector<LspDiagnostic> &diagnostics,
    std::optional<std::string_view> result_id = std::nullopt) {
    auto item = serialize_full_diagnostic_report(diagnostics, result_id);
    item->set("uri", json::JsonValue::make_string(std::string(uri)));
    if (version.has_value()) {
        item->set("version", json::JsonValue::make_int(*version));
    } else {
        item->set("version", json::JsonValue::make_null());
    }
    return item;
}

void send_invalid_params(JsonRpcTransport &transport, const std::string &id, std::string message) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.error = JsonRpcError{kInvalidParams, std::move(message)};
    transport.send_response(resp);
}

[[nodiscard]] std::string compute_document_result_id(const LspAnalysisSnapshot *snapshot,
                                                     std::string_view uri) {
    if (snapshot == nullptr) {
        return std::string(uri) + "#v0";
    }
    const auto *source = snapshot->source_for_uri(uri);
    const std::uint64_t content_hash =
        source != nullptr && source->source != nullptr
            ? std::hash<std::string>{}(source->source->content)
            : snapshot->content_hash;
    return std::string(uri) + "#v" + std::to_string(snapshot->document_version) + "-" +
           std::to_string(snapshot->document_revision) + "-" +
           std::to_string(snapshot->workspace_revision) + "-" +
           std::to_string(content_hash);
}

[[nodiscard]] bool text_document_id(const json::JsonValue &params, std::string &uri) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return false;
    }
    uri = parse_text_document_identifier(*td).uri;
    return !uri.empty();
}

[[nodiscard]] bool
text_document_position(const json::JsonValue &params, std::string &uri, Position &position) {
    const auto *td = params.get("textDocument");
    const auto *pos_val = params.get("position");
    if (td == nullptr || pos_val == nullptr) {
        return false;
    }
    uri = parse_text_document_identifier(*td).uri;
    position = parse_position(*pos_val);
    return !uri.empty();
}

[[nodiscard]] std::string read_string_field(const json::JsonValue &params, std::string_view name) {
    const auto *field = params.get(name);
    if (field == nullptr) {
        return {};
    }
    const auto value = field->as_string();
    return value.has_value() ? std::string(*value) : std::string{};
}

[[nodiscard]] MarkupKind preferred_hover_markup_kind(const json::JsonValue *params) {
    if (params == nullptr) {
        return MarkupKind::Markdown;
    }
    const auto *capabilities = params->get("capabilities");
    const auto *text_document =
        capabilities != nullptr ? capabilities->get("textDocument") : nullptr;
    const auto *hover = text_document != nullptr ? text_document->get("hover") : nullptr;
    const auto *content_format = hover != nullptr ? hover->get("contentFormat") : nullptr;
    if (content_format == nullptr || !content_format->is_array()) {
        return MarkupKind::Markdown;
    }
    for (const auto &item : content_format->array_items) {
        if (item == nullptr) {
            continue;
        }
        const auto value = item->as_string();
        if (!value.has_value()) {
            continue;
        }
        if (*value == "markdown") {
            return MarkupKind::Markdown;
        }
        if (*value == "plaintext") {
            return MarkupKind::Plaintext;
        }
    }
    return MarkupKind::Markdown;
}

[[nodiscard]] const json::JsonValue *hover_initialization_options(const json::JsonValue *params) {
    if (params == nullptr) {
        return nullptr;
    }
    const auto *init_options = params->get("initializationOptions");
    if (init_options == nullptr || !init_options->is_object()) {
        return nullptr;
    }
    if (const auto *hover = init_options->get("hover"); hover != nullptr && hover->is_object()) {
        return hover;
    }
    const auto *ahfl = init_options->get("ahfl");
    if (ahfl == nullptr || !ahfl->is_object()) {
        return nullptr;
    }
    const auto *hover = ahfl->get("hover");
    return hover != nullptr && hover->is_object() ? hover : nullptr;
}

void apply_hover_detail_level(HoverRenderOptions &options, const json::JsonValue &hover_options) {
    const auto *value = hover_options.get("detailLevel");
    if (value == nullptr) {
        return;
    }
    const auto text = value->as_string();
    if (!text.has_value()) {
        return;
    }
    if (*text == "compact") {
        options.detail_level = HoverDetailLevel::Compact;
    } else if (*text == "debug") {
        options.detail_level = HoverDetailLevel::Debug;
    } else {
        options.detail_level = HoverDetailLevel::Standard;
    }
}

void apply_hover_markup_kind(HoverRenderOptions &options, const json::JsonValue &hover_options) {
    const auto *value = hover_options.get("markupKind");
    if (value == nullptr) {
        return;
    }
    const auto text = value->as_string();
    if (!text.has_value()) {
        return;
    }
    if (*text == "plaintext") {
        options.markup_kind = MarkupKind::Plaintext;
    } else if (*text == "markdown") {
        options.markup_kind = MarkupKind::Markdown;
    }
}

void apply_hover_scalar_options(HoverRenderOptions &options, const json::JsonValue &hover_options) {
    if (const auto *show_source = hover_options.get("showSource"); show_source != nullptr) {
        if (const auto value = show_source->as_bool(); value.has_value()) {
            options.show_source = *value;
        }
    }
    if (const auto *max_facts = hover_options.get("maxFacts"); max_facts != nullptr) {
        if (const auto value = max_facts->as_int(); value.has_value() && *value >= 0) {
            options.max_facts = static_cast<std::size_t>(*value);
        }
    }
}

[[nodiscard]] HoverRenderOptions
hover_render_options_from_initialize(const json::JsonValue *params) {
    HoverRenderOptions options;
    options.markup_kind = preferred_hover_markup_kind(params);
    if (const auto *hover_options = hover_initialization_options(params);
        hover_options != nullptr) {
        apply_hover_detail_level(options, *hover_options);
        apply_hover_markup_kind(options, *hover_options);
        apply_hover_scalar_options(options, *hover_options);
    }
    return options;
}

[[nodiscard]] std::vector<std::filesystem::path>
workspace_roots_from_initialize(const json::JsonValue *params) {
    std::vector<std::filesystem::path> roots;
    if (params == nullptr) {
        return roots;
    }

    const auto append_root_uri = [&roots](const json::JsonValue *value) {
        if (value == nullptr) {
            return;
        }
        const auto uri = value->as_string();
        if (!uri.has_value()) {
            return;
        }
        if (auto path = AnalysisService::path_from_uri(*uri); path.has_value()) {
            roots.push_back(*path);
        }
    };

    append_root_uri(params->get("rootUri"));

    if (const auto *root_path = params->get("rootPath"); root_path != nullptr) {
        const auto path = root_path->as_string();
        if (path.has_value() && !path->empty()) {
            roots.emplace_back(std::string(*path));
        }
    }

    if (const auto *folders = params->get("workspaceFolders");
        folders != nullptr && folders->is_array()) {
        for (const auto &folder : folders->array_items) {
            append_root_uri(folder->get("uri"));
        }
    }

    return roots;
}

void append_workspace_folder_roots(std::vector<std::filesystem::path> &roots,
                                   const json::JsonValue *folders) {
    if (folders == nullptr || !folders->is_array()) {
        return;
    }

    for (const auto &folder : folders->array_items) {
        const auto *uri_value = folder->get("uri");
        if (uri_value == nullptr) {
            continue;
        }
        const auto uri = uri_value->as_string();
        if (!uri.has_value()) {
            continue;
        }
        if (auto path = AnalysisService::path_from_uri(*uri); path.has_value()) {
            roots.push_back(*path);
        }
    }
}

void remove_workspace_folder_roots(std::vector<std::filesystem::path> &roots,
                                   const json::JsonValue *folders) {
    if (folders == nullptr || !folders->is_array()) {
        return;
    }

    std::unordered_set<std::string> removed;
    for (const auto &folder : folders->array_items) {
        const auto *uri_value = folder->get("uri");
        if (uri_value == nullptr) {
            continue;
        }
        const auto uri = uri_value->as_string();
        if (!uri.has_value()) {
            continue;
        }
        if (auto path = AnalysisService::path_from_uri(*uri); path.has_value()) {
            removed.insert(AnalysisService::normalized_path_key(*path));
        }
    }

    std::erase_if(roots, [&removed](const std::filesystem::path &root) {
        return removed.contains(AnalysisService::normalized_path_key(root));
    });
}

void push_keyword_completion(std::vector<CompletionItem> &items, std::string_view keyword) {
    CompletionItem item;
    item.label = std::string(keyword);
    item.kind = CompletionItemKind::Keyword;
    items.push_back(std::move(item));
}

void push_symbol_completion(std::vector<CompletionItem> &items, const Symbol &symbol) {
    CompletionItem item;
    item.label = symbol.local_name;
    item.kind = to_completion_kind(symbol.kind);
    item.detail = symbol_detail(symbol.kind);
    items.push_back(std::move(item));
}

[[nodiscard]] bool looks_like_type_position(const SourceFile &source, std::size_t offset) {
    const auto &text = source.content;
    if (offset > text.size()) {
        offset = text.size();
    }
    while (offset > 0 && std::isspace(static_cast<unsigned char>(text[offset - 1])) != 0) {
        --offset;
    }
    if (offset == 0) {
        return false;
    }
    if (text[offset - 1] == ':') {
        auto word_end = offset - 1;
        while (word_end > 0 && std::isspace(static_cast<unsigned char>(text[word_end - 1])) != 0) {
            --word_end;
        }
        auto word_begin = word_end;
        while (word_begin > 0) {
            const auto ch = static_cast<unsigned char>(text[word_begin - 1]);
            if (std::isalnum(ch) == 0 && text[word_begin - 1] != '_') {
                break;
            }
            --word_begin;
        }
        const auto label = std::string_view{text}.substr(word_begin, word_end - word_begin);
        if (label == "return" || label == "safety" || label == "liveness") {
            return false;
        }
        return true;
    }
    return offset >= 2 && text[offset - 2] == '-' && text[offset - 1] == '>';
}

[[nodiscard]] std::optional<std::string> member_root_before_cursor(const SourceFile &source,
                                                                   std::size_t offset) {
    const auto &text = source.content;
    if (offset > text.size()) {
        offset = text.size();
    }
    while (offset > 0 && std::isspace(static_cast<unsigned char>(text[offset - 1])) != 0) {
        --offset;
    }
    if (offset == 0 || text[offset - 1] != '.') {
        return std::nullopt;
    }

    auto end = offset - 1;
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    auto begin = end;
    while (begin > 0) {
        const auto ch = static_cast<unsigned char>(text[begin - 1]);
        if (std::isalnum(ch) == 0 && text[begin - 1] != '_') {
            break;
        }
        --begin;
    }
    if (begin == end) {
        return std::nullopt;
    }
    return text.substr(begin, end - begin);
}

[[nodiscard]] const FlowTypeInfo *
flow_at(const TypeEnvironment &environment, std::optional<SourceId> source_id, std::size_t offset) {
    for (const auto &[_, flow] : environment.flows()) {
        (void)_;
        if (!contains(flow.declaration_range, offset)) {
            continue;
        }
        (void)source_id;
        return &flow;
    }
    return nullptr;
}

[[nodiscard]] const WorkflowTypeInfo *workflow_at(const TypeEnvironment &environment,
                                                  std::size_t offset) {
    for (const auto &[_, workflow] : environment.workflows()) {
        (void)_;
        if (contains(workflow.declaration_range, offset) ||
            contains(workflow.return_value_range, offset)) {
            return &workflow;
        }
        for (const auto &node : workflow.nodes) {
            if (contains(node.source_range, offset) || contains(node.input_expr_range, offset) ||
                contains(node.target_range, offset)) {
                return &workflow;
            }
        }
        for (const auto &range : workflow.safety_ranges) {
            if (contains(range, offset)) {
                return &workflow;
            }
        }
        for (const auto &range : workflow.liveness_ranges) {
            if (contains(range, offset)) {
                return &workflow;
            }
        }
    }
    return nullptr;
}

void push_struct_field_completions(std::vector<CompletionItem> &items,
                                   const TypeEnvironment &environment,
                                   TypePtr type) {
    if (type == nullptr) {
        return;
    }
    const auto struct_info = environment.get_struct(*type);
    if (!struct_info.has_value()) {
        return;
    }
    for (const auto &field : struct_info->get().fields) {
        CompletionItem item;
        item.label = field.name;
        item.kind = CompletionItemKind::Variable;
        item.detail = field.type ? field.type->describe() : "field";
        items.push_back(std::move(item));
    }
}

void push_member_completions(std::vector<CompletionItem> &items,
                             const LspAnalysisSnapshot &snapshot,
                             const LspSourceSnapshot &source,
                             std::size_t offset,
                             std::string_view root_name) {
    if (!snapshot.type_check_result) {
        return;
    }

    const auto &environment = snapshot.type_check_result->environment;
    const auto *flow = flow_at(environment, source.source_id, offset);
    if (flow == nullptr) {
        return;
    }
    const auto agent = environment.get_agent(flow->target_symbol);
    if (!agent.has_value()) {
        return;
    }

    if (root_name == "input") {
        push_struct_field_completions(items, environment, agent->get().input_type);
    } else if (root_name == "context" || root_name == "ctx") {
        push_struct_field_completions(items, environment, agent->get().context_type);
    } else if (root_name == "output") {
        push_struct_field_completions(items, environment, agent->get().output_type);
    }
}

void push_state_completions(std::vector<CompletionItem> &items,
                            const LspAnalysisSnapshot &snapshot,
                            const LspSourceSnapshot &source,
                            std::size_t offset) {
    if (!snapshot.type_check_result) {
        return;
    }
    const auto &environment = snapshot.type_check_result->environment;
    const auto *flow = flow_at(environment, source.source_id, offset);
    if (flow == nullptr) {
        return;
    }
    const auto agent = environment.get_agent(flow->target_symbol);
    if (!agent.has_value()) {
        return;
    }
    for (const auto &state : agent->get().states) {
        CompletionItem item;
        item.label = state;
        item.kind = CompletionItemKind::Variable;
        item.detail = "agent state";
        items.push_back(std::move(item));
    }
}

void push_workflow_node_completions(std::vector<CompletionItem> &items,
                                    const LspAnalysisSnapshot &snapshot,
                                    std::size_t offset) {
    if (!snapshot.type_check_result) {
        return;
    }
    const auto *workflow = workflow_at(snapshot.type_check_result->environment, offset);
    if (workflow == nullptr) {
        return;
    }
    for (const auto &node : workflow->nodes) {
        CompletionItem item;
        item.label = node.name;
        item.kind = CompletionItemKind::Variable;
        item.detail = "workflow node";
        items.push_back(std::move(item));
    }
}

void push_enum_variant_completions(std::vector<CompletionItem> &items,
                                   const TypeEnvironment &environment) {
    for (const auto &[_, enum_info] : environment.enums()) {
        (void)_;
        for (const auto &variant : enum_info.variants) {
            CompletionItem item;
            item.label = enum_info.canonical_name + "::" + variant.name;
            item.kind = CompletionItemKind::Enum;
            item.detail = "enum variant";
            items.push_back(std::move(item));
        }
    }
}

[[nodiscard]] std::string callable_signature(const CapabilityTypeInfo &capability) {
    std::string label = capability.canonical_name + "(";
    for (std::size_t index = 0; index < capability.params.size(); ++index) {
        if (index > 0) {
            label += ", ";
        }
        const auto &param = capability.params[index];
        label += param.name + ": " + (param.type ? param.type->describe() : "?");
    }
    label += ")";
    if (capability.return_type != nullptr) {
        label += " -> " + capability.return_type->describe();
    }
    return label;
}

[[nodiscard]] std::string callable_signature(const PredicateTypeInfo &predicate) {
    std::string label = predicate.canonical_name + "(";
    for (std::size_t index = 0; index < predicate.params.size(); ++index) {
        if (index > 0) {
            label += ", ";
        }
        const auto &param = predicate.params[index];
        label += param.name + ": " + (param.type ? param.type->describe() : "?");
    }
    label += ")";
    return label;
}

template <typename CallableInfo>
void fill_signature_parameters(SignatureInformation &signature, const CallableInfo &callable) {
    for (const auto &param : callable.params) {
        ParameterInformation info;
        info.label = param.name + ": " + (param.type ? param.type->describe() : "?");
        info.documentation = param.type ? param.type->describe() : "?";
        signature.parameters.push_back(std::move(info));
    }
}

[[nodiscard]] std::optional<std::pair<std::string, int>>
call_context_before_cursor(const SourceFile &source, std::size_t offset) {
    const auto &text = source.content;
    if (offset > text.size()) {
        offset = text.size();
    }

    int paren_depth = 0;
    std::size_t open_paren = std::string::npos;
    for (std::size_t index = offset; index > 0; --index) {
        const char ch = text[index - 1];
        if (ch == ')') {
            ++paren_depth;
        } else if (ch == '(') {
            if (paren_depth == 0) {
                open_paren = index - 1;
                break;
            }
            --paren_depth;
        }
    }
    if (open_paren == std::string::npos) {
        return std::nullopt;
    }

    auto ident_end = open_paren;
    while (ident_end > 0 && std::isspace(static_cast<unsigned char>(text[ident_end - 1])) != 0) {
        --ident_end;
    }
    auto ident_begin = ident_end;
    while (ident_begin > 0) {
        const auto ch = static_cast<unsigned char>(text[ident_begin - 1]);
        if (std::isalnum(ch) == 0 && text[ident_begin - 1] != '_') {
            break;
        }
        --ident_begin;
    }
    if (ident_begin == ident_end) {
        return std::nullopt;
    }

    int active_parameter = 0;
    int nested_depth = 0;
    for (std::size_t index = open_paren + 1; index < offset && index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '(') {
            ++nested_depth;
        } else if (ch == ')') {
            --nested_depth;
        } else if (ch == ',' && nested_depth == 0) {
            ++active_parameter;
        }
    }

    return std::pair{text.substr(ident_begin, ident_end - ident_begin), active_parameter};
}

void append_symbol_children(DocumentSymbol &symbol,
                            const LspAnalysisSnapshot &snapshot,
                            const Symbol &semantic_symbol,
                            const LspSourceSnapshot &source) {
    if (!snapshot.type_check_result) {
        return;
    }
    const auto &environment = snapshot.type_check_result->environment;

    if (semantic_symbol.kind == SymbolKind::Agent) {
        const auto agent = environment.get_agent(semantic_symbol.id);
        if (!agent.has_value()) {
            return;
        }
        for (const auto &state : agent->get().states) {
            DocumentSymbol child;
            child.name = state;
            child.kind = LspSymbolKind::Property;
            child.range = symbol.range;
            child.selection_range = symbol.selection_range;
            symbol.children.push_back(std::move(child));
        }
        return;
    }

    if (semantic_symbol.kind == SymbolKind::Workflow) {
        const auto workflow = environment.get_workflow(semantic_symbol.id);
        if (!workflow.has_value()) {
            return;
        }
        for (const auto &node : workflow->get().nodes) {
            DocumentSymbol child;
            child.name = node.name;
            child.kind = LspSymbolKind::Function;
            child.range = to_lsp_range(*source.source, node.source_range);
            child.selection_range = child.range;
            symbol.children.push_back(std::move(child));
        }
        return;
    }
}

/// Simple folding range computation based on braces and top-level declarations.
[[nodiscard]] std::vector<FoldingRange> compute_folding_ranges(const std::string &source) {
    std::vector<FoldingRange> ranges;
    std::vector<std::uint32_t> brace_stack;
    std::uint32_t line_number = 0;
    std::uint32_t last_top_level_start = 0;
    bool in_top_level = false;

    std::size_t pos = 0;
    while (pos < source.size()) {
        auto newline_pos = source.find('\n', pos);
        if (newline_pos == std::string::npos) {
            newline_pos = source.size();
        }
        std::string_view line{source.data() + pos, newline_pos - pos};

        // Track braces for region-based folding
        for (char ch : line) {
            if (ch == '{') {
                brace_stack.push_back(line_number);
            } else if (ch == '}' && !brace_stack.empty()) {
                std::uint32_t start = brace_stack.back();
                brace_stack.pop_back();
                if (line_number > start) {
                    ranges.push_back(FoldingRange{
                        .start_line = start,
                        .end_line = line_number,
                        .kind = FoldingRangeKind::Region,
                    });
                }
            }
        }

        // Detect top-level declaration starts for region folding
        for (auto kw : kTopLevelKeywords) {
            if (line.starts_with(kw) ||
                (line.size() > kw.size() &&
                 line.substr(0, kw.size()) == kw &&
                 (line[kw.size()] == ' ' || line[kw.size()] == '\t'))) {
                if (in_top_level && line_number > last_top_level_start) {
                    ranges.push_back(FoldingRange{
                        .start_line = last_top_level_start,
                        .end_line = line_number - 1,
                        .kind = FoldingRangeKind::Region,
                    });
                }
                last_top_level_start = line_number;
                in_top_level = true;
                break;
            }
        }

        if (newline_pos == source.size()) {
            break;
        }
        pos = newline_pos + 1;
        ++line_number;
    }

    // Final top-level range
    if (in_top_level && line_number > last_top_level_start) {
        ranges.push_back(FoldingRange{
            .start_line = last_top_level_start,
            .end_line = line_number,
            .kind = FoldingRangeKind::Region,
        });
    }

    return ranges;
}

} // anonymous namespace

LspServer::LspServer(std::istream &in, std::ostream &out)
    : transport_(in, out), analysis_(store_),
      trace_enabled_(std::getenv("AHFL_LSP_TRACE") != nullptr) {}

void LspServer::run() {
    while (true) {
        auto msg = transport_.read_message();
        if (!msg.has_value()) {
            break;
        }

        std::visit(
            [this](auto &m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, JsonRpcRequest>) {
                    handle_request(m);
                } else {
                    handle_notification(m);
                }
            },
            *msg);

        if (shutdown_requested_) {
            break;
        }
    }
}

void LspServer::handle_request(const JsonRpcRequest &req) {
    trace("request " + req.method);
    if (req.method == "initialize") {
        handle_initialize(req);
    } else if (req.method == "shutdown") {
        handle_shutdown(req);
    } else if (req.method == "textDocument/completion") {
        handle_completion(req);
    } else if (req.method == "textDocument/definition") {
        handle_definition(req);
    } else if (req.method == "textDocument/hover") {
        handle_hover(req);
    } else if (req.method == "textDocument/references") {
        handle_references(req);
    } else if (req.method == "textDocument/prepareRename") {
        handle_prepare_rename(req);
    } else if (req.method == "textDocument/rename") {
        handle_rename(req);
    } else if (req.method == "textDocument/diagnostic") {
        handle_text_document_diagnostic(req);
    } else if (req.method == "workspace/diagnostic") {
        handle_workspace_diagnostic(req);
    } else if (req.method == "textDocument/documentSymbol") {
        handle_document_symbol(req);
    } else if (req.method == "workspace/symbol") {
        handle_workspace_symbol(req);
    } else if (req.method == "textDocument/signatureHelp") {
        handle_signature_help(req);
    } else if (req.method == "textDocument/formatting") {
        handle_document_formatting(req);
    } else if (req.method == "textDocument/semanticTokens/full") {
        handle_semantic_tokens_full(req);
    } else if (req.method == "textDocument/codeAction") {
        handle_code_action(req);
    } else if (req.method == "textDocument/foldingRange") {
        handle_folding_range(req);
    } else if (req.method == "textDocument/documentHighlight") {
        handle_document_highlight(req);
    } else if (req.method == "textDocument/selectionRange") {
        handle_selection_range(req);
    } else if (req.method == "textDocument/codeLens") {
        handle_code_lens(req);
    } else {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.error = JsonRpcError{kMethodNotFound, "Method not found: " + req.method};
        transport_.send_response(resp);
    }
}

void LspServer::handle_notification(const JsonRpcNotification &notif) {
    trace("notification " + notif.method);
    if (notif.method == "initialized") {
        handle_initialized();
    } else if (notif.method == "textDocument/didOpen") {
        if (notif.params) {
            handle_did_open(*notif.params);
        }
    } else if (notif.method == "textDocument/didChange") {
        if (notif.params) {
            handle_did_change(*notif.params);
        }
    } else if (notif.method == "textDocument/didClose") {
        if (notif.params) {
            handle_did_close(*notif.params);
        }
    } else if (notif.method == "workspace/didChangeWorkspaceFolders") {
        if (notif.params) {
            handle_workspace_folders_changed(*notif.params);
        }
    } else if (notif.method == "workspace/didChangeWatchedFiles") {
        if (notif.params) {
            handle_watched_files_changed(*notif.params);
        }
    } else if (notif.method == "exit") {
        handle_exit();
    }
}

void LspServer::handle_initialize(const JsonRpcRequest &req) {
    initialized_ = true;
    workspace_roots_ = workspace_roots_from_initialize(req.params.get());
    analysis_.set_workspace_roots(workspace_roots_);
    hover_options_ = hover_render_options_from_initialize(req.params.get());

    ServerCapabilities caps;
    caps.document_formatting_provider = true;
    caps.semantic_tokens_provider = true;
    caps.code_action_provider = true;
    caps.folding_range_provider = true;
    caps.document_highlight_provider = true;
    caps.selection_range_provider = true;
    caps.code_lens_provider = true;
    auto result = json::JsonValue::make_object();
    result->set("capabilities", serialize_server_capabilities(caps));

    auto info = json::JsonValue::make_object();
    info->set("name", json::JsonValue::make_string("ahfl-lsp"));
    info->set("version", json::JsonValue::make_string("0.1.0"));
    result->set("serverInfo", std::move(info));

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_shutdown(const JsonRpcRequest &req) {
    shutdown_requested_ = true;

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = json::JsonValue::make_null();
    transport_.send_response(resp);
}

void LspServer::handle_initialized() {}

void LspServer::handle_did_open(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    auto item = parse_text_document_item(*td);
    store_.open(item);
    analysis_.invalidate_all();
    send_diagnostic_refresh();
}

void LspServer::handle_did_change(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    const auto versioned = parse_versioned_text_document_identifier(*td);
    const auto *changes = params.get("contentChanges");
    if (changes == nullptr || !changes->is_array() || changes->array_items.empty()) {
        return;
    }

    const auto &last_change = *changes->array_items.back();
    const auto *text_opt = last_change.get("text");
    if (text_opt == nullptr) {
        return;
    }
    const auto text = text_opt->as_string();
    if (!text.has_value()) {
        return;
    }

    store_.change(versioned.uri, versioned.version, std::string(*text));
    analysis_.invalidate_all();
    send_diagnostic_refresh();
}

void LspServer::handle_did_close(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    const auto id = parse_text_document_identifier(*td);
    store_.close(id.uri);
    analysis_.invalidate_all();
    send_diagnostic_refresh();
}

void LspServer::handle_workspace_folders_changed(const json::JsonValue &params) {
    const auto *event = params.get("event");
    if (event == nullptr) {
        return;
    }

    remove_workspace_folder_roots(workspace_roots_, event->get("removed"));
    append_workspace_folder_roots(workspace_roots_, event->get("added"));
    analysis_.set_workspace_roots(workspace_roots_);
    send_diagnostic_refresh();
}

void LspServer::handle_watched_files_changed(const json::JsonValue &params) {
    (void)params;
    analysis_.invalidate_all();
    send_diagnostic_refresh();
}

void LspServer::handle_exit() {
    shutdown_requested_ = true;
}

void LspServer::send_diagnostic_refresh() {
    trace("diagnostic/refresh");
    auto params = json::JsonValue::make_object();
    transport_.send_notification("$/diagnostic/refresh", std::move(params));
}

void LspServer::trace(std::string_view message) const {
    if (!trace_enabled_) {
        return;
    }
    std::clog << "[ahfl-lsp] " << message << '\n';
}

void LspServer::handle_completion(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto offset = offset_at(*source->source, position);
    std::vector<CompletionItem> items;

    if (const auto root = member_root_before_cursor(*source->source, offset); root.has_value()) {
        push_member_completions(items, *snapshot, *source, offset, *root);
    } else if (looks_like_type_position(*source->source, offset)) {
        for (const auto &symbol : snapshot->resolve_result.symbol_table.symbols()) {
            if (symbol.kind == SymbolKind::Struct || symbol.kind == SymbolKind::Enum ||
                symbol.kind == SymbolKind::TypeAlias) {
                push_symbol_completion(items, symbol);
            }
        }
    } else {
        for (const auto keyword : kTopLevelKeywords) {
            push_keyword_completion(items, keyword);
        }
        for (const auto keyword : kExpressionKeywords) {
            push_keyword_completion(items, keyword);
        }
        for (const auto &symbol : snapshot->resolve_result.symbol_table.symbols()) {
            push_symbol_completion(items, symbol);
        }
        if (snapshot->type_check_result) {
            push_enum_variant_completions(items, snapshot->type_check_result->environment);
        }
        push_state_completions(items, *snapshot, *source, offset);
        push_workflow_node_completions(items, *snapshot, offset);
    }

    auto result = json::JsonValue::make_array();
    for (const auto &item : items) {
        result->push(serialize_completion_item(item));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_definition(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_null(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }

    const auto target = symbol_at(*snapshot, *source, offset_at(*source->source, position));
    if (!target.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
    if (!symbol.has_value()) {
        send_null(transport_, req.id);
        return;
    }
    const auto location = symbol_location(*snapshot, symbol->get(), *source);
    if (!location.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_location(*location);
    transport_.send_response(resp);
}

void LspServer::handle_hover(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_null(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }

    const HoverService hover_service(hover_options_);
    if (auto hover = hover_service.hover_at(*snapshot, *source, position); hover.has_value()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_hover(*hover);
        transport_.send_response(resp);
        return;
    }
    send_null(transport_, req.id);
}

void LspServer::handle_references(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto target = symbol_at(*snapshot, *source, offset_at(*source->source, position));
    auto result = json::JsonValue::make_array();
    if (target.has_value()) {
        bool include_declaration = false;
        if (const auto *context = req.params->get("context"); context != nullptr) {
            if (const auto *include = context->get("includeDeclaration"); include != nullptr) {
                if (const auto value = include->as_bool(); value.has_value()) {
                    include_declaration = *value;
                }
            }
        }

        if (include_declaration) {
            const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
            if (symbol.has_value()) {
                if (const auto location = symbol_location(*snapshot, symbol->get(), *source);
                    location.has_value()) {
                    result->push(serialize_location(*location));
                }
            }
        }

        for (const auto &reference : snapshot->resolve_result.references()) {
            if (reference.target == *target) {
                if (const auto location = reference_location(*snapshot, reference, *source);
                    location.has_value()) {
                    result->push(serialize_location(*location));
                }
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_prepare_rename(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_null(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }

    const auto location =
        rename_location_at(*snapshot, *source, offset_at(*source->source, position));
    if (!location.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_range(location->range);
    transport_.send_response(resp);
}

void LspServer::handle_rename(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_null(transport_, req.id);
        return;
    }

    const auto new_name = read_string_field(*req.params, "newName");
    if (!is_identifier(new_name) || is_keyword(new_name)) {
        send_invalid_params(transport_, req.id, "newName must be a non-keyword AHFL identifier");
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }

    const auto target = symbol_at(*snapshot, *source, offset_at(*source->source, position));
    if (!target.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
    if (!symbol.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    if (const auto conflict = snapshot->resolve_result.symbol_table.find_local(
            symbol->get().name_space, new_name, symbol->get().module_name);
        conflict.has_value() && !(conflict->get().id == symbol->get().id)) {
        send_invalid_params(transport_, req.id, "rename would conflict with an existing symbol");
        return;
    }

    WorkspaceEdit edit;
    if (const auto declaration = symbol_location(*snapshot, symbol->get(), *source);
        declaration.has_value()) {
        edit.changes[declaration->uri].push_back(TextEdit{
            .range = declaration->range,
            .new_text = new_name,
        });
    }
    for (const auto &reference : snapshot->resolve_result.references()) {
        if (reference.target == *target) {
            if (const auto location = reference_location(*snapshot, reference, *source);
                location.has_value()) {
                edit.changes[location->uri].push_back(TextEdit{
                    .range = location->range,
                    .new_text = new_name,
                });
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_workspace_edit(edit);
    transport_.send_response(resp);
}

void LspServer::handle_text_document_diagnostic(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_full_diagnostic_report({});
        transport_.send_response(resp);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_full_diagnostic_report({});
        transport_.send_response(resp);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto diagnostics =
        snapshot != nullptr ? snapshot->diagnostics_for_uri(uri) : std::vector<LspDiagnostic>{};

    const std::string result_id = compute_document_result_id(snapshot, uri);

    // Check if client sent a previousResultId; if unchanged, we still send full for now
    // (incremental updates can be added later)
    if (req.params) {
        if (const auto *prev_id = req.params->get("previousResultId"); prev_id != nullptr) {
            if (auto prev = prev_id->as_string(); prev.has_value() && *prev == result_id) {
                // Result unchanged - send empty items with same resultId
                JsonRpcResponse resp;
                resp.id = req.id;
                resp.result = serialize_full_diagnostic_report({}, result_id);
                transport_.send_response(resp);
                return;
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_full_diagnostic_report(diagnostics, result_id);
    transport_.send_response(resp);
}

void LspServer::handle_workspace_diagnostic(const JsonRpcRequest &req) {
    // Build map of previous result IDs if provided
    std::unordered_map<std::string, std::string> previous_ids;
    if (req.params) {
        if (const auto *prev_ids = req.params->get("previousResultIds");
            prev_ids != nullptr && prev_ids->is_object()) {
            for (const auto &[key, value] : prev_ids->object_fields) {
                if (value != nullptr) {
                    if (auto s = value->as_string(); s.has_value()) {
                        previous_ids[key] = *s;
                    }
                }
            }
        }
    }

    auto result = json::JsonValue::make_object();
    auto items = json::JsonValue::make_array();
    std::unordered_set<std::string> emitted;

    for (const auto *snapshot : analysis_.workspace_snapshots()) {
        if (snapshot == nullptr) {
            continue;
        }
        for (const auto &source : snapshot->sources) {
            if (source.uri.empty() || !emitted.insert(source.uri).second) {
                continue;
            }

            const auto *document = store_.get(source.uri);
            const std::optional<int> version =
                document != nullptr ? std::optional<int>(document->version) : std::nullopt;

            const auto diagnostics = snapshot->diagnostics_for_uri(source.uri);
            const std::string result_id = compute_document_result_id(snapshot, source.uri);

            // If previous result ID matches, skip this document (no changes)
            auto it = previous_ids.find(source.uri);
            if (it != previous_ids.end() && it->second == result_id) {
                continue;
            }

            items->push(serialize_workspace_diagnostic_item(
                source.uri, version, diagnostics, result_id));
        }
    }

    result->set("items", std::move(items));

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_document_symbol(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    auto result = json::JsonValue::make_array();
    for (const auto &symbol : snapshot->resolve_result.symbol_table.symbols()) {
        if (!same_source(symbol.source_id, source->source_id)) {
            continue;
        }

        DocumentSymbol doc_symbol;
        doc_symbol.name = symbol.local_name;
        doc_symbol.kind = to_lsp_symbol_kind(symbol.kind);
        doc_symbol.range = to_lsp_range(*source->source, symbol.declaration_range);
        doc_symbol.selection_range =
            to_lsp_range(*source->source, symbol_navigation_source_range(symbol, *source));
        append_symbol_children(doc_symbol, *snapshot, symbol, *source);
        result->push(serialize_document_symbol(doc_symbol));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_workspace_symbol(const JsonRpcRequest &req) {
    std::string query;
    if (req.params) {
        query = read_string_field(*req.params, "query");
    }

    auto result = json::JsonValue::make_array();
    std::unordered_set<std::string> emitted;
    for (const auto *snapshot : analysis_.workspace_snapshots()) {
        if (snapshot == nullptr) {
            continue;
        }
        const auto *fallback = snapshot->source_for_uri(snapshot->requested_uri);
        if (fallback == nullptr) {
            continue;
        }
        for (const auto &symbol : snapshot->resolve_result.symbol_table.symbols()) {
            if (!query.empty() && symbol.local_name.find(query) == std::string::npos &&
                symbol.canonical_name.find(query) == std::string::npos) {
                continue;
            }
            const auto location = symbol_location(*snapshot, symbol, *fallback);
            if (!location.has_value()) {
                continue;
            }

            const auto key = symbol.canonical_name + "@" + location->uri + ":" +
                             std::to_string(location->range.start.line) + ":" +
                             std::to_string(location->range.start.character);
            if (!emitted.insert(key).second) {
                continue;
            }

            SymbolInformation info;
            info.name = symbol.local_name;
            info.kind = to_lsp_symbol_kind(symbol.kind);
            info.location = *location;
            result->push(serialize_symbol_information(info));
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_signature_help(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_null(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr ||
        !snapshot->type_check_result) {
        send_null(transport_, req.id);
        return;
    }

    const auto offset = offset_at(*source->source, position);
    const auto context = call_context_before_cursor(*source->source, offset);
    if (!context.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    const auto &[callable_name, active_parameter] = *context;
    const auto &environment = snapshot->type_check_result->environment;
    SignatureHelp help;
    help.active_signature = 0;
    help.active_parameter = active_parameter;

    if (const auto symbol = snapshot->resolve_result.symbol_table.find_local(
            SymbolNamespace::Capabilities, callable_name);
        symbol.has_value()) {
        const auto capability = environment.get_capability(symbol->get().id);
        if (capability.has_value()) {
            SignatureInformation info;
            info.label = callable_signature(capability->get());
            info.documentation = "capability " + capability->get().canonical_name;
            fill_signature_parameters(info, capability->get());
            help.signatures.push_back(std::move(info));
        }
    }

    if (help.signatures.empty()) {
        if (const auto symbol = snapshot->resolve_result.symbol_table.find_local(
                SymbolNamespace::Predicates, callable_name);
            symbol.has_value()) {
            const auto predicate = environment.get_predicate(symbol->get().id);
            if (predicate.has_value()) {
                SignatureInformation info;
                info.label = callable_signature(predicate->get());
                info.documentation = "predicate " + predicate->get().canonical_name;
                fill_signature_parameters(info, predicate->get());
                help.signatures.push_back(std::move(info));
            }
        }
    }

    if (help.signatures.empty()) {
        send_null(transport_, req.id);
        return;
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_signature_help(help);
    transport_.send_response(resp);
}

void LspServer::handle_document_formatting(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    formatter::FormatOptions options;
    // Read options from params if provided
    if (const auto *options_val = req.params->get("options"); options_val != nullptr) {
        if (const auto *tab_size = options_val->get("tabSize"); tab_size != nullptr) {
            if (auto size = tab_size->as_int(); size.has_value() && *size > 0) {
                options.indent_width = static_cast<int>(*size);
            }
        }
        if (const auto *insert_spaces = options_val->get("insertSpaces");
            insert_spaces != nullptr) {
            if (auto spaces = insert_spaces->as_bool(); spaces.has_value()) {
                options.use_tabs = !*spaces;
            }
        }
    }

    auto result = formatter::format_source(document->text, options);
    if (!result.success || result.formatted == document->text) {
        send_empty_array(transport_, req.id);
        return;
    }

    // Compute full document range
    const auto &text = document->text;
    std::size_t line_count = 0;
    for (char c : text) {
        if (c == '\n') {
            ++line_count;
        }
    }

    // Find last line length
    std::size_t last_line_length = 0;
    auto last_newline = text.rfind('\n');
    if (last_newline != std::string::npos) {
        last_line_length = text.size() - last_newline - 1;
    } else {
        last_line_length = text.size();
    }

    TextEdit edit;
    edit.range.start = Position{0, 0};
    edit.range.end = Position{static_cast<uint32_t>(line_count),
                              static_cast<uint32_t>(last_line_length)};
    edit.new_text = result.formatted;

    auto edits_array = json::JsonValue::make_array();
    edits_array->push(serialize_text_edit(edit));

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(edits_array);
    transport_.send_response(resp);
}

void LspServer::handle_semantic_tokens_full(const JsonRpcRequest &req) {
    if (!req.params) {
        send_null(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_null(transport_, req.id);
        return;
    }

    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }

    auto tokens = compute_semantic_tokens(source->source->content, analysis_);
    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_semantic_tokens(tokens);
    transport_.send_response(resp);
}

void LspServer::handle_code_action(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *range_val = req.params->get("range");
    if (range_val == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }
    auto range = parse_range(*range_val);

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::vector<LspDiagnostic> diagnostics;
    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    if (snapshot != nullptr) {
        diagnostics = snapshot->diagnostics_for_uri(uri);
    }

    auto actions = compute_code_actions(document->text, range, diagnostics);

    auto result = json::JsonValue::make_array();
    for (const auto &action : actions) {
        result->push(serialize_code_action(action));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_folding_range(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    auto ranges = compute_folding_ranges(document->text);

    auto result = json::JsonValue::make_array();
    for (const auto &r : ranges) {
        result->push(serialize_folding_range(r));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_document_highlight(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    Position position;
    if (!text_document_position(*req.params, uri, position)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    auto highlights = compute_document_highlights(document->text, position);

    auto result = json::JsonValue::make_array();
    for (const auto &hl : highlights) {
        result->push(serialize_document_highlight(hl));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_selection_range(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *positions_val = req.params->get("positions");
    if (positions_val == nullptr || !positions_val->is_array()) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::vector<Position> positions;
    for (const auto &pos_val : positions_val->array_items) {
        if (pos_val == nullptr) {
            continue;
        }
        positions.push_back(parse_position(*pos_val));
    }

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    auto ranges = compute_selection_ranges(document->text, positions);

    auto result = json::JsonValue::make_array();
    for (const auto &r : ranges) {
        result->push(serialize_selection_range(r));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_code_lens(const JsonRpcRequest &req) {
    if (!req.params) {
        send_empty_array(transport_, req.id);
        return;
    }

    std::string uri;
    if (!text_document_id(*req.params, uri)) {
        send_empty_array(transport_, req.id);
        return;
    }

    const auto *document = store_.get(uri);
    if (document == nullptr) {
        send_empty_array(transport_, req.id);
        return;
    }

    auto lenses = compute_code_lens(document->text);

    auto result = json::JsonValue::make_array();
    for (const auto &lens : lenses) {
        result->push(serialize_code_lens(lens));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

} // namespace ahfl::lsp
