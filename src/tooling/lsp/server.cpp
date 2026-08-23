#include "tooling/lsp/server.hpp"

#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "compiler/project_discovery/discovery.hpp"
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
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

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

constexpr std::size_t kMaxBoundedIntLiteralPatternCompletions = 64;

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
    case SymbolKind::Function:
        return LspSymbolKind::Function;
    case SymbolKind::Trait:
        // P3 (RFC §3.2.2): a trait maps to the LSP Interface symbol kind
        // (closest match — like a capability, traits describe capability).
        return LspSymbolKind::Interface;
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
    case SymbolKind::Function:
    case SymbolKind::Trait:
        // P3 (RFC §3.2.2): a trait is offered as a generic completion item.
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
    case SymbolKind::Function:
        return "function";
    case SymbolKind::Trait:
        // P3 (RFC §3.2.2): a trait's detail label.
        return "trait";
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

[[nodiscard]] std::optional<Location>
primitive_type_definition_for_kind(const LspAnalysisSnapshot &snapshot, PrimitiveKind kind) {
    const TypeKey type{
        .kind = TypeKey::Kind::Primitive,
        .primitive = kind,
    };
    if (snapshot.workspace_index != nullptr) {
        if (const auto location = snapshot.workspace_index->primitive_home_location_for_type(type);
            location.has_value()) {
            return location;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Location>
primitive_type_definition_for_type_key(const LspAnalysisSnapshot &snapshot, const TypeKey &type) {
    if (snapshot.workspace_index != nullptr) {
        if (const auto location = snapshot.workspace_index->primitive_home_location_for_type(type);
            location.has_value()) {
            return location;
        }
    }
    if (type.kind != TypeKey::Kind::Primitive || !type.primitive.has_value()) {
        return std::nullopt;
    }
    return primitive_type_definition_for_kind(snapshot, *type.primitive);
}

[[nodiscard]] std::optional<TypeKey> primitive_type_key_at(const LspAnalysisSnapshot &snapshot,
                                                           const LspSourceSnapshot &source,
                                                           std::size_t offset);

[[nodiscard]] std::optional<Location> primitive_type_definition_at(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, std::size_t offset) {
    const auto type = primitive_type_key_at(snapshot, source, offset);
    return type.has_value() ? primitive_type_definition_for_type_key(snapshot, *type)
                            : std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> decimal_scale_at(const SourceFile &source,
                                                           std::size_t cursor) {
    auto skip_space = [&]() {
        while (cursor < source.content.size() &&
               std::isspace(static_cast<unsigned char>(source.content[cursor])) != 0) {
            ++cursor;
        }
    };

    skip_space();
    if (cursor >= source.content.size() || source.content[cursor] != '(') {
        return std::nullopt;
    }
    ++cursor;
    skip_space();

    if (cursor >= source.content.size() ||
        std::isdigit(static_cast<unsigned char>(source.content[cursor])) == 0) {
        return std::nullopt;
    }
    std::int64_t scale = 0;
    while (cursor < source.content.size() &&
           std::isdigit(static_cast<unsigned char>(source.content[cursor])) != 0) {
        scale = scale * 10 + static_cast<std::int64_t>(source.content[cursor] - '0');
        ++cursor;
    }

    skip_space();
    if (cursor >= source.content.size() || source.content[cursor] != ')') {
        return std::nullopt;
    }
    return scale;
}

[[nodiscard]] std::optional<TypeKey> primitive_type_key_at(const LspAnalysisSnapshot &snapshot,
                                                           const LspSourceSnapshot &source,
                                                           std::size_t offset) {
    const auto index_iter = snapshot.hover_indices.find(hover_index_key(source));
    if (index_iter == snapshot.hover_indices.end()) {
        return std::nullopt;
    }

    const auto *target = index_iter->second.lookup(offset);
    if (target == nullptr || target->kind != HoverTargetKind::TypeReference ||
        target->role != "builtin type") {
        return std::nullopt;
    }
    const auto primitive = primitive_kind_from_spelling(target->local_name);
    if (!primitive.has_value()) {
        return std::nullopt;
    }

    TypeKey key{
        .kind = TypeKey::Kind::Primitive,
        .primitive = *primitive,
    };
    if (*primitive == PrimitiveKind::Decimal && source.source != nullptr) {
        key.primitive_parameter =
            decimal_scale_at(*source.source, target->token_range.end_offset).value_or(0);
    }
    return key;
}

[[nodiscard]] bool has_extent(SourceRange range) noexcept {
    return range.end_offset > range.begin_offset;
}

[[nodiscard]] std::optional<Location> location_for_source_range(const LspAnalysisSnapshot &snapshot,
                                                                std::optional<SourceId> source_id,
                                                                const LspSourceSnapshot &fallback,
                                                                SourceRange range) {
    const auto *source = &fallback;
    if (source_id.has_value()) {
        if (const auto *resolved = snapshot.source_for_id(*source_id); resolved != nullptr) {
            source = resolved;
        }
    }
    if (source == nullptr || source->source == nullptr || !has_extent(range)) {
        return std::nullopt;
    }
    return Location{
        .uri = source->uri,
        .range = to_lsp_range(*source->source, range),
    };
}

[[nodiscard]] SourceRange impl_location_range(const ImplTypeInfo &impl, bool prefer_trait) {
    if (prefer_trait && has_extent(impl.trait_ref_range)) {
        return impl.trait_ref_range;
    }
    if (has_extent(impl.target_type_range)) {
        return impl.target_type_range;
    }
    return impl.declaration_range;
}

struct OrderedLocation {
    std::size_t order{0};
    Location location;
};

[[nodiscard]] bool same_location(const Location &lhs, const Location &rhs) {
    return lhs.uri == rhs.uri && lhs.range.start.line == rhs.range.start.line &&
           lhs.range.start.character == rhs.range.start.character &&
           lhs.range.end.line == rhs.range.end.line &&
           lhs.range.end.character == rhs.range.end.character;
}

[[nodiscard]] bool same_source_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] bool same_file_uri(std::string_view lhs, std::string_view rhs) {
    const auto lhs_path = AnalysisService::path_from_uri(lhs);
    const auto rhs_path = AnalysisService::path_from_uri(rhs);
    if (!lhs_path.has_value() || !rhs_path.has_value()) {
        return lhs == rhs;
    }
    return AnalysisService::normalized_path_key(*lhs_path) ==
           AnalysisService::normalized_path_key(*rhs_path);
}

void push_unique_location(std::vector<Location> &locations, Location location) {
    const auto duplicate =
        std::find_if(locations.begin(), locations.end(), [&](const Location &existing) {
            return same_location(existing, location);
        });
    if (duplicate == locations.end()) {
        locations.push_back(std::move(location));
    }
}

[[nodiscard]] std::size_t source_range_size(SourceRange range) noexcept {
    return range.end_offset >= range.begin_offset ? range.end_offset - range.begin_offset : 0;
}

[[nodiscard]] bool contains_exclusive(SourceRange range, std::size_t offset) noexcept {
    return range.begin_offset <= offset && offset < range.end_offset;
}

[[nodiscard]] bool contains_range(SourceRange outer, SourceRange inner) noexcept {
    return outer.begin_offset <= inner.begin_offset && inner.end_offset <= outer.end_offset;
}

[[nodiscard]] bool ranges_overlap(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset < rhs.end_offset && rhs.begin_offset < lhs.end_offset;
}

[[nodiscard]] bool same_source_range_vector(const std::vector<SourceRange> &lhs,
                                            const std::vector<SourceRange> &rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!same_source_range(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] SourceRange bounded_source_range(const SourceFile &source,
                                               SourceRange range) noexcept {
    const auto begin = std::min(range.begin_offset, source.content.size());
    const auto end = std::max(begin, std::min(range.end_offset, source.content.size()));
    return SourceRange{.begin_offset = begin, .end_offset = end};
}

[[nodiscard]] std::vector<SourceRange> identifier_ranges_in_source_range(const SourceFile &source,
                                                                         SourceRange raw_range,
                                                                         std::string_view name) {
    std::vector<SourceRange> ranges;
    if (name.empty()) {
        return ranges;
    }

    const auto range = bounded_source_range(source, raw_range);
    auto cursor = range.begin_offset;
    while (cursor < range.end_offset) {
        const auto found = source.content.find(name, cursor);
        if (found == std::string::npos || found + name.size() > range.end_offset) {
            break;
        }

        const auto before_ok = found == 0 || !is_identifier_char(source.content[found - 1]);
        const auto after = found + name.size();
        const auto after_ok =
            after >= source.content.size() || !is_identifier_char(source.content[after]);
        if (before_ok && after_ok) {
            ranges.push_back(SourceRange{.begin_offset = found, .end_offset = after});
        }
        cursor = found + 1;
    }
    return ranges;
}

[[nodiscard]] std::optional<SourceRange>
first_identifier_source_range(const SourceFile &source, SourceRange range, std::string_view name) {
    const auto ranges = identifier_ranges_in_source_range(source, range, name);
    if (ranges.empty()) {
        return std::nullopt;
    }
    return ranges.front();
}

enum class LocalBindingKind {
    Pattern,
    Let,
    ShadowOnly,
};

struct LocalBindingTarget {
    std::size_t id{0};
    LocalBindingKind kind{LocalBindingKind::Pattern};
    std::string name;
    std::optional<SourceId> source_id;
    std::vector<SourceRange> declaration_ranges;
    std::vector<SourceRange> scopes;
};

struct LocalBindingAt {
    LocalBindingTarget target;
    SourceRange token_range;
    bool declaration{false};
};

class LocalBindingCollector {
  public:
    LocalBindingCollector(const LspSourceSnapshot &source, std::vector<LocalBindingTarget> &targets)
        : source_(&source), targets_(&targets) {}

    void collect_program(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            collect_declaration(declaration);
        }
        assign_ids();
    }

  private:
    const LspSourceSnapshot *source_{nullptr};
    std::vector<LocalBindingTarget> *targets_{nullptr};

    void assign_ids() {
        for (std::size_t index = 0; index < targets_->size(); ++index) {
            (*targets_)[index].id = index;
        }
    }

    void add_binding(LocalBindingKind kind,
                     std::string name,
                     SourceRange declaration_range,
                     std::vector<SourceRange> scopes) {
        if (name.empty() || source_range_size(declaration_range) == 0) {
            return;
        }
        scopes.erase(
            std::remove_if(scopes.begin(),
                           scopes.end(),
                           [](SourceRange range) { return source_range_size(range) == 0; }),
            scopes.end());
        if (scopes.empty()) {
            return;
        }
        for (auto &target : *targets_) {
            if (target.kind == kind && target.name == name &&
                target.source_id == source_->source_id &&
                same_source_range_vector(target.scopes, scopes)) {
                target.declaration_ranges.push_back(declaration_range);
                return;
            }
        }
        targets_->push_back(LocalBindingTarget{
            .kind = kind,
            .name = std::move(name),
            .source_id = source_->source_id,
            .declaration_ranges = {declaration_range},
            .scopes = std::move(scopes),
        });
    }

    void add_param_shadows(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                           const ast::BlockSyntax *body) {
        if (body == nullptr) {
            return;
        }
        for (const auto &param : params) {
            if (param) {
                add_binding(LocalBindingKind::ShadowOnly, param->name, param->range, {body->range});
            }
        }
    }

    void add_pattern_bindings(const ast::PatternSyntax *pattern,
                              const std::vector<SourceRange> &scopes) {
        if (pattern == nullptr) {
            return;
        }

        std::visit(Overloaded{
                       [&](const ast::LiteralPattern &) {},
                       [&](const ast::IntRangePattern &) {},
                       [&](const ast::WildcardPattern &) {},
                       [&](const ast::BindingPattern &binding) {
                           add_binding(
                               LocalBindingKind::Pattern, binding.name, pattern->range, scopes);
                           add_pattern_bindings(binding.nested.get(), scopes);
                       },
                       [&](const ast::TuplePattern &tuple) {
                           for (const auto &element : tuple.elements) {
                               add_pattern_bindings(element.get(), scopes);
                           }
                       },
                       [&](const ast::OrPattern &or_pattern) {
                           for (const auto &branch : or_pattern.branches) {
                               add_pattern_bindings(branch.get(), scopes);
                           }
                       },
                       [&](const ast::VariantPattern &variant) {
                           for (const auto &subpattern : variant.subpatterns) {
                               add_pattern_bindings(subpattern.get(), scopes);
                           }
                           for (const auto &field : variant.fields) {
                               if (field && !field->is_rest) {
                                   add_pattern_bindings(field->pattern.get(), scopes);
                               }
                           }
                       },
                   },
                   pattern->node);
    }

    void collect_declaration(const ast::Decl &declaration) {
        switch (ast::decl_kind(declaration)) {
        case ast::NodeKind::ConstDecl: {
            const auto &decl = std::get<ast::ConstDecl>(declaration);
            collect_expr(decl.value.get());
            return;
        }
        case ast::NodeKind::WorkflowDecl: {
            const auto &decl = std::get<ast::WorkflowDecl>(declaration);
            collect_expr(decl.return_value.get());
            for (const auto &safety : decl.safety) {
                collect_temporal_expr(safety.get());
            }
            for (const auto &liveness : decl.liveness) {
                collect_temporal_expr(liveness.get());
            }
            return;
        }
        case ast::NodeKind::ContractDecl: {
            const auto &decl = std::get<ast::ContractDecl>(declaration);
            for (const auto &clause : decl.clauses) {
                if (clause) {
                    collect_expr(clause->expr.get());
                    collect_temporal_expr(clause->temporal_expr.get());
                    if (clause->decreases) {
                        for (const auto &term : clause->decreases->decreases_exprs) {
                            collect_expr(term.get());
                        }
                    }
                }
            }
            return;
        }
        case ast::NodeKind::FlowDecl: {
            const auto &decl = std::get<ast::FlowDecl>(declaration);
            for (const auto &handler : decl.state_handlers) {
                if (handler) {
                    collect_block(handler->body.get());
                }
            }
            return;
        }
        case ast::NodeKind::FnDecl: {
            const auto &decl = std::get<ast::FnDecl>(declaration);
            add_param_shadows(decl.params, decl.body.get());
            collect_block(decl.body.get());
            if (decl.effect_clause) {
                collect_expr(decl.effect_clause->decreases_expr.get());
            }
            return;
        }
        case ast::NodeKind::ImplDecl: {
            const auto &decl = std::get<ast::ImplDecl>(declaration);
            for (const auto &method : decl.methods) {
                if (method) {
                    add_param_shadows(method->params, method->body.get());
                }
                collect_block(method ? method->body.get() : nullptr);
                if (method && method->effect_clause) {
                    collect_expr(method->effect_clause->decreases_expr.get());
                }
            }
            for (const auto &constant : decl.const_items) {
                if (constant) {
                    collect_expr(constant->value.get());
                }
            }
            return;
        }
        case ast::NodeKind::Program:
        case ast::NodeKind::ModuleDecl:
        case ast::NodeKind::ImportDecl:
        case ast::NodeKind::UseDecl:
        case ast::NodeKind::TypeAliasDecl:
        case ast::NodeKind::StructDecl:
        case ast::NodeKind::EnumDecl:
        case ast::NodeKind::CapabilityDecl:
        case ast::NodeKind::PredicateDecl:
        case ast::NodeKind::AgentDecl:
        case ast::NodeKind::TraitDecl:
            return;
        }
    }

    void collect_block(const ast::BlockSyntax *block) {
        if (block == nullptr) {
            return;
        }
        for (const auto &statement : block->statements) {
            if (!statement) {
                continue;
            }
            collect_statement(*statement, *block);
        }
    }

    void collect_statement(const ast::StatementSyntax &statement, const ast::BlockSyntax &block) {
        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
            if (statement.let_stmt) {
                collect_expr(statement.let_stmt->initializer.get());
                add_binding(LocalBindingKind::Let,
                            statement.let_stmt->name,
                            statement.let_stmt->range,
                            {SourceRange{.begin_offset = statement.range.end_offset,
                                         .end_offset = block.range.end_offset}});
            }
            return;
        case ast::StatementSyntaxKind::Assign:
            if (statement.assign_stmt) {
                collect_expr(statement.assign_stmt->value.get());
            }
            return;
        case ast::StatementSyntaxKind::If:
            if (statement.if_stmt) {
                collect_expr(statement.if_stmt->condition.get());
                collect_block(statement.if_stmt->then_block.get());
                collect_block(statement.if_stmt->else_block.get());
            }
            return;
        case ast::StatementSyntaxKind::IfLet:
            if (statement.if_let_stmt) {
                collect_expr(statement.if_let_stmt->scrutinee.get());
                if (statement.if_let_stmt->then_block) {
                    add_pattern_bindings(statement.if_let_stmt->pattern.get(),
                                         {statement.if_let_stmt->then_block->range});
                }
                collect_block(statement.if_let_stmt->then_block.get());
                collect_block(statement.if_let_stmt->else_block.get());
            }
            return;
        case ast::StatementSyntaxKind::Return:
            if (statement.return_stmt) {
                collect_expr(statement.return_stmt->value.get());
            }
            return;
        case ast::StatementSyntaxKind::Assert:
            if (statement.assert_stmt) {
                collect_expr(statement.assert_stmt->condition.get());
                collect_expr(statement.assert_stmt->message.get());
            }
            return;
        case ast::StatementSyntaxKind::Unwrap:
            if (statement.unwrap_stmt) {
                collect_expr(statement.unwrap_stmt->operand.get());
            }
            return;
        case ast::StatementSyntaxKind::Requires:
            if (statement.requires_stmt) {
                collect_expr(statement.requires_stmt->condition.get());
                collect_expr(statement.requires_stmt->message.get());
            }
            return;
        case ast::StatementSyntaxKind::Unreachable:
            if (statement.unreachable_stmt) {
                collect_expr(statement.unreachable_stmt->message.get());
            }
            return;
        case ast::StatementSyntaxKind::Expr:
            if (statement.expr_stmt) {
                collect_expr(statement.expr_stmt->expr.get());
            }
            return;
        case ast::StatementSyntaxKind::Goto:
            return;
        }
    }

    void collect_expr(const ast::ExprSyntax *expr) {
        if (expr == nullptr) {
            return;
        }
        std::visit(
            Overloaded{
                [](const ast::BoolLiteralExpr &) {},
                [](const ast::IntegerLiteralExpr &) {},
                [](const ast::FloatLiteralExpr &) {},
                [](const ast::DecimalLiteralExpr &) {},
                [](const ast::StringLiteralExpr &) {},
                [](const ast::DurationLiteralExpr &) {},
                [](const ast::PathExpr &) {},
                [](const ast::QualifiedValueExpr &) {},
                [&](const ast::CallExpr &call) {
                    for (const auto &arg : call.arguments) {
                        collect_expr(arg.get());
                    }
                },
                [&](const ast::MethodCallExpr &call) {
                    collect_expr(call.receiver.get());
                    for (const auto &arg : call.arguments) {
                        collect_expr(arg.get());
                    }
                },
                [&](const ast::StructLiteralExpr &literal) {
                    for (const auto &field : literal.fields) {
                        if (field) {
                            collect_expr(field->value.get());
                        }
                    }
                },
                [&](const ast::UnaryExpr &unary) { collect_expr(unary.operand.get()); },
                [&](const ast::BinaryExpr &binary) {
                    collect_expr(binary.lhs.get());
                    collect_expr(binary.rhs.get());
                },
                [&](const ast::MemberAccessExpr &member) { collect_expr(member.base.get()); },
                [&](const ast::IndexAccessExpr &index) {
                    collect_expr(index.base.get());
                    collect_expr(index.index.get());
                },
                [&](const ast::GroupExpr &group) { collect_expr(group.inner.get()); },
                [&](const ast::MatchExpr &match) {
                    collect_expr(match.scrutinee.get());
                    for (const auto &arm : match.arms) {
                        if (!arm) {
                            continue;
                        }
                        std::vector<SourceRange> scopes;
                        if (arm->guard) {
                            scopes.push_back(arm->guard->range);
                        }
                        if (arm->body) {
                            scopes.push_back(arm->body->range);
                        }
                        add_pattern_bindings(arm->pattern.get(), scopes);
                        collect_expr(arm->guard.get());
                        collect_expr(arm->body.get());
                    }
                },
                [&](const ast::LambdaExpr &lambda) {
                    if (lambda.body) {
                        for (const auto &param : lambda.params) {
                            if (param) {
                                add_binding(LocalBindingKind::ShadowOnly,
                                            param->name,
                                            param->range,
                                            {lambda.body->range});
                            }
                        }
                    }
                    collect_expr(lambda.body.get());
                },
                [&](const ast::UnwrapExprSyntax &unwrap) { collect_expr(unwrap.operand.get()); },
                // RFC 0014: operand? — recurse into the operand.
                [&](const ast::TryExpr &try_expr) { collect_expr(try_expr.operand.get()); },
                // RFC 0013 P3-gaps-B: `{}` — leaf, no sub-expressions to collect.
                [](const ast::UnitLiteralExpr &) {},
            },
            expr->node);
    }

    void collect_temporal_expr(const ast::TemporalExprSyntax *expr) {
        if (expr == nullptr) {
            return;
        }
        std::visit(Overloaded{
                       [&](const ast::EmbeddedTemporalExpr &embedded) {
                           collect_expr(embedded.expr.get());
                       },
                       [](const ast::CalledTemporalExpr &) {},
                       [](const ast::InStateTemporalExpr &) {},
                       [](const ast::RunningTemporalExpr &) {},
                       [](const ast::CompletedTemporalExpr &) {},
                       [&](const ast::UnaryTemporalExpr &unary) {
                           collect_temporal_expr(unary.operand.get());
                       },
                       [&](const ast::BinaryTemporalExpr &binary) {
                           collect_temporal_expr(binary.lhs.get());
                           collect_temporal_expr(binary.rhs.get());
                       },
                   },
                   expr->node);
    }
};

[[nodiscard]] std::vector<LocalBindingTarget>
collect_local_binding_targets(const LspSourceSnapshot &source) {
    std::vector<LocalBindingTarget> targets;
    if (source.program == nullptr) {
        return targets;
    }
    LocalBindingCollector collector{source, targets};
    collector.collect_program(*source.program);
    return targets;
}

[[nodiscard]] bool local_binding_kind_is_renameable(LocalBindingKind kind) noexcept {
    return kind == LocalBindingKind::Pattern || kind == LocalBindingKind::Let;
}

[[nodiscard]] bool local_binding_declaration_contains(const LocalBindingTarget &target,
                                                      SourceRange range,
                                                      std::size_t offset) noexcept {
    return contains_exclusive(range, offset) &&
           (target.kind == LocalBindingKind::Pattern || target.kind == LocalBindingKind::Let ||
            target.kind == LocalBindingKind::ShadowOnly);
}

[[nodiscard]] std::optional<SourceRange> local_binding_declaration_range_at(
    const SourceFile &source, const LocalBindingTarget &target, std::size_t offset) {
    for (const auto declaration : target.declaration_ranges) {
        const auto token =
            first_identifier_source_range(source, declaration, target.name).value_or(declaration);
        if (local_binding_declaration_contains(target, token, offset)) {
            return token;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool local_binding_scope_contains(const LocalBindingTarget &target,
                                                SourceRange range) noexcept {
    return std::any_of(target.scopes.begin(), target.scopes.end(), [&](SourceRange scope) {
        return contains_range(scope, range);
    });
}

[[nodiscard]] std::size_t local_binding_scope_rank(const LocalBindingTarget &target,
                                                   SourceRange range) noexcept {
    std::size_t rank = std::numeric_limits<std::size_t>::max();
    for (const auto scope : target.scopes) {
        if (contains_range(scope, range)) {
            rank = std::min(rank, source_range_size(scope));
        }
    }
    return rank;
}

[[nodiscard]] std::optional<SourceRange> typed_expr_root_range(const SourceFile &source,
                                                               const TypedExpr &expr) {
    if (expr.path_root.empty() || source_range_size(expr.range) == 0) {
        return std::nullopt;
    }
    return first_identifier_source_range(source, expr.range, expr.path_root);
}

[[nodiscard]] const LocalBindingTarget *
innermost_local_binding_for_reference(const std::vector<LocalBindingTarget> &targets,
                                      std::string_view name,
                                      SourceRange reference_range) {
    const LocalBindingTarget *best = nullptr;
    auto best_rank = std::numeric_limits<std::size_t>::max();
    for (const auto &target : targets) {
        if (target.name != name || !local_binding_scope_contains(target, reference_range)) {
            continue;
        }
        const auto rank = local_binding_scope_rank(target, reference_range);
        if (rank < best_rank || (rank == best_rank && best != nullptr && target.id > best->id)) {
            best = &target;
            best_rank = rank;
        }
    }
    return best;
}

[[nodiscard]] std::optional<LocalBindingAt> local_binding_at(const LspAnalysisSnapshot &snapshot,
                                                             const LspSourceSnapshot &source,
                                                             std::size_t offset) {
    if (source.source == nullptr) {
        return std::nullopt;
    }
    const auto targets = collect_local_binding_targets(source);
    for (const auto &target : targets) {
        if (!local_binding_kind_is_renameable(target.kind)) {
            continue;
        }
        if (const auto range = local_binding_declaration_range_at(*source.source, target, offset);
            range.has_value()) {
            return LocalBindingAt{.target = target, .token_range = *range, .declaration = true};
        }
    }

    const auto *typed = snapshot.typed_program();
    if (typed == nullptr) {
        return std::nullopt;
    }
    for (const auto &expr : typed->expressions) {
        if (!same_source(expr.source_id, source.source_id) || expr.resolved_symbol.has_value() ||
            expr.path_root.empty() ||
            (expr.path_root_kind != AssignTargetRootKind::Local &&
             expr.path_root_kind != AssignTargetRootKind::Identifier)) {
            continue;
        }
        const auto root_range = typed_expr_root_range(*source.source, expr);
        if (!root_range.has_value() || !contains_exclusive(*root_range, offset)) {
            continue;
        }
        const auto *target =
            innermost_local_binding_for_reference(targets, expr.path_root, *root_range);
        if (target != nullptr && local_binding_kind_is_renameable(target->kind)) {
            return LocalBindingAt{.target = *target, .token_range = *root_range};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Location> local_binding_location(const LspAnalysisSnapshot &snapshot,
                                                             const LspSourceSnapshot &fallback,
                                                             const LocalBindingTarget &target,
                                                             SourceRange range) {
    return location_for_source_range(snapshot, target.source_id, fallback, range);
}

[[nodiscard]] std::vector<Location>
local_binding_definition_locations(const LspAnalysisSnapshot &snapshot,
                                   const LspSourceSnapshot &source,
                                   const LocalBindingTarget &target) {
    std::vector<Location> locations;
    for (const auto declaration : target.declaration_ranges) {
        auto token = declaration;
        if (source.source != nullptr) {
            token = first_identifier_source_range(*source.source, declaration, target.name)
                        .value_or(declaration);
        }
        if (const auto location = local_binding_location(snapshot, source, target, token);
            location.has_value()) {
            push_unique_location(locations, *location);
        }
    }
    return locations;
}

[[nodiscard]] std::vector<Location>
local_binding_reference_locations(const LspAnalysisSnapshot &snapshot,
                                  const LspSourceSnapshot &source,
                                  const LocalBindingTarget &target,
                                  bool include_declarations) {
    std::vector<Location> locations;
    if (include_declarations) {
        for (auto location : local_binding_definition_locations(snapshot, source, target)) {
            push_unique_location(locations, std::move(location));
        }
    }

    const auto *typed = snapshot.typed_program();
    if (typed == nullptr || source.source == nullptr) {
        return locations;
    }
    const auto targets = collect_local_binding_targets(source);
    for (const auto &expr : typed->expressions) {
        if (!same_source(expr.source_id, source.source_id) || expr.resolved_symbol.has_value() ||
            expr.path_root != target.name ||
            (expr.path_root_kind != AssignTargetRootKind::Local &&
             expr.path_root_kind != AssignTargetRootKind::Identifier)) {
            continue;
        }
        const auto root_range = typed_expr_root_range(*source.source, expr);
        if (!root_range.has_value()) {
            continue;
        }
        const auto *bound =
            innermost_local_binding_for_reference(targets, expr.path_root, *root_range);
        if (bound == nullptr || bound->id != target.id) {
            continue;
        }
        if (const auto location =
                location_for_source_range(snapshot, expr.source_id, source, *root_range);
            location.has_value()) {
            push_unique_location(locations, *location);
        }
    }
    return locations;
}

[[nodiscard]] std::vector<DocumentHighlight>
local_binding_document_highlights(const LspAnalysisSnapshot &snapshot,
                                  const LspSourceSnapshot &source,
                                  const LocalBindingTarget &target) {
    std::vector<DocumentHighlight> highlights;
    for (const auto &location : local_binding_reference_locations(
             snapshot, source, target, /*include_declarations=*/true)) {
        if (location.uri != source.uri) {
            continue;
        }
        highlights.push_back(
            DocumentHighlight{.range = location.range, .kind = DocumentHighlightKind::Text});
    }
    return highlights;
}

[[nodiscard]] bool local_binding_rename_conflicts(const std::vector<LocalBindingTarget> &targets,
                                                  const LocalBindingTarget &target,
                                                  std::string_view new_name) {
    for (const auto &other : targets) {
        if (other.id == target.id || other.name != new_name) {
            continue;
        }
        for (const auto target_scope : target.scopes) {
            for (const auto other_scope : other.scopes) {
                if (ranges_overlap(target_scope, other_scope)) {
                    return true;
                }
            }
        }
        for (const auto declaration : other.declaration_ranges) {
            if (local_binding_scope_contains(target, declaration)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::optional<SymbolId> nominal_symbol_for_type(const Type &type) {
    return type.visit(types::Overloads{
        [](const types::StructT &structure) { return structure.symbol; },
        [](const types::EnumT &enumeration) { return enumeration.symbol; },
        [](const types::EnumVariantT &variant) { return variant.symbol; },
        [](const auto &) { return std::optional<SymbolId>{}; },
    });
}

[[nodiscard]] std::optional<Location> indexed_symbol_location(const LspAnalysisSnapshot &snapshot,
                                                              const Symbol &symbol) {
    if (snapshot.workspace_index == nullptr) {
        return std::nullopt;
    }
    const auto def = snapshot.workspace_def_for_symbol(symbol.id);
    if (!def.has_value()) {
        return std::nullopt;
    }
    const auto *fact = snapshot.workspace_index->symbol_for_def(*def);
    if (fact == nullptr || fact->completeness == FactCompleteness::Invalid) {
        return std::nullopt;
    }
    return fact->location;
}

[[nodiscard]] std::optional<DefId> index_def_for_symbol(const LspWorkspaceIndex &index,
                                                        const LspAnalysisSnapshot &snapshot,
                                                        const Symbol &symbol) {
    if (!symbol.source_id.has_value()) {
        return std::nullopt;
    }
    const auto *source = snapshot.source_for_id(*symbol.source_id);
    if (source == nullptr) {
        return std::nullopt;
    }
    const auto selection_range = symbol_navigation_source_range(symbol, *source);

    for (const auto &fact : index.symbols()) {
        if (fact.completeness == FactCompleteness::Invalid || fact.kind != symbol.kind) {
            continue;
        }
        if (!same_file_uri(fact.location.uri, source->uri)) {
            continue;
        }
        if (same_source_range(fact.declaration_range, symbol.declaration_range) ||
            same_source_range(fact.selection_range, selection_range)) {
            return fact.def_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<Location> type_definition_locations_for_type(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, const Type &type) {
    std::vector<Location> locations;
    if (const auto primitive = primitive_kind_for_type(type); primitive.has_value()) {
        if (const auto location = primitive_type_definition_for_kind(snapshot, *primitive);
            location.has_value()) {
            locations.push_back(*location);
        }
        return locations;
    }

    const auto symbol_id = nominal_symbol_for_type(type);
    if (!symbol_id.has_value()) {
        return locations;
    }

    const auto symbol = snapshot.resolve_result.symbol_table.get(*symbol_id);
    if (!symbol.has_value()) {
        return locations;
    }
    if (const auto location = indexed_symbol_location(snapshot, symbol->get());
        location.has_value()) {
        locations.push_back(*location);
        return locations;
    }
    if (const auto location = symbol_location(snapshot, symbol->get(), source);
        location.has_value()) {
        locations.push_back(*location);
    }
    return locations;
}

[[nodiscard]] std::vector<Location> type_definition_locations_for_expression_at(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, std::size_t offset) {
    if (snapshot.type_check_result == nullptr) {
        return {};
    }

    const auto *expr =
        snapshot.type_check_result->typed_program.find_expr_containing(offset, source.source_id);
    if (expr == nullptr || expr->type == nullptr) {
        return {};
    }
    return type_definition_locations_for_type(snapshot, source, *expr->type);
}

void push_impl_location(std::vector<OrderedLocation> &locations,
                        const LspAnalysisSnapshot &snapshot,
                        const LspSourceSnapshot &fallback,
                        const ImplTypeInfo &impl,
                        bool prefer_trait) {
    const auto range = impl_location_range(impl, prefer_trait);
    const auto location = location_for_source_range(snapshot, impl.source_id, fallback, range);
    if (!location.has_value()) {
        return;
    }
    locations.push_back(OrderedLocation{
        .order = impl.index,
        .location = *location,
    });
}

[[nodiscard]] std::vector<Location> finalize_impl_locations(std::vector<OrderedLocation> ordered) {
    std::sort(ordered.begin(), ordered.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.order != rhs.order) {
            return lhs.order < rhs.order;
        }
        if (lhs.location.uri != rhs.location.uri) {
            return lhs.location.uri < rhs.location.uri;
        }
        if (lhs.location.range.start.line != rhs.location.range.start.line) {
            return lhs.location.range.start.line < rhs.location.range.start.line;
        }
        return lhs.location.range.start.character < rhs.location.range.start.character;
    });

    std::vector<Location> result;
    result.reserve(ordered.size());
    for (const auto &entry : ordered) {
        if (!result.empty() && same_location(result.back(), entry.location)) {
            continue;
        }
        result.push_back(entry.location);
    }
    return result;
}

[[nodiscard]] std::vector<Location> implementation_locations_for_symbol(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, const Symbol &symbol) {
    if (snapshot.workspace_index != nullptr) {
        if (symbol.kind == SymbolKind::Struct || symbol.kind == SymbolKind::Enum) {
            const auto def = snapshot.workspace_def_for_symbol(symbol.id);
            if (def.has_value()) {
                return snapshot.workspace_index->implementation_locations_for_nominal_def(*def);
            }
            return {};
        }

        if (symbol.kind == SymbolKind::Trait) {
            const auto def = snapshot.workspace_def_for_symbol(symbol.id);
            if (def.has_value()) {
                return snapshot.workspace_index->implementation_locations_for_trait(*def);
            }
            return {};
        }
    }

    if (snapshot.type_check_result == nullptr) {
        return {};
    }

    std::vector<OrderedLocation> ordered;
    for (const auto &[index, impl] : snapshot.type_check_result->environment.impls()) {
        (void)index;
        if (symbol.kind == SymbolKind::Trait) {
            if (impl.trait_symbol.has_value() && *impl.trait_symbol == symbol.id) {
                push_impl_location(ordered, snapshot, source, impl, true);
            }
            continue;
        }

        if ((symbol.kind == SymbolKind::Struct || symbol.kind == SymbolKind::Enum) &&
            impl.target_symbol.has_value() && *impl.target_symbol == symbol.id) {
            push_impl_location(ordered, snapshot, source, impl, false);
        }
    }

    return finalize_impl_locations(std::move(ordered));
}

[[nodiscard]] std::vector<Location>
primitive_implementation_locations(const LspAnalysisSnapshot &snapshot,
                                   const LspSourceSnapshot &source,
                                   const TypeKey &primitive_type) {
    if (snapshot.analysis_mode == LspAnalysisMode::DetachedSourceUnit) {
        return {};
    }

    if (snapshot.workspace_index != nullptr) {
        return snapshot.workspace_index->implementation_locations_for_type(primitive_type);
    }

    if (snapshot.type_check_result == nullptr) {
        return {};
    }

    std::vector<OrderedLocation> ordered;
    for (const auto &[index, impl] : snapshot.type_check_result->environment.impls()) {
        (void)index;
        if (impl.target_type == nullptr) {
            continue;
        }
        if (type_key_for_type(*impl.target_type) == primitive_type) {
            push_impl_location(ordered, snapshot, source, impl, false);
        }
    }

    return finalize_impl_locations(std::move(ordered));
}

[[nodiscard]] std::vector<Location> primitive_type_definition_locations_at(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, std::size_t offset) {
    std::vector<Location> locations;
    if (const auto home = primitive_type_definition_at(snapshot, source, offset);
        home.has_value()) {
        locations.push_back(*home);
    }

    return locations;
}

void append_primitive_type_definition_locations_from_index(std::vector<Location> &locations,
                                                           const SysrootPrimitiveIndex *index,
                                                           const TypeKey &type) {
    if (index == nullptr) {
        return;
    }
    if (const auto home = index->home_location_for_type(type); home.has_value()) {
        push_unique_location(locations, *home);
    }
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_location_array(const std::vector<Location> &locations) {
    auto result = json::JsonValue::make_array();
    for (const auto &location : locations) {
        result->push(serialize_location(location));
    }
    return result;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_location_or_array(const std::vector<Location> &locations) {
    if (locations.size() == 1) {
        return serialize_location(locations.front());
    }
    return serialize_location_array(locations);
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

[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_workspace_diagnostic_item(std::string_view uri,
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
    const std::uint64_t content_hash = source != nullptr && source->source != nullptr
                                           ? std::hash<std::string>{}(source->source->content)
                                           : snapshot->content_hash;
    return std::string(uri) + "#v" + std::to_string(snapshot->document_version) + "-" +
           std::to_string(snapshot->document_revision) + "-" +
           std::to_string(snapshot->workspace_revision) + "-" + std::to_string(content_hash);
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

[[nodiscard]] bool completion_snippet_support_from_initialize(const json::JsonValue *params) {
    if (params == nullptr) {
        return false;
    }
    const auto *capabilities = params->get("capabilities");
    const auto *text_document =
        capabilities != nullptr ? capabilities->get("textDocument") : nullptr;
    const auto *completion = text_document != nullptr ? text_document->get("completion") : nullptr;
    const auto *completion_item =
        completion != nullptr ? completion->get("completionItem") : nullptr;
    const auto *snippet_support =
        completion_item != nullptr ? completion_item->get("snippetSupport") : nullptr;
    if (snippet_support == nullptr) {
        return false;
    }
    const auto value = snippet_support->as_bool();
    return value.has_value() && *value;
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
workspace_folders_from_initialize(const json::JsonValue *params) {
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

void append_toolchain_diagnostics(project_discovery::ToolchainProfileSet &profiles,
                                  std::vector<package_graph::Diagnostic> diagnostics) {
    profiles.diagnostics.reserve(profiles.diagnostics.size() + diagnostics.size());
    for (auto &diagnostic : diagnostics) {
        profiles.diagnostics.push_back(std::move(diagnostic));
    }
}

void add_toolchain_profile_ambiguity(project_discovery::ToolchainProfileSet &profiles,
                                     const std::filesystem::path &workspace_root,
                                     const std::filesystem::path &existing_std_manifest,
                                     const std::filesystem::path &new_std_manifest) {
    profiles.diagnostics.push_back(package_graph::Diagnostic{
        .code = "E::toolchain_profile_ambiguous",
        .message = "workspace folder '" + workspace_root.generic_string() +
                   "' has multiple AHFL sysroots: '" + existing_std_manifest.generic_string() +
                   "' and '" + new_std_manifest.generic_string() + "'",
        .range = {},
        .related =
            {
                package_graph::Diagnostic::Related{
                    .path = existing_std_manifest,
                    .message = "existing sysroot profile for this workspace folder",
                },
                package_graph::Diagnostic::Related{
                    .path = new_std_manifest,
                    .message = "conflicting sysroot profile for this workspace folder",
                },
            },
    });
}

void append_toolchain_profile(project_discovery::ToolchainProfileSet &profiles,
                              std::filesystem::path sysroot,
                              project_discovery::ToolchainProfileOrigin origin,
                              std::optional<std::filesystem::path> workspace_root) {
    auto result =
        project_discovery::toolchain_profile_from_sysroot_input(std::move(sysroot), origin);
    append_toolchain_diagnostics(profiles, std::move(result.diagnostics));
    if (!result.profile.has_value()) {
        return;
    }

    if (workspace_root.has_value()) {
        const auto normalized_root = project_discovery::normalize_project_path(*workspace_root);
        result.profile->scope = project_discovery::ToolchainProfileScope::WorkspaceFolder;
        for (const auto &existing : profiles.workspace_profiles) {
            if (project_discovery::normalize_project_path(existing.workspace_root) !=
                normalized_root) {
                continue;
            }
            if (project_discovery::normalize_project_path(existing.profile.std_manifest) !=
                project_discovery::normalize_project_path(result.profile->std_manifest)) {
                add_toolchain_profile_ambiguity(profiles,
                                                normalized_root,
                                                existing.profile.std_manifest,
                                                result.profile->std_manifest);
            }
            return;
        }
        profiles.workspace_profiles.push_back(project_discovery::WorkspaceToolchainProfile{
            .workspace_root = normalized_root,
            .profile = std::move(*result.profile),
        });
        return;
    }

    result.profile->scope = project_discovery::ToolchainProfileScope::GlobalDefault;
    profiles.default_profile = std::move(result.profile);
}

void parse_toolchain_profiles_array(project_discovery::ToolchainProfileSet &profiles,
                                    const json::JsonValue &items,
                                    project_discovery::ToolchainProfileOrigin origin) {
    if (!items.is_array()) {
        return;
    }
    for (const auto &item : items.array_items) {
        if (item == nullptr || !item->is_object()) {
            continue;
        }
        const auto *workspace_value = item->get("workspaceFolder");
        const auto *sysroot_value = item->get("sysroot");
        if (workspace_value == nullptr || sysroot_value == nullptr) {
            continue;
        }
        const auto workspace_uri = workspace_value->as_string();
        const auto sysroot = sysroot_value->as_string();
        if (!workspace_uri.has_value() || !sysroot.has_value() || sysroot->empty()) {
            continue;
        }
        auto workspace_root = AnalysisService::path_from_uri(*workspace_uri);
        if (!workspace_root.has_value()) {
            profiles.diagnostics.push_back(package_graph::Diagnostic{
                .code = "E::toolchain_profile_ambiguous",
                .message = "toolchain profile workspaceFolder must be a file URI: '" +
                           std::string(*workspace_uri) + "'",
                .range = {},
            });
            continue;
        }
        append_toolchain_profile(profiles,
                                 std::filesystem::path(std::string(*sysroot)),
                                 origin,
                                 std::move(workspace_root));
    }
}

[[nodiscard]] project_discovery::ToolchainProfileSet
toolchain_profiles_from_initialization_json(const json::JsonValue &toolchain) {
    project_discovery::ToolchainProfileSet profiles;
    if (const auto *sysroot = toolchain.get("defaultSysroot"); sysroot != nullptr) {
        if (const auto value = sysroot->as_string(); value.has_value() && !value->empty()) {
            append_toolchain_profile(profiles,
                                     std::filesystem::path(std::string(*value)),
                                     project_discovery::ToolchainProfileOrigin::LspInitialization,
                                     std::nullopt);
        }
    }
    if (!profiles.default_profile.has_value()) {
        if (const auto *sysroot = toolchain.get("bundledSysroot"); sysroot != nullptr) {
            if (const auto value = sysroot->as_string(); value.has_value() && !value->empty()) {
                append_toolchain_profile(
                    profiles,
                    std::filesystem::path(std::string(*value)),
                    project_discovery::ToolchainProfileOrigin::BundledExtension,
                    std::nullopt);
            }
        }
    }
    if (const auto *items = toolchain.get("profiles"); items != nullptr) {
        parse_toolchain_profiles_array(
            profiles, *items, project_discovery::ToolchainProfileOrigin::LspInitialization);
    }
    return profiles;
}

[[nodiscard]] project_discovery::ToolchainProfileSet
toolchain_profiles_from_configuration_json(const json::JsonValue &toolchain) {
    project_discovery::ToolchainProfileSet profiles;
    if (const auto *sysroot = toolchain.get("sysroot"); sysroot != nullptr) {
        if (const auto value = sysroot->as_string(); value.has_value() && !value->empty()) {
            append_toolchain_profile(profiles,
                                     std::filesystem::path(std::string(*value)),
                                     project_discovery::ToolchainProfileOrigin::LspConfiguration,
                                     std::nullopt);
        }
    }
    return profiles;
}

void upsert_workspace_toolchain_profile(project_discovery::ToolchainProfileSet &profiles,
                                        project_discovery::WorkspaceToolchainProfile profile) {
    const auto normalized_root = project_discovery::normalize_project_path(profile.workspace_root);
    profile.workspace_root = normalized_root;
    profile.profile.scope = project_discovery::ToolchainProfileScope::WorkspaceFolder;
    for (auto &existing : profiles.workspace_profiles) {
        if (project_discovery::normalize_project_path(existing.workspace_root) == normalized_root) {
            existing = std::move(profile);
            return;
        }
    }
    profiles.workspace_profiles.push_back(std::move(profile));
}

[[nodiscard]] project_discovery::ToolchainProfileSet merge_toolchain_profiles(
    const project_discovery::ToolchainProfileSet &initialization,
    const std::optional<project_discovery::ToolchainProfileSet> &configuration) {
    project_discovery::ToolchainProfileSet merged = initialization;
    if (!configuration.has_value()) {
        return merged;
    }

    if (configuration->default_profile.has_value()) {
        merged.default_profile = configuration->default_profile;
        merged.default_profile->scope = project_discovery::ToolchainProfileScope::GlobalDefault;
    }
    for (const auto &workspace_profile : configuration->workspace_profiles) {
        upsert_workspace_toolchain_profile(merged, workspace_profile);
    }
    merged.diagnostics.reserve(merged.diagnostics.size() + configuration->diagnostics.size());
    for (const auto &diagnostic : configuration->diagnostics) {
        merged.diagnostics.push_back(diagnostic);
    }
    return merged;
}

[[nodiscard]] project_discovery::ToolchainProfileSet
toolchain_profiles_from_workspace_configuration_result(
    const json::JsonValue &result, const std::vector<std::filesystem::path> &workspace_folders) {
    project_discovery::ToolchainProfileSet profiles;
    if (!result.is_array()) {
        return profiles;
    }

    for (std::size_t index = 0; index < result.array_items.size(); ++index) {
        const auto &item = *result.array_items[index];
        std::optional<std::string_view> sysroot;
        if (item.is_object()) {
            if (const auto *value = item.get("sysroot"); value != nullptr) {
                sysroot = value->as_string();
            }
        } else {
            sysroot = item.as_string();
        }
        if (!sysroot.has_value() || sysroot->empty()) {
            continue;
        }

        std::optional<std::filesystem::path> workspace_root;
        if (index < workspace_folders.size()) {
            workspace_root = workspace_folders[index];
        }
        auto sysroot_path = std::filesystem::path(std::string(*sysroot));
        if (workspace_root.has_value() && !sysroot_path.is_absolute()) {
            sysroot_path =
                project_discovery::normalize_project_path(*workspace_root / sysroot_path);
        }
        append_toolchain_profile(profiles,
                                 std::move(sysroot_path),
                                 project_discovery::ToolchainProfileOrigin::LspConfiguration,
                                 std::move(workspace_root));
    }

    return profiles;
}

[[nodiscard]] project_discovery::ToolchainProfileSet
toolchain_profiles_from_initialize(const json::JsonValue *params) {
    project_discovery::ToolchainProfileSet profiles;
    if (params == nullptr) {
        return profiles;
    }
    const auto *init_options = params->get("initializationOptions");
    if (init_options == nullptr || !init_options->is_object()) {
        return profiles;
    }
    const auto *ahfl = init_options->get("ahfl");
    if (ahfl == nullptr || !ahfl->is_object()) {
        return profiles;
    }
    const auto *toolchain = ahfl->get("toolchain");
    if (toolchain == nullptr || !toolchain->is_object()) {
        return profiles;
    }
    return toolchain_profiles_from_initialization_json(*toolchain);
}

[[nodiscard]] std::optional<project_discovery::ToolchainProfileSet>
toolchain_profiles_from_configuration(const json::JsonValue &params) {
    const auto *settings = params.get("settings");
    if (settings == nullptr || !settings->is_object()) {
        return std::nullopt;
    }
    const auto *ahfl = settings->get("ahfl");
    if (ahfl == nullptr || !ahfl->is_object()) {
        return std::nullopt;
    }
    const auto *toolchain = ahfl->get("toolchain");
    if (toolchain == nullptr || !toolchain->is_object()) {
        return std::nullopt;
    }
    return toolchain_profiles_from_configuration_json(*toolchain);
}

void apply_hover_options_from_configuration(HoverRenderOptions &options,
                                            const json::JsonValue &params) {
    const auto *settings = params.get("settings");
    const auto *ahfl =
        settings != nullptr && settings->is_object() ? settings->get("ahfl") : nullptr;
    const auto *hover = ahfl != nullptr && ahfl->is_object() ? ahfl->get("hover") : nullptr;
    if (hover == nullptr || !hover->is_object()) {
        return;
    }
    apply_hover_detail_level(options, *hover);
    apply_hover_markup_kind(options, *hover);
    apply_hover_scalar_options(options, *hover);
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

[[nodiscard]] bool symbol_visible_for_completion(const LspAnalysisSnapshot &snapshot,
                                                 const LspSourceSnapshot &source,
                                                 const Symbol &symbol) {
    if (!symbol.source_id.has_value() || same_source(symbol.source_id, source.source_id)) {
        return true;
    }
    if (snapshot.analysis_mode == LspAnalysisMode::DetachedSourceUnit) {
        return true;
    }
    return symbol.visibility == ast::Visibility::Public &&
           snapshot.resolve_result.is_api_reachable(symbol.id);
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

[[nodiscard]] std::string snippet_placeholder(std::size_t tabstop, std::string_view fallback) {
    return "${" + std::to_string(tabstop) + ":" + std::string(fallback) + "}";
}

[[nodiscard]] std::string snippet_choice_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == ',' || ch == '|') {
            escaped += '\\';
        }
        escaped += ch;
    }
    return escaped;
}

[[nodiscard]] std::string snippet_choice_placeholder(std::size_t tabstop,
                                                     const std::vector<std::string> &choices) {
    std::string snippet = "${" + std::to_string(tabstop) + "|";
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (index > 0) {
            snippet += ",";
        }
        snippet += snippet_choice_escape(choices[index]);
    }
    snippet += "|}";
    return snippet;
}

[[nodiscard]] std::optional<std::size_t>
bounded_int_literal_completion_count(const types::BoundedIntT &bounds) {
    if (bounds.maximum < bounds.minimum) {
        return std::nullopt;
    }

    std::size_t count = 1;
    std::int64_t value = bounds.minimum;
    while (value != bounds.maximum) {
        if (count >= kMaxBoundedIntLiteralPatternCompletions ||
            value == std::numeric_limits<std::int64_t>::max()) {
            return std::nullopt;
        }
        ++value;
        ++count;
    }
    return count;
}

[[nodiscard]] std::string bounded_int_range_pattern_label(const types::BoundedIntT &bounds) {
    return std::to_string(bounds.minimum) + ".." + std::to_string(bounds.maximum);
}

[[nodiscard]] std::optional<std::string>
nested_enum_variant_pattern_choice(const EnumVariantInfo &variant) {
    if (!is_identifier(variant.name)) {
        return std::nullopt;
    }
    if (variant.payload_kind == EnumVariantPayloadKind::Unit) {
        return variant.name;
    }
    if (variant.payload_kind == EnumVariantPayloadKind::Tuple) {
        std::string choice = variant.name + "(";
        for (std::size_t index = 0; index < variant.payload.size(); ++index) {
            if (index > 0) {
                choice += ", ";
            }
            choice += "_";
        }
        choice += ")";
        return choice;
    }
    if (variant.payload_kind == EnumVariantPayloadKind::Struct) {
        std::string choice = variant.name + " { ";
        for (std::size_t index = 0; index < variant.fields.size(); ++index) {
            if (!is_identifier(variant.fields[index].name)) {
                return std::nullopt;
            }
            if (index > 0) {
                choice += ", ";
            }
            choice += variant.fields[index].name + ": _";
        }
        choice += " }";
        return choice;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
primitive_pattern_snippet_choices(TypePtr payload_type) {
    if (payload_type == nullptr) {
        return std::nullopt;
    }

    if (const auto *bounded_int = payload_type->get_if<types::BoundedIntT>();
        bounded_int != nullptr) {
        if (bounded_int->maximum < bounded_int->minimum) {
            return std::nullopt;
        }
        std::vector<std::string> choices;
        if (bounded_int_literal_completion_count(*bounded_int).has_value()) {
            std::int64_t value = bounded_int->minimum;
            while (true) {
                choices.push_back(std::to_string(value));
                if (value == bounded_int->maximum) {
                    break;
                }
                ++value;
            }
        }
        if (bounded_int->minimum != bounded_int->maximum) {
            choices.push_back(bounded_int_range_pattern_label(*bounded_int));
        }
        choices.push_back("_");
        return choices;
    }

    if (const auto *bounded_string = payload_type->get_if<types::BoundedStringT>();
        bounded_string != nullptr) {
        if (bounded_string->minimum == 0 && bounded_string->maximum == 0) {
            return std::vector<std::string>{"\"\"", "_"};
        }
        return std::nullopt;
    }

    const auto primitive = primitive_kind_for_type(*payload_type);
    if (!primitive.has_value()) {
        return std::nullopt;
    }
    switch (*primitive) {
    case PrimitiveKind::Bool:
        return std::vector<std::string>{"true", "false", "_"};
    case PrimitiveKind::Int:
        return std::vector<std::string>{"0", "0..0", "_"};
    case PrimitiveKind::Float:
        return std::vector<std::string>{"0.0", "_"};
    case PrimitiveKind::String:
        return std::vector<std::string>{"\"\"", "_"};
    case PrimitiveKind::Unit:
    case PrimitiveKind::UUID:
    case PrimitiveKind::Timestamp:
    case PrimitiveKind::Duration:
    case PrimitiveKind::Decimal:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string pattern_payload_snippet_placeholder(const TypeEnvironment &environment,
                                                              TypePtr payload_type,
                                                              std::size_t tabstop) {
    if (payload_type == nullptr) {
        return snippet_placeholder(tabstop, "_");
    }

    if (const auto choices = primitive_pattern_snippet_choices(payload_type); choices.has_value()) {
        return snippet_choice_placeholder(tabstop, *choices);
    }

    const auto enum_info = environment.get_enum(*payload_type);
    if (!enum_info.has_value()) {
        return snippet_placeholder(tabstop, "_");
    }

    std::vector<std::string> choices;
    choices.reserve(enum_info->get().variants.size() + 1);
    for (const auto &variant : enum_info->get().variants) {
        if (const auto choice = nested_enum_variant_pattern_choice(variant); choice.has_value()) {
            choices.push_back(*choice);
        }
    }
    choices.push_back("_");
    return snippet_choice_placeholder(tabstop, choices);
}

[[nodiscard]] std::string tuple_variant_pattern_snippet(const TypeEnvironment &environment,
                                                        const EnumVariantInfo &variant) {
    std::string snippet = variant.name + "(";
    for (std::size_t index = 0; index < variant.payload.size(); ++index) {
        if (index > 0) {
            snippet += ", ";
        }
        snippet +=
            pattern_payload_snippet_placeholder(environment, variant.payload[index], index + 1);
    }
    snippet += ")";
    return snippet;
}

[[nodiscard]] std::string struct_variant_pattern_snippet(const TypeEnvironment &environment,
                                                         const EnumVariantInfo &variant) {
    std::string snippet = variant.name + " { ";
    for (std::size_t index = 0; index < variant.fields.size(); ++index) {
        if (index > 0) {
            snippet += ", ";
        }
        snippet +=
            variant.fields[index].name + ": " +
            pattern_payload_snippet_placeholder(environment, variant.fields[index].type, index + 1);
    }
    snippet += " }";
    return snippet;
}

void push_pattern_enum_variant_completions(std::vector<CompletionItem> &items,
                                           const TypeEnvironment &environment,
                                           const EnumTypeInfo &enum_info,
                                           bool snippet_support) {
    for (const auto &variant : enum_info.variants) {
        CompletionItem item;
        item.label = variant.name;
        item.kind = CompletionItemKind::Enum;
        item.detail = "enum pattern " + enum_info.canonical_name;
        if (snippet_support && variant.payload_kind == EnumVariantPayloadKind::Tuple &&
            !variant.payload.empty()) {
            item.insert_text = tuple_variant_pattern_snippet(environment, variant);
            item.insert_text_format = InsertTextFormat::Snippet;
        } else if (snippet_support && variant.payload_kind == EnumVariantPayloadKind::Struct &&
                   !variant.fields.empty()) {
            item.insert_text = struct_variant_pattern_snippet(environment, variant);
            item.insert_text_format = InsertTextFormat::Snippet;
        }
        items.push_back(std::move(item));
    }
}

[[nodiscard]] std::string_view pattern_source_text(const SourceFile &source,
                                                   const TypedPattern &pattern) {
    if (pattern.range.begin_offset >= pattern.range.end_offset ||
        pattern.range.end_offset > source.content.size()) {
        return {};
    }
    return std::string_view{source.content}.substr(
        pattern.range.begin_offset, pattern.range.end_offset - pattern.range.begin_offset);
}

[[nodiscard]] bool is_single_line_source_fragment(std::string_view text) {
    return !text.empty() && text.find('\n') == std::string_view::npos &&
           text.find('\r') == std::string_view::npos;
}

[[nodiscard]] std::optional<SourceRange> pattern_completion_replacement_range_at(
    const SourceFile &source, const TypedPattern &pattern, std::size_t offset) {
    if (pattern.range.begin_offset >= pattern.range.end_offset ||
        pattern.range.end_offset > source.content.size() || !contains(pattern.range, offset)) {
        return std::nullopt;
    }
    const auto text = pattern_source_text(source, pattern);
    if (!is_single_line_source_fragment(text)) {
        return std::nullopt;
    }
    switch (pattern.kind) {
    case TypedPatternKind::Wildcard:
        if (text == "_") {
            return pattern.range;
        }
        break;
    case TypedPatternKind::Literal:
        if (pattern.children.empty()) {
            return pattern.range;
        }
        break;
    case TypedPatternKind::IntRange:
        if (pattern.children.empty() && text.find("..") != std::string_view::npos) {
            return pattern.range;
        }
        break;
    case TypedPatternKind::Variant:
        if (pattern.children.empty() &&
            pattern.variant_payload_kind == EnumVariantPayloadKind::Unit &&
            text == pattern.variant_name && is_identifier(text)) {
            return pattern.range;
        }
        break;
    case TypedPatternKind::Binding:
        if (pattern.children.empty() && is_identifier(text)) {
            return pattern.range;
        }
        break;
    case TypedPatternKind::Tuple:
    case TypedPatternKind::Or:
        break;
    }
    return std::nullopt;
}

void apply_completion_text_edit(std::vector<CompletionItem> &items,
                                const SourceFile &source,
                                std::optional<SourceRange> replacement_range) {
    if (!replacement_range.has_value()) {
        return;
    }
    const auto range = to_lsp_range(source, *replacement_range);
    for (auto &item : items) {
        std::string replacement =
            item.insert_text.empty() ? item.label : std::move(item.insert_text);
        item.insert_text.clear();
        item.text_edit = TextEdit{
            .range = range,
            .new_text = std::move(replacement),
        };
    }
}

void push_bool_pattern_completions(std::vector<CompletionItem> &items) {
    for (const std::string_view literal : {"true", "false"}) {
        CompletionItem item;
        item.label = std::string{literal};
        item.kind = CompletionItemKind::Constant;
        item.detail = "Bool pattern";
        items.push_back(std::move(item));
    }
}

void push_wildcard_pattern_completion(std::vector<CompletionItem> &items, std::string_view detail) {
    CompletionItem item;
    item.label = "_";
    item.kind = CompletionItemKind::Constant;
    item.detail = std::string{detail};
    items.push_back(std::move(item));
}

void push_literal_pattern_completion(
    std::vector<CompletionItem> &items,
    std::string label,
    std::string detail,
    std::string insert_text = {},
    std::optional<InsertTextFormat> insert_text_format = std::nullopt) {
    CompletionItem item;
    item.label = std::move(label);
    item.kind = CompletionItemKind::Constant;
    item.detail = std::move(detail);
    item.insert_text = std::move(insert_text);
    item.insert_text_format = insert_text_format;
    items.push_back(std::move(item));
}

void push_open_primitive_pattern_completions(std::vector<CompletionItem> &items,
                                             PrimitiveKind primitive,
                                             bool snippet_support) {
    push_wildcard_pattern_completion(items, "wildcard pattern");

    switch (primitive) {
    case PrimitiveKind::Int:
        push_literal_pattern_completion(items, "0", "Int literal pattern");
        if (snippet_support) {
            push_literal_pattern_completion(
                items, "0..0", "Int range pattern", "${1:0}..${2:0}", InsertTextFormat::Snippet);
        } else {
            push_literal_pattern_completion(items, "0..0", "Int range pattern");
        }
        return;
    case PrimitiveKind::Float:
        push_literal_pattern_completion(items, "0.0", "Float literal pattern");
        return;
    case PrimitiveKind::String:
        push_literal_pattern_completion(items, "\"\"", "String literal pattern");
        return;
    case PrimitiveKind::Unit:
    case PrimitiveKind::Bool:
    case PrimitiveKind::UUID:
    case PrimitiveKind::Timestamp:
    case PrimitiveKind::Duration:
    case PrimitiveKind::Decimal:
        return;
    }
}

void push_bounded_string_pattern_completions(std::vector<CompletionItem> &items,
                                             const types::BoundedStringT &bounds) {
    push_wildcard_pattern_completion(items, "wildcard pattern");
    if (bounds.minimum == 0 && bounds.maximum == 0) {
        push_literal_pattern_completion(items, "\"\"", "String literal pattern");
    }
}

void push_bounded_int_pattern_completions(std::vector<CompletionItem> &items,
                                          const types::BoundedIntT &bounds) {
    if (bounds.maximum < bounds.minimum) {
        return;
    }

    if (bounded_int_literal_completion_count(bounds).has_value()) {
        std::int64_t value = bounds.minimum;
        while (true) {
            CompletionItem item;
            item.label = std::to_string(value);
            item.kind = CompletionItemKind::Constant;
            item.detail = "bounded Int pattern";
            items.push_back(std::move(item));
            if (value == bounds.maximum) {
                break;
            }
            ++value;
        }
    }

    if (bounds.minimum != bounds.maximum) {
        CompletionItem item;
        item.label = bounded_int_range_pattern_label(bounds);
        item.kind = CompletionItemKind::Constant;
        item.detail = "bounded Int range pattern";
        items.push_back(std::move(item));
    }
}

[[nodiscard]] bool is_rest_pattern_boundary(char ch) noexcept {
    return ch == '\0' || std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == '{' ||
           ch == '}';
}

[[nodiscard]] std::optional<SourceRange> struct_variant_rest_pattern_range_at(
    const SourceFile &source, const TypedPattern &pattern, std::size_t offset) {
    if (pattern.variant_payload_kind != EnumVariantPayloadKind::Struct ||
        pattern.range.begin_offset >= pattern.range.end_offset ||
        pattern.range.end_offset > source.content.size()) {
        return std::nullopt;
    }

    const auto text = std::string_view{source.content}.substr(
        pattern.range.begin_offset, pattern.range.end_offset - pattern.range.begin_offset);
    const auto open = text.find('{');
    const auto close = text.rfind('}');
    if (open == std::string_view::npos || close == std::string_view::npos || open >= close) {
        return std::nullopt;
    }

    const auto open_offset = pattern.range.begin_offset + open;
    const auto close_offset = pattern.range.begin_offset + close;
    if (offset < open_offset || offset > close_offset) {
        return std::nullopt;
    }

    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaping = false;
    for (std::size_t index = open_offset + 1; index + 1 < close_offset; ++index) {
        const char ch = source.content[index];
        if (in_string) {
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '(') {
            ++paren_depth;
            continue;
        }
        if (ch == ')' && paren_depth > 0) {
            --paren_depth;
            continue;
        }
        if (ch == '{') {
            ++brace_depth;
            continue;
        }
        if (ch == '}' && brace_depth > 0) {
            --brace_depth;
            continue;
        }
        if (ch == '[') {
            ++bracket_depth;
            continue;
        }
        if (ch == ']' && bracket_depth > 0) {
            --bracket_depth;
            continue;
        }
        if (paren_depth != 0 || brace_depth != 0 || bracket_depth != 0 || ch != '.' ||
            source.content[index + 1] != '.') {
            continue;
        }

        const char before = index == open_offset + 1 ? '\0' : source.content[index - 1];
        const char after = index + 2 >= close_offset ? '\0' : source.content[index + 2];
        if (!is_rest_pattern_boundary(before) || !is_rest_pattern_boundary(after)) {
            continue;
        }

        const auto rest_end = index + 2;
        if (index <= offset && offset <= rest_end) {
            return SourceRange{.begin_offset = index, .end_offset = rest_end};
        }
    }
    return std::nullopt;
}

void push_struct_variant_field_completions(std::vector<CompletionItem> &items,
                                           const SourceFile &source,
                                           const TypeEnvironment &environment,
                                           const EnumTypeInfo &enum_info,
                                           const EnumVariantInfo &variant_info,
                                           const TypedPattern &pattern,
                                           std::size_t offset,
                                           bool snippet_support) {
    const auto rest_range = struct_variant_rest_pattern_range_at(source, pattern, offset);
    std::unordered_set<std::string> used_fields;
    used_fields.reserve(pattern.children.size());
    for (const auto &child : pattern.children) {
        if (!child.name.empty()) {
            used_fields.insert(child.name);
        }
    }

    for (const auto &field : variant_info.fields) {
        if (used_fields.contains(field.name)) {
            continue;
        }
        CompletionItem item;
        item.label = field.name;
        item.kind = CompletionItemKind::Variable;
        item.detail = "field " + enum_info.canonical_name + "::" + variant_info.name;
        std::string replacement = field.name;
        if (snippet_support) {
            replacement =
                field.name + ": " + pattern_payload_snippet_placeholder(environment, field.type, 1);
            item.insert_text_format = InsertTextFormat::Snippet;
        }
        if (rest_range.has_value()) {
            item.text_edit = TextEdit{
                .range = to_lsp_range(source, *rest_range),
                .new_text = std::move(replacement),
            };
        } else {
            item.insert_text = std::move(replacement);
        }
        items.push_back(std::move(item));
    }
}

struct PatternPayloadCursor {
    const TypedPattern *pattern{nullptr};
    std::size_t open_offset{0};
    std::size_t close_offset{0};
};

[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
payload_delimiter_bounds(const SourceFile &source, const TypedPattern &pattern) {
    if (pattern.range.begin_offset >= pattern.range.end_offset ||
        pattern.range.end_offset > source.content.size()) {
        return std::nullopt;
    }

    const auto text = std::string_view{source.content}.substr(
        pattern.range.begin_offset, pattern.range.end_offset - pattern.range.begin_offset);
    char open_char = '(';
    char close_char = ')';
    if (pattern.variant_payload_kind == EnumVariantPayloadKind::Struct) {
        open_char = '{';
        close_char = '}';
    } else if (pattern.variant_payload_kind != EnumVariantPayloadKind::Tuple) {
        return std::nullopt;
    }

    const auto open = text.find(open_char);
    const auto close = text.rfind(close_char);
    if (open == std::string_view::npos || close == std::string_view::npos || open >= close) {
        return std::nullopt;
    }
    return std::pair{pattern.range.begin_offset + open, pattern.range.begin_offset + close};
}

[[nodiscard]] int active_payload_parameter(const SourceFile &source,
                                           std::size_t open_offset,
                                           std::size_t close_offset,
                                           std::size_t cursor_offset,
                                           std::size_t parameter_count) {
    if (parameter_count == 0) {
        return 0;
    }

    int active = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaping = false;
    const auto end = std::min(cursor_offset, close_offset);
    for (std::size_t index = open_offset + 1; index < end && index < source.content.size();
         ++index) {
        const char ch = source.content[index];
        if (in_string) {
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '(') {
            ++paren_depth;
        } else if (ch == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
            --brace_depth;
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']' && bracket_depth > 0) {
            --bracket_depth;
        } else if (ch == ',' && paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            ++active;
        }
    }
    return std::min(active, static_cast<int>(parameter_count - 1));
}

[[nodiscard]] const TypedPattern *find_typed_pattern_at(const TypedProgram &program,
                                                        std::optional<SourceId> source_id,
                                                        std::size_t offset) {
    const TypedPattern *best = nullptr;
    std::size_t best_width = std::numeric_limits<std::size_t>::max();
    for (const auto &pattern : program.patterns) {
        if (!same_source(pattern.source_id, source_id) || !contains(pattern.range, offset)) {
            continue;
        }
        const auto width = pattern.range.end_offset - pattern.range.begin_offset;
        if (best == nullptr || width < best_width) {
            best = &pattern;
            best_width = width;
        }
    }
    return best;
}

[[nodiscard]] std::vector<Range> typed_pattern_selection_ranges(const TypedProgram &program,
                                                                const SourceFile &source,
                                                                std::optional<SourceId> source_id,
                                                                std::size_t offset) {
    std::vector<Range> ranges;
    for (const auto &pattern : program.patterns) {
        if (!same_source(pattern.source_id, source_id) || !contains(pattern.range, offset) ||
            pattern.range.begin_offset == pattern.range.end_offset) {
            continue;
        }
        ranges.push_back(to_lsp_range(source, pattern.range));
    }
    return ranges;
}

[[nodiscard]] std::optional<PatternPayloadCursor>
find_variant_payload_pattern_at(const TypedProgram &program,
                                const SourceFile &source,
                                std::optional<SourceId> source_id,
                                std::size_t offset) {
    const TypedPattern *best = nullptr;
    std::size_t best_open = 0;
    std::size_t best_close = 0;
    std::size_t best_width = std::numeric_limits<std::size_t>::max();
    for (const auto &pattern : program.patterns) {
        if (!same_source(pattern.source_id, source_id) || !contains(pattern.range, offset) ||
            pattern.kind != TypedPatternKind::Variant ||
            (pattern.variant_payload_kind != EnumVariantPayloadKind::Struct &&
             pattern.variant_payload_kind != EnumVariantPayloadKind::Tuple)) {
            continue;
        }
        const auto bounds = payload_delimiter_bounds(source, pattern);
        if (!bounds.has_value() || !(bounds->first < offset && offset <= bounds->second)) {
            continue;
        }
        const auto width = pattern.range.end_offset - pattern.range.begin_offset;
        if (best == nullptr || width < best_width) {
            best = &pattern;
            best_open = bounds->first;
            best_close = bounds->second;
            best_width = width;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return PatternPayloadCursor{
        .pattern = best,
        .open_offset = best_open,
        .close_offset = best_close,
    };
}

[[nodiscard]] const TypedPattern *find_struct_variant_pattern_at(const TypedProgram &program,
                                                                 const SourceFile &source,
                                                                 std::optional<SourceId> source_id,
                                                                 std::size_t offset) {
    const auto cursor = find_variant_payload_pattern_at(program, source, source_id, offset);
    if (!cursor.has_value() ||
        cursor->pattern->variant_payload_kind != EnumVariantPayloadKind::Struct) {
        return nullptr;
    }
    return cursor->pattern;
}

[[nodiscard]] bool push_typed_pattern_domain_completions(std::vector<CompletionItem> &items,
                                                         const SourceFile &source,
                                                         const TypeEnvironment &environment,
                                                         const TypedPattern &pattern,
                                                         std::size_t offset,
                                                         bool snippet_support) {
    if (pattern.matched_type == nullptr) {
        return false;
    }
    const auto replacement_range = pattern_completion_replacement_range_at(source, pattern, offset);

    if (const auto primitive = primitive_kind_for_type(*pattern.matched_type);
        primitive.has_value() && *primitive == PrimitiveKind::Bool) {
        push_bool_pattern_completions(items);
        apply_completion_text_edit(items, source, replacement_range);
        return true;
    }
    if (const auto *bounded_int = pattern.matched_type->get_if<types::BoundedIntT>();
        bounded_int != nullptr) {
        push_bounded_int_pattern_completions(items, *bounded_int);
        apply_completion_text_edit(items, source, replacement_range);
        return true;
    }
    if (pattern.matched_type->holds<types::BoundedStringT>()) {
        push_bounded_string_pattern_completions(
            items, *pattern.matched_type->get_if<types::BoundedStringT>());
        apply_completion_text_edit(items, source, replacement_range);
        return true;
    }
    if (const auto primitive = primitive_kind_for_type(*pattern.matched_type);
        primitive.has_value()) {
        push_open_primitive_pattern_completions(items, *primitive, snippet_support);
        apply_completion_text_edit(items, source, replacement_range);
        return true;
    }

    const auto enum_info = environment.get_enum(*pattern.matched_type);
    if (!enum_info.has_value()) {
        return false;
    }

    push_pattern_enum_variant_completions(items, environment, enum_info->get(), snippet_support);
    apply_completion_text_edit(items, source, replacement_range);
    return true;
}

[[nodiscard]] bool push_pattern_context_completions(std::vector<CompletionItem> &items,
                                                    const LspAnalysisSnapshot &snapshot,
                                                    const LspSourceSnapshot &source,
                                                    std::size_t offset,
                                                    bool snippet_support) {
    if (!snapshot.type_check_result || source.source == nullptr) {
        return false;
    }
    const auto *program = snapshot.typed_program();
    if (program == nullptr) {
        return false;
    }
    const auto *pattern = find_typed_pattern_at(*program, source.source_id, offset);
    const auto *field_pattern =
        find_struct_variant_pattern_at(*program, *source.source, source.source_id, offset);
    if (pattern != nullptr && field_pattern != nullptr && pattern != field_pattern &&
        push_typed_pattern_domain_completions(items,
                                              *source.source,
                                              snapshot.type_check_result->environment,
                                              *pattern,
                                              offset,
                                              snippet_support)) {
        return true;
    }
    if (field_pattern != nullptr && field_pattern->enum_symbol.has_value()) {
        const auto enum_info =
            snapshot.type_check_result->environment.get_enum(*field_pattern->enum_symbol);
        if (enum_info.has_value()) {
            const auto variant_info = enum_info->get().find_variant(field_pattern->variant_name);
            if (variant_info.has_value() &&
                variant_info->get().payload_kind == EnumVariantPayloadKind::Struct) {
                push_struct_variant_field_completions(items,
                                                      *source.source,
                                                      snapshot.type_check_result->environment,
                                                      enum_info->get(),
                                                      variant_info->get(),
                                                      *field_pattern,
                                                      offset,
                                                      snippet_support);
                return true;
            }
        }
    }

    if (pattern == nullptr) {
        return false;
    }
    return push_typed_pattern_domain_completions(items,
                                                 *source.source,
                                                 snapshot.type_check_result->environment,
                                                 *pattern,
                                                 offset,
                                                 snippet_support);
}

[[nodiscard]] std::optional<SignatureHelp> pattern_signature_help(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, std::size_t offset) {
    if (!snapshot.type_check_result || source.source == nullptr) {
        return std::nullopt;
    }
    const auto *program = snapshot.typed_program();
    if (program == nullptr) {
        return std::nullopt;
    }
    auto cursor =
        find_variant_payload_pattern_at(*program, *source.source, source.source_id, offset);
    if (!cursor.has_value() || cursor->pattern == nullptr ||
        !cursor->pattern->enum_symbol.has_value()) {
        return std::nullopt;
    }
    const auto enum_info =
        snapshot.type_check_result->environment.get_enum(*cursor->pattern->enum_symbol);
    if (!enum_info.has_value()) {
        return std::nullopt;
    }
    const auto variant_info = enum_info->get().find_variant(cursor->pattern->variant_name);
    if (!variant_info.has_value()) {
        return std::nullopt;
    }

    SignatureInformation info;
    info.documentation =
        "enum pattern " + enum_info->get().canonical_name + "::" + variant_info->get().name;
    if (variant_info->get().payload_kind == EnumVariantPayloadKind::Tuple) {
        info.label = enum_info->get().canonical_name + "::" + variant_info->get().name + "(";
        for (std::size_t index = 0; index < variant_info->get().payload.size(); ++index) {
            if (index > 0) {
                info.label += ", ";
            }
            const auto type = variant_info->get().payload[index];
            info.label += type ? type->describe() : "?";

            ParameterInformation parameter;
            parameter.label = std::to_string(index) + ": " + (type ? type->describe() : "?");
            parameter.documentation = "tuple payload " + std::to_string(index);
            info.parameters.push_back(std::move(parameter));
        }
        info.label += ")";
    } else if (variant_info->get().payload_kind == EnumVariantPayloadKind::Struct) {
        info.label = enum_info->get().canonical_name + "::" + variant_info->get().name + " { ";
        for (std::size_t index = 0; index < variant_info->get().fields.size(); ++index) {
            if (index > 0) {
                info.label += ", ";
            }
            const auto &field = variant_info->get().fields[index];
            info.label += field.name + ": " + (field.type ? field.type->describe() : "?");

            ParameterInformation parameter;
            parameter.label = field.name + ": " + (field.type ? field.type->describe() : "?");
            parameter.documentation = field.type ? field.type->describe() : "?";
            info.parameters.push_back(std::move(parameter));
        }
        info.label += " }";
    } else {
        return std::nullopt;
    }

    if (info.parameters.empty()) {
        return std::nullopt;
    }

    SignatureHelp help;
    help.signatures.push_back(std::move(info));
    help.active_signature = 0;
    help.active_parameter = active_payload_parameter(*source.source,
                                                     cursor->open_offset,
                                                     cursor->close_offset,
                                                     offset,
                                                     help.signatures.front().parameters.size());
    return help;
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
                (line.size() > kw.size() && line.substr(0, kw.size()) == kw &&
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
                } else if constexpr (std::is_same_v<T, JsonRpcResponse>) {
                    handle_response(m);
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
    } else if (req.method == "textDocument/typeDefinition") {
        handle_type_definition(req);
    } else if (req.method == "textDocument/implementation") {
        handle_implementation(req);
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
    } else if (notif.method == "workspace/didChangeConfiguration") {
        if (notif.params) {
            handle_did_change_configuration(*notif.params);
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

void LspServer::handle_response(const JsonRpcResponse &resp) {
    if (!pending_configuration_request_id_.has_value() ||
        resp.id != *pending_configuration_request_id_) {
        return;
    }
    pending_configuration_request_id_.reset();
    if (resp.error.has_value() || !resp.result) {
        return;
    }

    configuration_toolchain_profiles_ =
        toolchain_profiles_from_workspace_configuration_result(*resp.result, workspace_folders_);
    apply_active_toolchain_profiles();
    send_diagnostic_refresh();
}

void LspServer::handle_initialize(const JsonRpcRequest &req) {
    initialized_ = true;
    workspace_folders_ = workspace_folders_from_initialize(req.params.get());
    analysis_.set_workspace_folders(workspace_folders_);
    initialization_toolchain_profiles_ = toolchain_profiles_from_initialize(req.params.get());
    apply_active_toolchain_profiles();
    hover_options_ = hover_render_options_from_initialize(req.params.get());
    completion_snippet_support_ = completion_snippet_support_from_initialize(req.params.get());

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
    if (const auto path = AnalysisService::path_from_uri(versioned.uri); path.has_value()) {
        analysis_.invalidate_paths({*path});
    } else {
        analysis_.invalidate_all();
    }
    send_diagnostic_refresh();
}

void LspServer::handle_did_close(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    const auto id = parse_text_document_identifier(*td);
    const auto path = AnalysisService::path_from_uri(id.uri);
    store_.close(id.uri);
    if (path.has_value()) {
        analysis_.invalidate_paths({*path});
    } else {
        analysis_.invalidate_all();
    }
    send_diagnostic_refresh();
}

void LspServer::handle_did_change_configuration(const json::JsonValue &params) {
    apply_hover_options_from_configuration(hover_options_, params);
    const auto profiles = toolchain_profiles_from_configuration(params);
    if (profiles.has_value()) {
        configuration_toolchain_profiles_ = std::move(*profiles);
        apply_active_toolchain_profiles();
        send_diagnostic_refresh();
    }
    request_workspace_configuration();
}

void LspServer::handle_workspace_folders_changed(const json::JsonValue &params) {
    const auto *event = params.get("event");
    if (event == nullptr) {
        return;
    }

    remove_workspace_folder_roots(workspace_folders_, event->get("removed"));
    append_workspace_folder_roots(workspace_folders_, event->get("added"));
    analysis_.set_workspace_folders(workspace_folders_);
    request_workspace_configuration();
    send_diagnostic_refresh();
}

void LspServer::handle_watched_files_changed(const json::JsonValue &params) {
    std::vector<std::filesystem::path> changed_paths;
    if (const auto *changes = params.get("changes"); changes != nullptr && changes->is_array()) {
        changed_paths.reserve(changes->array_items.size());
        for (const auto &change : changes->array_items) {
            if (change == nullptr) {
                continue;
            }
            const auto *uri_value = change->get("uri");
            if (uri_value == nullptr) {
                continue;
            }
            const auto uri = uri_value->as_string();
            if (!uri.has_value()) {
                continue;
            }
            if (const auto path = AnalysisService::path_from_uri(*uri); path.has_value()) {
                changed_paths.push_back(*path);
            }
        }
    }
    if (changed_paths.empty()) {
        analysis_.invalidate_all();
    } else {
        analysis_.invalidate_paths(changed_paths);
    }
    send_diagnostic_refresh();
}

void LspServer::handle_exit() {
    shutdown_requested_ = true;
}

void LspServer::apply_active_toolchain_profiles() {
    analysis_.set_toolchain_profiles(merge_toolchain_profiles(initialization_toolchain_profiles_,
                                                              configuration_toolchain_profiles_));
}

void LspServer::request_workspace_configuration() {
    auto params = json::JsonValue::make_object();
    auto items = json::JsonValue::make_array();
    for (const auto &folder : workspace_folders_) {
        auto item = json::JsonValue::make_object();
        item->set("scopeUri", json::JsonValue::make_string(AnalysisService::uri_from_path(folder)));
        item->set("section", json::JsonValue::make_string("ahfl.toolchain"));
        items->push(std::move(item));
    }
    if (items->array_items.empty()) {
        auto item = json::JsonValue::make_object();
        item->set("section", json::JsonValue::make_string("ahfl.toolchain"));
        items->push(std::move(item));
    }
    params->set("items", std::move(items));

    pending_configuration_request_id_ =
        "ahfl-workspace-configuration-" + std::to_string(next_server_request_id_++);
    transport_.send_request(
        *pending_configuration_request_id_, "workspace/configuration", std::move(params));
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
    } else if (push_pattern_context_completions(
                   items, *snapshot, *source, offset, completion_snippet_support_)) {
        // Pattern-aware completion is type-directed. Once the cursor is inside
        // a typed pattern, generic expression symbols would be noisy and can
        // offer symbols that are not valid constructors for this scrutinee.
    } else if (looks_like_type_position(*source->source, offset)) {
        for (const auto &symbol : snapshot->resolve_result.symbol_table.symbols()) {
            if (!symbol_visible_for_completion(*snapshot, *source, symbol)) {
                continue;
            }
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
            if (!symbol_visible_for_completion(*snapshot, *source, symbol)) {
                continue;
            }
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

    const auto offset = offset_at(*source->source, position);
    const auto target = symbol_at(*snapshot, *source, offset);
    if (!target.has_value()) {
        if (const auto local = local_binding_at(*snapshot, *source, offset); local.has_value()) {
            const auto locations =
                local_binding_definition_locations(*snapshot, *source, local->target);
            if (!locations.empty()) {
                JsonRpcResponse resp;
                resp.id = req.id;
                resp.result = serialize_location_or_array(locations);
                transport_.send_response(resp);
                return;
            }
        }
        auto locations = primitive_type_definition_locations_at(*snapshot, *source, offset);
        if (locations.empty()) {
            if (const auto type = primitive_type_key_at(*snapshot, *source, offset);
                type.has_value()) {
                append_primitive_type_definition_locations_from_index(
                    locations, analysis_.sysroot_primitive_index_for_uri(uri), *type);
            }
        }
        if (!locations.empty()) {
            JsonRpcResponse resp;
            resp.id = req.id;
            resp.result = serialize_location_or_array(locations);
            transport_.send_response(resp);
            return;
        }
        send_null(transport_, req.id);
        return;
    }

    const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
    if (!symbol.has_value()) {
        send_null(transport_, req.id);
        return;
    }
    auto location = indexed_symbol_location(*snapshot, symbol->get());
    if (!location.has_value()) {
        location = symbol_location(*snapshot, symbol->get(), *source);
    }
    if (!location.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_location(*location);
    transport_.send_response(resp);
}

void LspServer::handle_type_definition(const JsonRpcRequest &req) {
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

    const auto offset = offset_at(*source->source, position);
    auto primitive_locations = primitive_type_definition_locations_at(*snapshot, *source, offset);
    if (primitive_locations.empty()) {
        if (const auto type = primitive_type_key_at(*snapshot, *source, offset); type.has_value()) {
            append_primitive_type_definition_locations_from_index(
                primitive_locations, analysis_.sysroot_primitive_index_for_uri(uri), *type);
        }
    }
    if (!primitive_locations.empty()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_location_or_array(primitive_locations);
        transport_.send_response(resp);
        return;
    }

    if (auto locations = type_definition_locations_for_expression_at(*snapshot, *source, offset);
        !locations.empty()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_location_or_array(locations);
        transport_.send_response(resp);
        return;
    }

    const auto target = symbol_at(*snapshot, *source, offset);
    if (!target.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
    if (!symbol.has_value() ||
        (symbol->get().kind != SymbolKind::Struct && symbol->get().kind != SymbolKind::Enum &&
         symbol->get().kind != SymbolKind::TypeAlias && symbol->get().kind != SymbolKind::Trait)) {
        send_null(transport_, req.id);
        return;
    }
    auto location = indexed_symbol_location(*snapshot, symbol->get());
    if (!location.has_value()) {
        location = symbol_location(*snapshot, symbol->get(), *source);
    }
    if (!location.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_location(*location);
    transport_.send_response(resp);
}

void LspServer::handle_implementation(const JsonRpcRequest &req) {
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
    std::vector<Location> locations;
    if (const auto target = symbol_at(*snapshot, *source, offset); target.has_value()) {
        const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
        if (symbol.has_value()) {
            locations = implementation_locations_for_symbol(*snapshot, *source, symbol->get());
        }
    } else if (const auto primitive_type = primitive_type_key_at(*snapshot, *source, offset);
               primitive_type.has_value()) {
        locations = primitive_implementation_locations(*snapshot, *source, *primitive_type);
        if (const auto *sysroot_index = analysis_.sysroot_index_for_uri(uri);
            snapshot->analysis_mode != LspAnalysisMode::DetachedSourceUnit &&
            sysroot_index != nullptr) {
            for (const auto &location :
                 sysroot_index->implementation_locations_for_type(*primitive_type)) {
                push_unique_location(locations, location);
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_location_array(locations);
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
    std::vector<Location> locations;

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
                auto location = indexed_symbol_location(*snapshot, symbol->get());
                if (!location.has_value()) {
                    location = symbol_location(*snapshot, symbol->get(), *source);
                }
                if (location.has_value()) {
                    push_unique_location(locations, *location);
                }
            }
        }

        bool used_workspace_index = false;
        const auto symbol = snapshot->resolve_result.symbol_table.get(*target);
        if (symbol.has_value() && snapshot->workspace_index != nullptr) {
            const auto def = snapshot->workspace_def_for_symbol(symbol->get().id);
            if (def.has_value()) {
                for (const auto &location :
                     snapshot->workspace_index->reference_locations_for_def(*def)) {
                    push_unique_location(locations, location);
                }
                used_workspace_index = true;
            }
        }
        if (symbol.has_value()) {
            if (const auto *sysroot_index = analysis_.sysroot_index_for_uri(uri);
                sysroot_index != nullptr) {
                const auto def = index_def_for_symbol(*sysroot_index, *snapshot, symbol->get());
                if (def.has_value()) {
                    for (const auto &location : sysroot_index->reference_locations_for_def(*def)) {
                        push_unique_location(locations, location);
                    }
                }
            }
        }

        if (!used_workspace_index) {
            for (const auto &reference : snapshot->resolve_result.references()) {
                if (reference.target == *target) {
                    if (const auto location = reference_location(*snapshot, reference, *source);
                        location.has_value()) {
                        push_unique_location(locations, *location);
                    }
                }
            }
        }
    } else if (const auto local =
                   local_binding_at(*snapshot, *source, offset_at(*source->source, position));
               local.has_value()) {
        bool include_declaration = false;
        if (const auto *context = req.params->get("context"); context != nullptr) {
            if (const auto *include = context->get("includeDeclaration"); include != nullptr) {
                if (const auto value = include->as_bool(); value.has_value()) {
                    include_declaration = *value;
                }
            }
        }
        locations = local_binding_reference_locations(
            *snapshot, *source, local->target, include_declaration);
    }

    auto result = json::JsonValue::make_array();
    for (const auto &location : locations) {
        result->push(serialize_location(location));
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

    const auto offset = offset_at(*source->source, position);
    if (const auto local = local_binding_at(*snapshot, *source, offset); local.has_value()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_range(to_lsp_range(*source->source, local->token_range));
        transport_.send_response(resp);
        return;
    }

    const auto location = rename_location_at(*snapshot, *source, offset);
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

    const auto offset = offset_at(*source->source, position);
    if (const auto local = local_binding_at(*snapshot, *source, offset); local.has_value()) {
        const auto targets = collect_local_binding_targets(*source);
        if (local_binding_rename_conflicts(targets, local->target, new_name)) {
            send_invalid_params(
                transport_, req.id, "rename would conflict with an existing local binding");
            return;
        }

        WorkspaceEdit edit;
        for (const auto &location :
             local_binding_reference_locations(*snapshot, *source, local->target, true)) {
            edit.changes[location.uri].push_back(TextEdit{
                .range = location.range,
                .new_text = new_name,
            });
        }

        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_workspace_edit(edit);
        transport_.send_response(resp);
        return;
    }

    const auto target = symbol_at(*snapshot, *source, offset);
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

            items->push(
                serialize_workspace_diagnostic_item(source.uri, version, diagnostics, result_id));
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
    auto append_index_symbols = [&](const LspWorkspaceIndex &index) {
        for (const auto *symbol : index.workspace_symbols(query)) {
            if (symbol == nullptr) {
                continue;
            }
            const auto key = symbol->canonical_name + "@" + symbol->location.uri + ":" +
                             std::to_string(symbol->location.range.start.line) + ":" +
                             std::to_string(symbol->location.range.start.character);
            if (!emitted.insert(key).second) {
                continue;
            }

            SymbolInformation info;
            info.name = symbol->local_name;
            info.kind = to_lsp_symbol_kind(symbol->kind);
            info.location = symbol->location;
            result->push(serialize_symbol_information(info));
        }
    };

    for (const auto *index : analysis_.workspace_root_indices()) {
        if (index != nullptr) {
            append_index_symbols(*index);
        }
    }

    for (const auto *snapshot : analysis_.workspace_snapshots()) {
        if (snapshot == nullptr) {
            continue;
        }
        if (snapshot->workspace_index != nullptr) {
            append_index_symbols(*snapshot->workspace_index);
        }
        if (const auto *sysroot_index = analysis_.sysroot_index_for_uri(snapshot->requested_uri);
            sysroot_index != nullptr) {
            append_index_symbols(*sysroot_index);
        }

        if (snapshot->workspace_index != nullptr) {
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
    if (snapshot == nullptr || source == nullptr || source->source == nullptr) {
        send_null(transport_, req.id);
        return;
    }
    const bool has_typecheck = (snapshot->type_check_result != nullptr);

    const auto offset = offset_at(*source->source, position);
    if (auto pattern_help = pattern_signature_help(*snapshot, *source, offset);
        pattern_help.has_value()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_signature_help(*pattern_help);
        transport_.send_response(resp);
        return;
    }

    const auto context = call_context_before_cursor(*source->source, offset);
    if (!context.has_value()) {
        send_null(transport_, req.id);
        return;
    }

    const auto &[callable_name, active_parameter] = *context;
    SignatureHelp help;
    help.active_signature = 0;
    help.active_parameter = active_parameter;

    if (has_typecheck) {
        const auto &environment = snapshot->type_check_result->environment;
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
    }

    if (help.signatures.empty()) {
        // Keyword-signature fallback: statement-level assert-family keywords
        // do not live in the Capabilities/Predicates symbol namespaces, so we
        // resolve them here against the literal callable name extracted from
        // the source text.
        if (callable_name == "assert") {
            SignatureInformation info;
            info.label = "assert(condition: Bool[, message: String])";
            info.documentation =
                "Predicate assertion; throws assertion failure when condition is False. "
                "Optional message is rendered in the diagnostic.";
            {
                ParameterInformation p;
                p.label = "condition: Bool";
                p.documentation = "Bool predicate that must hold.";
                info.parameters.push_back(std::move(p));
            }
            {
                ParameterInformation p;
                p.label = "[message: String] (optional)";
                p.documentation = "Optional diagnostic message string.";
                info.parameters.push_back(std::move(p));
            }
            help.signatures.push_back(std::move(info));
        } else if (callable_name == "unwrap") {
            SignatureInformation info;
            info.label = "unwrap(value: Option<T>) -> T";
            info.documentation =
                "Optional deconstructor; if value is Some<T> returns T; "
                "if None raises unwrap-none assertion failure (kind: UNWRAP_NONE).";
            {
                ParameterInformation p;
                p.label = "value: Option<T>";
                p.documentation = "Optional value to deconstruct.";
                info.parameters.push_back(std::move(p));
            }
            help.signatures.push_back(std::move(info));
        } else if (callable_name == "requires") {
            SignatureInformation info;
            info.label = "requires(condition: Bool[, message: String])";
            info.documentation =
                "Contract requirement; fails contract evaluation "
                "(distinct from assert; kind: REQUIRES_VIOLATION) when condition is False.";
            {
                ParameterInformation p;
                p.label = "condition: Bool";
                p.documentation = "Bool contract predicate that must hold.";
                info.parameters.push_back(std::move(p));
            }
            {
                ParameterInformation p;
                p.label = "[message: String] (optional)";
                p.documentation = "Optional diagnostic message string.";
                info.parameters.push_back(std::move(p));
            }
            help.signatures.push_back(std::move(info));
        } else if (callable_name == "unreachable") {
            SignatureInformation info;
            info.label = "unreachable([message: String])";
            info.documentation =
                "Unreachable code marker; if ever executed raises kind: UNREACHABLE_EXECUTED.";
            {
                ParameterInformation p;
                p.label = "[message: String] (optional)";
                p.documentation = "Optional diagnostic message string.";
                info.parameters.push_back(std::move(p));
            }
            help.signatures.push_back(std::move(info));
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
    edit.range.end =
        Position{static_cast<uint32_t>(line_count), static_cast<uint32_t>(last_line_length)};
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

    auto tokens = compute_semantic_tokens(uri, analysis_);
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
    if (snapshot != nullptr && snapshot->analysis_mode == LspAnalysisMode::DetachedSourceUnit) {
        actions.erase(std::remove_if(actions.begin(),
                                     actions.end(),
                                     [](const CodeAction &action) {
                                         if (action.kind == CodeActionKind::SourceOrganizeImports) {
                                             return true;
                                         }
                                         return action.title == "Remove unused import";
                                     }),
                      actions.end());
    }

    // compute_code_actions uses "" as a sentinel URI in WorkspaceEdit::changes;
    // substitute it with the actual document URI here (single-document edits).
    for (auto &action : actions) {
        if (!action.edit) {
            continue;
        }
        auto it = action.edit->changes.find("");
        if (it == action.edit->changes.end()) {
            continue;
        }
        auto edits = std::move(it->second);
        action.edit->changes.erase(it);
        action.edit->changes.emplace(uri, std::move(edits));
    }

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

    std::vector<DocumentHighlight> highlights;
    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    if (snapshot != nullptr && source != nullptr && source->source != nullptr) {
        const auto offset = offset_at(*source->source, position);
        if (const auto local = local_binding_at(*snapshot, *source, offset); local.has_value()) {
            highlights = local_binding_document_highlights(*snapshot, *source, local->target);
        }
    }

    if (highlights.empty()) {
        highlights = compute_document_highlights(document->text, position);
    }

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

    std::vector<std::vector<Range>> semantic_ranges;
    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    const auto *source = snapshot != nullptr ? snapshot->source_for_uri(uri) : nullptr;
    const auto *program = snapshot != nullptr ? snapshot->typed_program() : nullptr;
    if (source != nullptr && source->source != nullptr && program != nullptr) {
        semantic_ranges.reserve(positions.size());
        for (const auto &position : positions) {
            const auto offset = offset_at(*source->source, position);
            semantic_ranges.push_back(typed_pattern_selection_ranges(
                *program, *source->source, source->source_id, offset));
        }
    }

    auto ranges = compute_selection_ranges(document->text, positions, semantic_ranges);

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

    std::vector<CodeLens> lenses;
    const auto *snapshot = analysis_.snapshot_for_uri(uri);
    if (snapshot != nullptr && snapshot->workspace_index != nullptr) {
        lenses = compute_code_lens(*snapshot->workspace_index, uri);
    } else {
        lenses = compute_code_lens(document->text);
    }

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
