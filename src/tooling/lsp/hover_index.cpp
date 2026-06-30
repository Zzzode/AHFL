#include "tooling/lsp/hover_index.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "tooling/lsp/analysis_service.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace ahfl::lsp {

namespace {

struct IdentifierSegment {
    std::string text;
    SourceRange range;
};

[[nodiscard]] bool same_source(std::optional<SourceId> lhs, std::optional<SourceId> rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || *lhs == *rhs;
}

[[nodiscard]] bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] bool contains(SourceRange range, std::size_t offset) noexcept {
    return range.begin_offset <= offset && offset <= range.end_offset;
}

[[nodiscard]] bool is_identifier_char(char ch) noexcept {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] bool is_identifier_start(char ch) noexcept {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalpha(value) != 0 || ch == '_';
}

[[nodiscard]] int default_priority(HoverTargetKind kind) noexcept {
    switch (kind) {
    case HoverTargetKind::Diagnostic:
    case HoverTargetKind::StructLiteral:
    case HoverTargetKind::EnumLiteral:
    case HoverTargetKind::ConstEval:
    case HoverTargetKind::ContractInstantiation:
    case HoverTargetKind::CapabilityInstantiation:
        return 0;
    case HoverTargetKind::ModuleName:
    case HoverTargetKind::ImportPath:
    case HoverTargetKind::ImportAlias:
    case HoverTargetKind::DeclarationName:
    case HoverTargetKind::TypeReference:
    case HoverTargetKind::ConstReference:
    case HoverTargetKind::CallableReference:
    case HoverTargetKind::StructField:
    case HoverTargetKind::EnumVariant:
    case HoverTargetKind::CapabilityParam:
    case HoverTargetKind::PredicateParam:
    case HoverTargetKind::AgentSchemaLabel:
    case HoverTargetKind::AgentTransition:
    case HoverTargetKind::FlowState:
    case HoverTargetKind::WorkflowSchemaLabel:
    case HoverTargetKind::WorkflowTemporalClause:
    case HoverTargetKind::WorkflowNode:
    case HoverTargetKind::WorkflowDependency:
    case HoverTargetKind::LocalBinding:
        return 1;
    case HoverTargetKind::AgentState:
        return 2;
    case HoverTargetKind::MemberAccess:
        return 2;
    case HoverTargetKind::Expression:
        return 20;
    case HoverTargetKind::Sentinel_ForStaticAssert:
        // Sentinel is never registered into a hover index; fall through to the
        // same "lowest priority" bucket as Expression so tie-breaks are stable.
        return 20;
    }
    return 20;
}

[[nodiscard]] std::size_t range_size(SourceRange range) noexcept {
    return range.end_offset >= range.begin_offset ? range.end_offset - range.begin_offset : 0;
}

[[nodiscard]] SourceRange bounded_range(const SourceFile &source, SourceRange range) noexcept {
    const auto begin = std::min(range.begin_offset, source.content.size());
    const auto end = std::max(begin, std::min(range.end_offset, source.content.size()));
    return SourceRange{.begin_offset = begin, .end_offset = end};
}

[[nodiscard]] std::vector<IdentifierSegment> identifier_segments_in_range(const SourceFile &source,
                                                                          SourceRange raw_range) {
    std::vector<IdentifierSegment> segments;
    const auto range = bounded_range(source, raw_range);
    auto cursor = range.begin_offset;
    while (cursor < range.end_offset) {
        if (!is_identifier_start(source.content[cursor])) {
            ++cursor;
            continue;
        }

        const auto begin = cursor;
        ++cursor;
        while (cursor < range.end_offset && is_identifier_char(source.content[cursor])) {
            ++cursor;
        }
        segments.push_back(IdentifierSegment{
            .text = source.content.substr(begin, cursor - begin),
            .range = SourceRange{.begin_offset = begin, .end_offset = cursor},
        });
    }
    return segments;
}

[[nodiscard]] std::vector<SourceRange>
identifier_ranges_in_range(const SourceFile &source, SourceRange raw_range, std::string_view name) {
    std::vector<SourceRange> ranges;
    if (name.empty()) {
        return ranges;
    }

    const auto range = bounded_range(source, raw_range);
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
first_identifier_range(const SourceFile &source, SourceRange range, std::string_view name) {
    const auto ranges = identifier_ranges_in_range(source, range, name);
    if (ranges.empty()) {
        return std::nullopt;
    }
    return ranges.front();
}

[[nodiscard]] std::optional<SourceRange>
last_identifier_range(const SourceFile &source, SourceRange range, std::string_view name) {
    const auto ranges = identifier_ranges_in_range(source, range, name);
    if (ranges.empty()) {
        return std::nullopt;
    }
    return ranges.back();
}

[[nodiscard]] std::optional<SourceRange>
schema_label_range(const SourceFile &source, const ast::TypeSyntax &type, std::string_view label) {
    const auto type_begin = std::min(type.range.begin_offset, source.content.size());
    const auto line_begin_pos = source.content.rfind('\n', type_begin);
    const auto line_begin = line_begin_pos == std::string::npos ? 0 : line_begin_pos + 1;
    if (line_begin >= type_begin) {
        return std::nullopt;
    }

    std::string_view prefix(source.content.data() + line_begin, type_begin - line_begin);
    auto found = prefix.rfind(label);
    while (found != std::string_view::npos) {
        const auto absolute = line_begin + found;
        const auto before_ok = absolute == 0 || !is_identifier_char(source.content[absolute - 1]);
        const auto after = found + label.size();
        const auto after_ok = after >= prefix.size() || !is_identifier_char(prefix[after]);
        if (before_ok && after_ok) {
            auto cursor = after;
            while (cursor < prefix.size() &&
                   std::isspace(static_cast<unsigned char>(prefix[cursor])) != 0) {
                ++cursor;
            }
            if (cursor < prefix.size() && prefix[cursor] == ':') {
                return SourceRange{.begin_offset = absolute, .end_offset = absolute + label.size()};
            }
        }

        if (found == 0) {
            break;
        }
        found = prefix.rfind(label, found - 1);
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<SourceRange> temporal_clause_label_range(const SourceFile &source,
                                                                     SourceRange declaration,
                                                                     SourceRange formula,
                                                                     std::string_view label) {
    if (label.empty()) {
        return std::nullopt;
    }

    const auto formula_begin = std::min(formula.begin_offset, source.content.size());
    const auto declaration_begin = std::min(declaration.begin_offset, source.content.size());
    auto search_from = formula_begin;
    while (search_from > declaration_begin) {
        const auto found = source.content.rfind(label, search_from - 1);
        if (found == std::string::npos || found < declaration_begin) {
            return std::nullopt;
        }

        const auto before_ok = found == 0 || !is_identifier_char(source.content[found - 1]);
        const auto after = found + label.size();
        const auto after_ok =
            after >= source.content.size() || !is_identifier_char(source.content[after]);
        if (before_ok && after_ok) {
            auto cursor = after;
            while (cursor < source.content.size() &&
                   std::isspace(static_cast<unsigned char>(source.content[cursor])) != 0) {
                ++cursor;
            }
            if (cursor < source.content.size() && source.content[cursor] == ':') {
                return SourceRange{.begin_offset = found, .end_offset = after};
            }
        }

        if (found == 0) {
            break;
        }
        search_from = found;
    }

    return std::nullopt;
}

[[nodiscard]] std::string source_text_in_range(const SourceFile &source, SourceRange raw_range) {
    const auto range = bounded_range(source, raw_range);
    return source.content.substr(range.begin_offset, range.end_offset - range.begin_offset);
}

[[nodiscard]] MaybeCRef<Symbol> symbol_for_declaration(const LspAnalysisSnapshot &snapshot,
                                                       const LspSourceSnapshot &source,
                                                       SymbolKind kind,
                                                       std::string_view local_name,
                                                       SourceRange declaration_range) {
    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (symbol.kind == kind && symbol.local_name == local_name &&
            same_source(symbol.source_id, source.source_id) &&
            same_range(symbol.declaration_range, declaration_range)) {
            return std::cref(symbol);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SourceRange> symbol_selection_range(const SourceFile &source,
                                                                const Symbol &symbol) {
    return first_identifier_range(source, symbol.declaration_range, symbol.local_name);
}

void add_symbol_target(HoverTargetIndex &index,
                       const LspSourceSnapshot &source,
                       const Symbol &symbol) {
    if (source.source == nullptr) {
        return;
    }
    const auto selection = symbol_selection_range(*source.source, symbol);
    if (!selection.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::DeclarationName,
        .token_range = *selection,
        .source_id = source.source_id,
        .symbol_id = symbol.id,
        .local_name = symbol.local_name,
        .role = "declaration",
        .source_label = source.source->display_name,
    });
}

[[nodiscard]] HoverTargetKind target_kind_for_reference(const Symbol &symbol,
                                                        ReferenceKind reference_kind) {
    switch (reference_kind) {
    case ReferenceKind::ConstValue:
    case ReferenceKind::QualifiedValueOwnerType:
        return HoverTargetKind::ConstReference;
    case ReferenceKind::CallTarget:
    case ReferenceKind::TemporalCapability:
    case ReferenceKind::AgentCapability:
    case ReferenceKind::FnCallTarget:
    case ReferenceKind::EnumVariantConstructor:
        return HoverTargetKind::CallableReference;
    case ReferenceKind::WorkflowNodeTarget:
        break;
    case ReferenceKind::TypeName:
    case ReferenceKind::ContractTarget:
    case ReferenceKind::FlowTarget:
        break;
    }

    switch (symbol.kind) {
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Function:
        return HoverTargetKind::CallableReference;
    case SymbolKind::Const:
        return HoverTargetKind::ConstReference;
    case SymbolKind::Struct:
    case SymbolKind::Enum:
    case SymbolKind::TypeAlias:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
    case SymbolKind::Trait:
        return HoverTargetKind::TypeReference;
    }
    return HoverTargetKind::TypeReference;
}

[[nodiscard]] std::optional<ImportBinding> import_alias_at(const LspAnalysisSnapshot &snapshot,
                                                           const LspSourceSnapshot &source,
                                                           std::string_view alias) {
    for (const auto &binding : snapshot.resolve_result.imports()) {
        if (binding.alias == alias && same_source(binding.source_id, source.source_id)) {
            return binding;
        }
    }
    return std::nullopt;
}

void add_reference_target(HoverTargetIndex &index,
                          const LspAnalysisSnapshot &snapshot,
                          const LspSourceSnapshot &source,
                          const ResolvedReference &reference) {
    if (source.source == nullptr || !same_source(reference.source_id, source.source_id)) {
        return;
    }
    const auto symbol = snapshot.resolve_result.symbol_table.get(reference.target);
    if (!symbol.has_value()) {
        return;
    }

    const auto kind = target_kind_for_reference(symbol->get(), reference.kind);
    const auto segments = identifier_segments_in_range(*source.source, reference.range);
    if (segments.empty()) {
        index.add(HoverTarget{
            .kind = kind,
            .token_range = reference.range,
            .source_id = source.source_id,
            .symbol_id = reference.target,
            .local_name = reference.text,
            .role = "reference",
            .source_label = source.source->display_name,
        });
        return;
    }

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto &segment = segments[i];
        if (i == 0) {
            if (const auto alias = import_alias_at(snapshot, source, segment.text);
                alias.has_value() && segments.size() > 1) {
                index.add(HoverTarget{
                    .kind = HoverTargetKind::ImportAlias,
                    .token_range = segment.range,
                    .source_id = source.source_id,
                    .local_name = alias->alias,
                    .role = alias->target_module,
                    .source_label = source.source->display_name,
                });
                continue;
            }
        }

        index.add(HoverTarget{
            .kind = kind,
            .token_range = segment.range,
            .source_id = source.source_id,
            .symbol_id = reference.target,
            .local_name = segment.text,
            .role = "reference",
            .source_label = source.source->display_name,
        });
    }
}

void add_qualified_name_targets(HoverTargetIndex &index,
                                const LspSourceSnapshot &source,
                                const ast::QualifiedName &name,
                                HoverTargetKind kind,
                                std::string role) {
    if (source.source == nullptr) {
        return;
    }
    for (const auto &segment : identifier_segments_in_range(*source.source, name.range)) {
        index.add(HoverTarget{
            .kind = kind,
            .token_range = segment.range,
            .source_id = source.source_id,
            .local_name = segment.text,
            .role = role,
            .source_label = source.source->display_name,
        });
    }
}

void add_schema_target(HoverTargetIndex &index,
                       const LspSourceSnapshot &source,
                       HoverTargetKind kind,
                       const Symbol &owner,
                       std::string_view label,
                       const ast::TypeSyntax *type) {
    if (source.source == nullptr || type == nullptr) {
        return;
    }
    const auto range = schema_label_range(*source.source, *type, label);
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = kind,
        .token_range = *range,
        .source_id = source.source_id,
        .owner_symbol_id = owner.id,
        .local_name = std::string(label),
        .role = "schema field",
        .declared_spelling = type->spelling(),
        .source_label = source.source->display_name,
    });
}

void add_named_range_target(HoverTargetIndex &index,
                            const LspSourceSnapshot &source,
                            HoverTargetKind kind,
                            SourceRange container,
                            std::string_view name,
                            std::optional<SymbolId> owner_symbol_id,
                            std::string role) {
    if (source.source == nullptr) {
        return;
    }
    const auto range = first_identifier_range(*source.source, container, name);
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = kind,
        .token_range = *range,
        .source_id = source.source_id,
        .owner_symbol_id = owner_symbol_id,
        .local_name = std::string(name),
        .role = std::move(role),
        .source_label = source.source->display_name,
    });
}

[[nodiscard]] std::string bracketed_list(const std::vector<std::string> &items) {
    std::string out = "[";
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            out += ", ";
        }
        out += items[index];
    }
    out += "]";
    return out;
}

void add_agent_property_label_target(HoverTargetIndex &index,
                                     const LspSourceSnapshot &source,
                                     const Symbol &owner,
                                     SourceRange container,
                                     std::string_view label,
                                     std::string declared_spelling) {
    if (source.source == nullptr) {
        return;
    }
    const auto range = first_identifier_range(*source.source, container, label);
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::AgentSchemaLabel,
        .token_range = *range,
        .source_id = source.source_id,
        .owner_symbol_id = owner.id,
        .local_name = std::string(label),
        .role = "agent property",
        .declared_spelling = std::move(declared_spelling),
        .source_label = source.source->display_name,
    });
}

void add_agent_transition_label_target(HoverTargetIndex &index,
                                       const LspSourceSnapshot &source,
                                       const Symbol &owner,
                                       const ast::TransitionSyntax &transition) {
    if (source.source == nullptr) {
        return;
    }
    const auto range = first_identifier_range(*source.source, transition.range, "transition");
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::AgentTransition,
        .token_range = *range,
        .source_id = source.source_id,
        .owner_symbol_id = owner.id,
        .local_name = "transition",
        .role = "agent transition",
        .declared_spelling = transition.from_state + " -> " + transition.to_state,
        .source_label = source.source->display_name,
    });
}

void add_workflow_temporal_clause_target(HoverTargetIndex &index,
                                         const LspSourceSnapshot &source,
                                         const Symbol &owner,
                                         SourceRange declaration_range,
                                         const ast::TemporalExprSyntax &formula,
                                         std::string_view label) {
    if (source.source == nullptr) {
        return;
    }
    const auto range =
        temporal_clause_label_range(*source.source, declaration_range, formula.range, label);
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::WorkflowTemporalClause,
        .token_range = *range,
        .source_id = source.source_id,
        .owner_symbol_id = owner.id,
        .local_name = std::string(label),
        .role = "workflow temporal clause",
        .declared_spelling = source_text_in_range(*source.source, formula.range),
        .source_label = source.source->display_name,
    });
}

[[nodiscard]] std::optional<SymbolId> capability_symbol(const LspAnalysisSnapshot &snapshot,
                                                        const LspSourceSnapshot &source,
                                                        std::string_view name) {
    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (symbol.kind == SymbolKind::Capability && symbol.local_name == name &&
            same_source(symbol.source_id, source.source_id)) {
            return symbol.id;
        }
    }
    if (const auto symbol =
            snapshot.resolve_result.symbol_table.find_local(SymbolNamespace::Capabilities, name);
        symbol.has_value() && symbol->get().kind == SymbolKind::Capability) {
        return symbol->get().id;
    }
    return std::nullopt;
}

void add_agent_capability_targets(HoverTargetIndex &index,
                                  const LspAnalysisSnapshot &snapshot,
                                  const LspSourceSnapshot &source,
                                  SourceRange container,
                                  const std::vector<std::string> &capabilities) {
    if (source.source == nullptr) {
        return;
    }
    for (const auto &capability : capabilities) {
        for (const auto range : identifier_ranges_in_range(*source.source, container, capability)) {
            index.add(HoverTarget{
                .kind = HoverTargetKind::CallableReference,
                .token_range = range,
                .source_id = source.source_id,
                .symbol_id = capability_symbol(snapshot, source, capability),
                .local_name = capability,
                .role = "agent capability",
                .source_label = source.source->display_name,
            });
        }
    }
}

[[nodiscard]] bool has_resolved_type_reference(const LspAnalysisSnapshot &snapshot,
                                               const LspSourceSnapshot &source,
                                               SourceRange range) {
    return snapshot.resolve_result.find_reference(ReferenceKind::TypeName, range, source.source_id)
        .has_value();
}

[[nodiscard]] std::string_view builtin_type_constructor(const ast::TypeSyntax &type) noexcept {
    return std::visit(
        Overloaded{
            [](const ast::UnitType &) -> std::string_view { return {}; },
            [](const ast::NamedType &t) -> std::string_view {
                if (!t.type_args.empty() && t.name && t.name->segments.size() == 1) {
                    const auto &head = t.name->segments.front();
                    if (head == "Optional" || head == "List" || head == "Set" || head == "Map") {
                        return head;
                    }
                }
                return {};
            },
            [](const ast::BoolType &) -> std::string_view { return "Bool"; },
            [](const ast::IntType &) -> std::string_view { return "Int"; },
            [](const ast::FloatType &) -> std::string_view { return "Float"; },
            [](const ast::StringType &) -> std::string_view { return "String"; },
            [](const ast::BoundedStringType &) -> std::string_view { return "String"; },
            [](const ast::UuidType &) -> std::string_view { return "UUID"; },
            [](const ast::TimestampType &) -> std::string_view { return "Timestamp"; },
            [](const ast::DurationType &) -> std::string_view { return "Duration"; },
            [](const ast::DecimalType &) -> std::string_view { return "Decimal"; },
            [](const ast::FnType &) -> std::string_view { return "Fn"; },
            [](const ast::AppType &) -> std::string_view { return {}; },
        },
        type.node);
}

void add_builtin_type_constructor_target(HoverTargetIndex &index,
                                         const LspSourceSnapshot &source,
                                         const ast::TypeSyntax &type) {
    if (source.source == nullptr) {
        return;
    }
    const auto constructor = builtin_type_constructor(type);
    if (constructor.empty()) {
        return;
    }
    const auto range = first_identifier_range(*source.source, type.range, constructor);
    if (!range.has_value()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::TypeReference,
        .token_range = *range,
        .source_id = source.source_id,
        .local_name = std::string(constructor),
        .role = "builtin type",
        .source_label = source.source->display_name,
    });
}

void add_type_syntax_targets(HoverTargetIndex &index,
                             const LspAnalysisSnapshot &snapshot,
                             const LspSourceSnapshot &source,
                             const ast::TypeSyntax *type) {
    if (type == nullptr || source.source == nullptr) {
        return;
    }

    add_builtin_type_constructor_target(index, source, *type);

    std::visit(Overloaded{
                   [&](const ast::NamedType &t) {
                       if (t.name &&
                           !has_resolved_type_reference(snapshot, source, t.name->range)) {
                           for (const auto &segment :
                                identifier_segments_in_range(*source.source, t.name->range)) {
                               index.add(HoverTarget{
                                   .kind = HoverTargetKind::TypeReference,
                                   .token_range = segment.range,
                                   .source_id = source.source_id,
                                   .local_name = segment.text,
                                   .role = "builtin type",
                                   .source_label = source.source->display_name,
                               });
                           }
                       }
                       for (const auto &arg : t.type_args) {
                           add_type_syntax_targets(index, snapshot, source, arg.get());
                       }
                   },
                   [&](const ast::AppType &t) {
                       if (t.name &&
                           !has_resolved_type_reference(snapshot, source, t.name->range)) {
                           for (const auto &segment :
                                identifier_segments_in_range(*source.source, t.name->range)) {
                               index.add(HoverTarget{
                                   .kind = HoverTargetKind::TypeReference,
                                   .token_range = segment.range,
                                   .source_id = source.source_id,
                                   .local_name = segment.text,
                                   .role = "type constructor",
                                   .source_label = source.source->display_name,
                               });
                           }
                       }
                       for (const auto &arg : t.arguments) {
                           add_type_syntax_targets(index, snapshot, source, arg.get());
                       }
                   },
                   [](const auto &) {
                       // Leaf types with no sub-types or name — nothing to add
                   },
               },
               type->node);
}

void add_predicate_return_type_target(HoverTargetIndex &index,
                                      const LspSourceSnapshot &source,
                                      SourceRange declaration_range) {
    if (source.source == nullptr) {
        return;
    }
    const auto ranges = identifier_ranges_in_range(*source.source, declaration_range, "Bool");
    if (ranges.empty()) {
        return;
    }
    index.add(HoverTarget{
        .kind = HoverTargetKind::TypeReference,
        .token_range = ranges.back(),
        .source_id = source.source_id,
        .local_name = "Bool",
        .role = "builtin type",
        .source_label = source.source->display_name,
    });
}

[[nodiscard]] std::optional<SymbolId> local_struct_symbol(const LspAnalysisSnapshot &snapshot,
                                                          const LspSourceSnapshot &source,
                                                          const ast::QualifiedName *name) {
    if (name == nullptr) {
        return std::nullopt;
    }
    if (const auto reference = snapshot.resolve_result.find_reference(
            ReferenceKind::TypeName, name->range, source.source_id);
        reference.has_value()) {
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(reference->get().target);
            symbol.has_value() && symbol->get().kind == SymbolKind::Struct) {
            return symbol->get().id;
        }
    }

    const auto spelling = name->spelling();
    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (symbol.kind == SymbolKind::Struct && symbol.local_name == spelling &&
            same_source(symbol.source_id, source.source_id)) {
            return symbol.id;
        }
    }
    if (const auto symbol =
            snapshot.resolve_result.symbol_table.find_local(SymbolNamespace::Types, spelling);
        symbol.has_value() && symbol->get().kind == SymbolKind::Struct) {
        return symbol->get().id;
    }
    return std::nullopt;
}

void add_expr_syntax_targets(HoverTargetIndex &index,
                             const LspAnalysisSnapshot &snapshot,
                             const LspSourceSnapshot &source,
                             const ast::ExprSyntax *expr);

void add_struct_init_targets(HoverTargetIndex &index,
                             const LspAnalysisSnapshot &snapshot,
                             const LspSourceSnapshot &source,
                             const ast::ExprSyntax &owner_expr,
                             const ast::StructLiteralExpr &expr) {
    // A3: register the whole literal (TypeName { … }) as a construct-hover
    // target. The hover renderer shows "Creates `Foo` with fields {…}".
    const auto owner = local_struct_symbol(snapshot, source, expr.type_name.get());
    const std::string name_spelling =
        expr.type_name != nullptr ? expr.type_name->spelling() : std::string{};
    if (expr.type_name != nullptr) {
        add_named_range_target(index,
                               source,
                               HoverTargetKind::StructLiteral,
                               expr.type_name->range,
                               name_spelling,
                               owner,
                               "struct literal construct site");
    }
    // Also register the full literal range so hovering the opening `{` or
    // closing `}` surfaces the same construct summary.
    index.add(HoverTarget{
        .kind = HoverTargetKind::StructLiteral,
        .token_range = owner_expr.range,
        .source_id = source.source_id,
        .symbol_id = owner,
        .owner_symbol_id = owner,
        .local_name = name_spelling,
        .role = "struct literal construct site",
    });

    for (const auto &field : expr.fields) {
        if (!field) {
            continue;
        }
        add_named_range_target(index,
                               source,
                               HoverTargetKind::StructField,
                               field->range,
                               field->field_name,
                               owner,
                               "struct literal field");
        add_expr_syntax_targets(index, snapshot, source, field->value.get());
    }
}

// ---------------------------------------------------------------------------
// Construct-Hover shared detection block.  Called from BOTH the AST-walk
// entry (add_expr_syntax_targets, for MemberAccess / Path) and the typed-
// expressions entry (add_typed_targets, as a belt-and-suspenders fallback).
// The original detection lives in add_typed_targets; we replicate it here
// because the TypedHIRBuilder does NOT populate `typed.expressions` for
// most LSP pipelines (did_open / snapshot single-file mode) — the HIR
// builder is gated behind has_errors() checks that short-circuit before
// append_expression() on most non-trivial sources, even when typecheck
// itself has successfully produced per-node type info via find_expr().
//
// So the reliable path is: AST-walk → typed_program.find_expr(node_id)
// → use the node-local TypedExpr (populated by remember_expression_type
// at typecheck time, not append_expression).
// ---------------------------------------------------------------------------
void try_register_construct_targets(HoverTargetIndex &index,
                                    const LspAnalysisSnapshot &snapshot,
                                    const LspSourceSnapshot &source,
                                    const ast::ExprSyntax *expr,
                                    std::uint32_t fallback_expr_index) {
    if (snapshot.type_check_result == nullptr || expr == nullptr ||
        source.source == nullptr) {
        return;
    }
    const auto *type_check_result = snapshot.type_check_result.get();
    const TypedExpr *typed = type_check_result->typed_program.find_expr(
        expr->node_id, source.source_id);
    std::uint32_t expr_index = fallback_expr_index;
    if (typed == nullptr && fallback_expr_index < type_check_result->typed_program.expressions.size()) {
        typed = &type_check_result->typed_program.expressions[fallback_expr_index];
    }
    if (typed == nullptr) {
        return;
    }
    const auto &e = *typed;

    // EnumLiteral detection — mirror of the add_typed_targets block.
    if (e.type != nullptr) {
        const EnumTypeInfo *matched_enum_info = nullptr;
        const bool matched = e.type->visit(types::Overloads{
            [&](const types::EnumT &enum_type) -> bool {
                const EnumTypeInfo *enum_info = nullptr;
                if (enum_type.symbol.has_value()) {
                    auto it = type_check_result->environment.enums().find(
                        enum_type.symbol->value);
                    if (it != type_check_result->environment.enums().end()) {
                        enum_info = &it->second;
                    }
                }
                if (enum_info == nullptr) {
                    for (const auto &[id, ei] :
                         type_check_result->environment.enums()) {
                        (void)id;
                        if (ei.canonical_name == enum_type.canonical_name) {
                            enum_info = &ei;
                            break;
                        }
                    }
                }
                if (enum_info == nullptr) {
                    return false;
                }
                std::string_view candidate_name;
                if (e.kind == ast::ExprSyntaxKind::MemberAccess &&
                    !e.member_name.empty()) {
                    candidate_name = e.member_name;
                } else {
                    candidate_name = e.semantic_name;
                }
                // semantic_name is fully-qualified ("Priority::High"); strip
                // the prefix so we match variant short names ("High") in
                // has_variant().  Always use the last segment because:
                //   - For MemberAccess with empty member_name (TypedExpr
                //     path where remember_expression_type only populated
                //     semantic_name), the last segment == the variant name.
                //   - For PathExpr with plain variant, there's no "::" and
                //     the string is returned unchanged.
                if (const auto sep = candidate_name.rfind("::");
                    sep != std::string_view::npos) {
                    candidate_name = candidate_name.substr(sep + 2);
                }
                const bool is_variant =
                    !candidate_name.empty() && enum_info->has_variant(candidate_name);
                if (is_variant) {
                    matched_enum_info = enum_info;
                }
                return is_variant;
            },
            [](const auto &) -> bool { return false; },
        });
        if (matched) {
            SourceRange effective_range = e.range;
                // Determine the identifier to narrow-to: prefer the concrete
                // member_name (populated by TypedHirBuilder for the flat
                // expressions vector); fall back to the last segment of the
                // semantic_name (populated by remember_expression_type via
                // the node_id index — this is the path that fires for AST-
                // walk registrations where member_name is blank).
                std::string narrow_target;
                if (e.kind == ast::ExprSyntaxKind::MemberAccess &&
                    !e.member_name.empty()) {
                    narrow_target = std::string{e.member_name};
                } else {
                    const auto sep = e.semantic_name.rfind("::");
                    if (sep != std::string::npos) {
                        narrow_target =
                            std::string{e.semantic_name.substr(sep + 2)};
                    } else if (!e.semantic_name.empty()) {
                        narrow_target = std::string{e.semantic_name};
                    }
                }
                if (!narrow_target.empty()) {
                    if (const auto narrow = last_identifier_range(
                            *source.source, e.range, narrow_target);
                        narrow.has_value()) {
                        effective_range = *narrow;
                    }
                }
            index.add(HoverTarget{
                .kind = HoverTargetKind::EnumLiteral,
                .token_range = effective_range,
                .source_id = source.source_id,
                .symbol_id = e.resolved_symbol,
                // owner_symbol_id carries the enum type's SymbolId so the
                // hover read-side can look up variant payload types for
                // tuple-variant Construct Hover (Wave-21 Lane C-1).
                .owner_symbol_id =
                    matched_enum_info ? std::optional<SymbolId>{matched_enum_info->symbol}
                                      : std::nullopt,
                .typed_expr_index = expr_index,
                .local_name = e.semantic_name,
                .role = "enum-variant construct site",
                .source_label = source.source->display_name,
            });
        }
    }

    // ConstEval detection — mirror of the add_typed_targets block.
    if (e.resolved_symbol.has_value()) {
        const auto ct = type_check_result->environment.get_const_type(
            *e.resolved_symbol);
        if (ct.has_value()) {
            index.add(HoverTarget{
                .kind = HoverTargetKind::ConstEval,
                .token_range = e.range,
                .source_id = source.source_id,
                .symbol_id = e.resolved_symbol,
                .typed_expr_index = expr_index,
                .local_name = e.semantic_name,
                .role = "compile-time constant reference",
                .source_label = source.source->display_name,
            });
        }
    }
}

void add_expr_syntax_targets(HoverTargetIndex &index,
                             const LspAnalysisSnapshot &snapshot,
                             const LspSourceSnapshot &source,
                             const ast::ExprSyntax *expr) {
    if (expr == nullptr) {
        return;
    }

    // Entry trace: surfaces every AST-expression walk so we can confirm
    // struct-field default values and flow-body expressions are visited.

    // NOTE: the `try_register_construct_targets` call below runs BEFORE the
    // std::visit because MemberAccessExpr and PathExpr are both leaf-ish
    // nodes w.r.t. construct detection — we want the narrowest (variant-
    // identifier / const-reference) range registered as early as possible
    // for tie-breaking.  Sub-expression recursion (done inside visit) then
    // registers any nested construct sites, e.g. a ConstEval used as the
    // enum-variant payload inside `Some(K * 2)`.
    try_register_construct_targets(index, snapshot, source, expr, 0U);

    std::visit(Overloaded{
                   [&](const ast::StructLiteralExpr &e) {
                       add_struct_init_targets(index, snapshot, source, *expr, e);
                   },
                   [&](const ast::CallExpr &e) {
                       for (const auto &item : e.arguments) {
                           add_expr_syntax_targets(index, snapshot, source, item.get());
                       }
                   },
                   [&](const ast::UnaryExpr &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.operand.get());
                   },
                   [&](const ast::GroupExpr &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.inner.get());
                   },
                   [&](const ast::BinaryExpr &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.lhs.get());
                       add_expr_syntax_targets(index, snapshot, source, e.rhs.get());
                   },
                   [&](const ast::IndexAccessExpr &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.base.get());
                       add_expr_syntax_targets(index, snapshot, source, e.index.get());
                   },
                   [&](const ast::MemberAccessExpr &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.base.get());
                   },
                   [&](const ast::UnwrapExprSyntax &e) {
                       add_expr_syntax_targets(index, snapshot, source, e.operand.get());
                   },
                   [](const auto &) {
                       // Leaf expressions with no sub-expressions — nothing to add
                   },
               },
               expr->node);
}

void add_all_named_targets(HoverTargetIndex &index,
                           const LspSourceSnapshot &source,
                           HoverTargetKind kind,
                           SourceRange container,
                           std::string_view name,
                           std::optional<SymbolId> owner_symbol_id,
                           std::string role) {
    if (source.source == nullptr) {
        return;
    }
    for (const auto range : identifier_ranges_in_range(*source.source, container, name)) {
        index.add(HoverTarget{
            .kind = kind,
            .token_range = range,
            .source_id = source.source_id,
            .owner_symbol_id = owner_symbol_id,
            .local_name = std::string(name),
            .role = role,
            .source_label = source.source->display_name,
        });
    }
}

[[nodiscard]] const FlowTypeInfo *
flow_at(const TypeEnvironment &environment, std::optional<SourceId> source_id, SourceRange range) {
    (void)source_id;
    for (const auto &[_, flow] : environment.flows()) {
        (void)_;
        if (contains(flow.declaration_range, range.begin_offset)) {
            return &flow;
        }
    }
    return nullptr;
}

[[nodiscard]] const FlowTypeInfo *flow_for_declaration(const TypeEnvironment &environment,
                                                       const ast::FlowDecl &decl) {
    for (const auto &[_, flow] : environment.flows()) {
        (void)_;
        if (same_range(flow.declaration_range, decl.range)) {
            return &flow;
        }
    }
    return nullptr;
}

void add_block_targets(HoverTargetIndex &index,
                       const LspAnalysisSnapshot &snapshot,
                       const LspSourceSnapshot &source,
                       const ast::BlockSyntax *block,
                       SymbolId owner_agent);

void add_statement_targets(HoverTargetIndex &index,
                           const LspAnalysisSnapshot &snapshot,
                           const LspSourceSnapshot &source,
                           const ast::StatementSyntax &statement,
                           SymbolId owner_agent) {
    switch (statement.kind) {
    case ast::StatementSyntaxKind::Goto:
        if (statement.goto_stmt) {
            add_named_range_target(index,
                                   source,
                                   HoverTargetKind::AgentState,
                                   statement.goto_stmt->range,
                                   statement.goto_stmt->target_state,
                                   owner_agent,
                                   "goto target");
        }
        return;
    case ast::StatementSyntaxKind::Let:
        if (statement.let_stmt) {
            add_named_range_target(index,
                                   source,
                                   HoverTargetKind::LocalBinding,
                                   statement.let_stmt->range,
                                   statement.let_stmt->name,
                                   std::nullopt,
                                   "local binding");
            add_type_syntax_targets(index, snapshot, source, statement.let_stmt->type.get());
            add_expr_syntax_targets(index, snapshot, source, statement.let_stmt->initializer.get());
        }
        return;
    case ast::StatementSyntaxKind::Assign:
        if (statement.assign_stmt) {
            add_expr_syntax_targets(index, snapshot, source, statement.assign_stmt->value.get());
        }
        return;
    case ast::StatementSyntaxKind::If:
        if (statement.if_stmt) {
            add_expr_syntax_targets(index, snapshot, source, statement.if_stmt->condition.get());
            add_block_targets(
                index, snapshot, source, statement.if_stmt->then_block.get(), owner_agent);
            add_block_targets(
                index, snapshot, source, statement.if_stmt->else_block.get(), owner_agent);
        }
        return;
    case ast::StatementSyntaxKind::IfLet:
        // RFC e-1 minimal POC: walk scrutinee + then/else bodies so hover
        // targets inside the new syntax are still indexed.  Pattern bindings
        // are not materialised as symbols yet (narrowing / symbol-intro
        // deferred to follow-up work).
        if (statement.if_let_stmt) {
            add_expr_syntax_targets(
                index, snapshot, source, statement.if_let_stmt->scrutinee.get());
            add_block_targets(
                index, snapshot, source, statement.if_let_stmt->then_block.get(), owner_agent);
            add_block_targets(
                index, snapshot, source, statement.if_let_stmt->else_block.get(), owner_agent);
        }
        return;
    case ast::StatementSyntaxKind::Return:
        if (statement.return_stmt) {
            add_expr_syntax_targets(index, snapshot, source, statement.return_stmt->value.get());
        }
        return;
    case ast::StatementSyntaxKind::Assert:
        if (statement.assert_stmt) {
            add_expr_syntax_targets(
                index, snapshot, source, statement.assert_stmt->condition.get());
            add_expr_syntax_targets(
                index, snapshot, source, statement.assert_stmt->message.get());
        }
        return;
    case ast::StatementSyntaxKind::Unwrap:
        if (statement.unwrap_stmt) {
            add_expr_syntax_targets(
                index, snapshot, source, statement.unwrap_stmt->operand.get());
        }
        return;
    case ast::StatementSyntaxKind::Requires:
        if (statement.requires_stmt) {
            add_expr_syntax_targets(
                index, snapshot, source, statement.requires_stmt->condition.get());
            add_expr_syntax_targets(
                index, snapshot, source, statement.requires_stmt->message.get());
        }
        return;
    case ast::StatementSyntaxKind::Unreachable:
        if (statement.unreachable_stmt) {
            add_expr_syntax_targets(
                index, snapshot, source, statement.unreachable_stmt->message.get());
        }
        return;
    case ast::StatementSyntaxKind::Expr:
        if (statement.expr_stmt) {
            add_expr_syntax_targets(index, snapshot, source, statement.expr_stmt->expr.get());
        }
        return;
    }
}

void add_block_targets(HoverTargetIndex &index,
                       const LspAnalysisSnapshot &snapshot,
                       const LspSourceSnapshot &source,
                       const ast::BlockSyntax *block,
                       SymbolId owner_agent) {
    if (block == nullptr) {
        return;
    }
    for (const auto &statement : block->statements) {
        if (statement) {
            add_statement_targets(index, snapshot, source, *statement, owner_agent);
        }
    }
}

void add_ast_targets_for_declaration(HoverTargetIndex &index,
                                     const LspAnalysisSnapshot &snapshot,
                                     const LspSourceSnapshot &source,
                                     const ast::Decl &declaration) {
    if (source.source == nullptr) {
        return;
    }

    switch (declaration.kind) {
    case ast::NodeKind::ModuleDecl: {
        const auto &module = static_cast<const ast::ModuleDecl &>(declaration);
        if (module.name) {
            add_qualified_name_targets(
                index, source, *module.name, HoverTargetKind::ModuleName, module.name->spelling());
        }
        return;
    }
    case ast::NodeKind::ImportDecl: {
        const auto &import = static_cast<const ast::ImportDecl &>(declaration);
        if (import.path) {
            add_qualified_name_targets(
                index, source, *import.path, HoverTargetKind::ImportPath, import.path->spelling());
        }
        if (!import.alias.empty()) {
            add_named_range_target(index,
                                   source,
                                   HoverTargetKind::ImportAlias,
                                   import.range,
                                   import.alias,
                                   std::nullopt,
                                   import.path ? import.path->spelling() : std::string{});
        }
        return;
    }
    case ast::NodeKind::StructDecl: {
        const auto &decl = static_cast<const ast::StructDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Struct, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        for (const auto &field : decl.fields) {
            if (field) {
                add_named_range_target(index,
                                       source,
                                       HoverTargetKind::StructField,
                                       field->range,
                                       field->name,
                                       symbol->get().id,
                                       "struct field");
                add_type_syntax_targets(index, snapshot, source, field->type.get());
                add_expr_syntax_targets(index, snapshot, source, field->default_value.get());
            }
        }
        return;
    }
    case ast::NodeKind::EnumDecl: {
        const auto &decl = static_cast<const ast::EnumDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Enum, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        for (const auto &variant : decl.variants) {
            if (variant) {
                add_named_range_target(index,
                                       source,
                                       HoverTargetKind::EnumVariant,
                                       variant->range,
                                       variant->name,
                                       symbol->get().id,
                                       "enum variant");
            }
        }
        return;
    }
    case ast::NodeKind::CapabilityDecl: {
        const auto &decl = static_cast<const ast::CapabilityDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Capability, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        for (const auto &param : decl.params) {
            if (param) {
                add_named_range_target(index,
                                       source,
                                       HoverTargetKind::CapabilityParam,
                                       param->range,
                                       param->name,
                                       symbol->get().id,
                                       "capability parameter");
                add_type_syntax_targets(index, snapshot, source, param->type.get());
            }
        }
        add_type_syntax_targets(index, snapshot, source, decl.return_type.get());
        return;
    }
    case ast::NodeKind::PredicateDecl: {
        const auto &decl = static_cast<const ast::PredicateDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Predicate, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        for (const auto &param : decl.params) {
            if (param) {
                add_named_range_target(index,
                                       source,
                                       HoverTargetKind::PredicateParam,
                                       param->range,
                                       param->name,
                                       symbol->get().id,
                                       "predicate parameter");
                add_type_syntax_targets(index, snapshot, source, param->type.get());
            }
        }
        add_predicate_return_type_target(index, source, decl.range);
        return;
    }
    case ast::NodeKind::AgentDecl: {
        const auto &decl = static_cast<const ast::AgentDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Agent, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        add_schema_target(index,
                          source,
                          HoverTargetKind::AgentSchemaLabel,
                          symbol->get(),
                          "input",
                          decl.input_type.get());
        add_type_syntax_targets(index, snapshot, source, decl.input_type.get());
        add_schema_target(index,
                          source,
                          HoverTargetKind::AgentSchemaLabel,
                          symbol->get(),
                          "context",
                          decl.context_type.get());
        add_type_syntax_targets(index, snapshot, source, decl.context_type.get());
        add_schema_target(index,
                          source,
                          HoverTargetKind::AgentSchemaLabel,
                          symbol->get(),
                          "output",
                          decl.output_type.get());
        add_type_syntax_targets(index, snapshot, source, decl.output_type.get());
        add_agent_property_label_target(
            index, source, symbol->get(), decl.range, "states", bracketed_list(decl.states));
        add_agent_property_label_target(
            index, source, symbol->get(), decl.range, "initial", decl.initial_state);
        add_agent_property_label_target(
            index, source, symbol->get(), decl.range, "final", bracketed_list(decl.final_states));
        add_agent_property_label_target(index,
                                        source,
                                        symbol->get(),
                                        decl.range,
                                        "capabilities",
                                        bracketed_list(decl.capabilities));
        for (const auto &transition : decl.transitions) {
            if (transition) {
                add_agent_transition_label_target(index, source, symbol->get(), *transition);
            }
        }
        add_agent_capability_targets(index, snapshot, source, decl.range, decl.capabilities);
        for (const auto &state : decl.states) {
            add_all_named_targets(index,
                                  source,
                                  HoverTargetKind::AgentState,
                                  decl.range,
                                  state,
                                  symbol->get().id,
                                  "agent state");
        }
        return;
    }
    case ast::NodeKind::FlowDecl: {
        if (snapshot.type_check_result == nullptr) {
            return;
        }
        const auto &decl = static_cast<const ast::FlowDecl &>(declaration);
        const auto *flow = flow_for_declaration(snapshot.type_check_result->environment, decl);
        if (flow == nullptr) {
            return;
        }
        for (const auto &handler : decl.state_handlers) {
            if (!handler) {
                continue;
            }
            add_named_range_target(index,
                                   source,
                                   HoverTargetKind::FlowState,
                                   handler->range,
                                   handler->state_name,
                                   flow->target_symbol,
                                   "flow state");
            add_block_targets(index, snapshot, source, handler->body.get(), flow->target_symbol);
        }
        return;
    }
    case ast::NodeKind::WorkflowDecl: {
        const auto &decl = static_cast<const ast::WorkflowDecl &>(declaration);
        const auto symbol =
            symbol_for_declaration(snapshot, source, SymbolKind::Workflow, decl.name, decl.range);
        if (!symbol.has_value()) {
            return;
        }
        add_schema_target(index,
                          source,
                          HoverTargetKind::WorkflowSchemaLabel,
                          symbol->get(),
                          "input",
                          decl.input_type.get());
        add_type_syntax_targets(index, snapshot, source, decl.input_type.get());
        add_schema_target(index,
                          source,
                          HoverTargetKind::WorkflowSchemaLabel,
                          symbol->get(),
                          "output",
                          decl.output_type.get());
        add_type_syntax_targets(index, snapshot, source, decl.output_type.get());
        for (const auto &node : decl.nodes) {
            if (!node) {
                continue;
            }
            add_named_range_target(index,
                                   source,
                                   HoverTargetKind::WorkflowNode,
                                   node->range,
                                   node->name,
                                   symbol->get().id,
                                   "workflow node");
            add_expr_syntax_targets(index, snapshot, source, node->input.get());
            for (const auto &dependency : node->after) {
                add_named_range_target(index,
                                       source,
                                       HoverTargetKind::WorkflowDependency,
                                       node->range,
                                       dependency,
                                       symbol->get().id,
                                       "workflow dependency");
            }
        }
        for (const auto &formula : decl.safety) {
            if (formula) {
                add_workflow_temporal_clause_target(
                    index, source, symbol->get(), decl.range, *formula, "safety");
            }
        }
        for (const auto &formula : decl.liveness) {
            if (formula) {
                add_workflow_temporal_clause_target(
                    index, source, symbol->get(), decl.range, *formula, "liveness");
            }
        }
        add_expr_syntax_targets(index, snapshot, source, decl.return_value.get());
        return;
    }
    case ast::NodeKind::TypeAliasDecl:
        add_type_syntax_targets(
            index,
            snapshot,
            source,
            static_cast<const ast::TypeAliasDecl &>(declaration).aliased_type.get());
        return;
    case ast::NodeKind::ConstDecl:
        add_type_syntax_targets(
            index, snapshot, source, static_cast<const ast::ConstDecl &>(declaration).type.get());
        add_expr_syntax_targets(
            index, snapshot, source, static_cast<const ast::ConstDecl &>(declaration).value.get());
        return;
    case ast::NodeKind::ContractDecl:
    case ast::NodeKind::Program:
    case ast::NodeKind::FnDecl:
        // P2 (RFC §3.2.2): fn-declaration hover targets (param/return/effect/
        // where-clause) land in P2b once fn symbols are registered. P2a only
        // needs the AST surface to parse.
        return;
    case ast::NodeKind::TraitDecl:
    case ast::NodeKind::ImplDecl:
        // P3 (RFC §3.2.2 / type-system §1.3 / §1.4): trait/impl hover targets
        // (super-traits, method signatures, assoc types) land in P3b once
        // trait symbols and impl-method bindings are registered. P3a only
        // needs the AST surface to parse.
        return;
    }
}

void add_ast_targets(HoverTargetIndex &index,
                     const LspAnalysisSnapshot &snapshot,
                     const LspSourceSnapshot &source) {
    if (source.program == nullptr) {
        return;
    }
    for (const auto &declaration : source.program->declarations) {
        if (declaration) {
            add_ast_targets_for_declaration(index, snapshot, source, *declaration);
        }
    }
}

void add_resolver_targets(HoverTargetIndex &index,
                          const LspAnalysisSnapshot &snapshot,
                          const LspSourceSnapshot &source) {
    for (const auto &symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (same_source(symbol.source_id, source.source_id)) {
            add_symbol_target(index, source, symbol);
        }
    }
    for (const auto &reference : snapshot.resolve_result.references()) {
        add_reference_target(index, snapshot, source, reference);
    }
}

void add_path_targets(HoverTargetIndex &index,
                      const LspSourceSnapshot &source,
                      const TypeEnvironment *environment,
                      const TypedExpr &expr,
                      std::uint32_t expr_index) {
    if (source.source == nullptr) {
        return;
    }
    const auto segments = identifier_segments_in_range(*source.source, expr.range);
    if (segments.empty()) {
        return;
    }

    std::optional<SymbolId> owner_agent;
    if (environment != nullptr) {
        if (const auto *flow = flow_at(*environment, source.source_id, expr.range);
            flow != nullptr) {
            owner_agent = flow->target_symbol;
        }
    }

    bool root_consumed = false;
    if (!expr.path_root.empty()) {
        for (const auto &segment : segments) {
            if (segment.text == expr.path_root) {
                HoverTargetKind kind = HoverTargetKind::Expression;
                std::string role = "path root";
                auto local_name = expr.path_root;
                if ((expr.path_root == "input" || expr.path_root == "context" ||
                     expr.path_root == "ctx" || expr.path_root == "output") &&
                    owner_agent.has_value()) {
                    kind = HoverTargetKind::AgentSchemaLabel;
                    if (expr.path_root == "ctx") {
                        local_name = "context";
                    }
                } else if (expr.path_root_kind == AssignTargetRootKind::Local ||
                           expr.path_root_kind == AssignTargetRootKind::Identifier) {
                    kind = HoverTargetKind::LocalBinding;
                    role = "path root";
                }
                index.add(HoverTarget{
                    .kind = kind,
                    .token_range = segment.range,
                    .source_id = source.source_id,
                    .owner_symbol_id = owner_agent,
                    .typed_expr_index = expr_index,
                    .local_name = local_name,
                    .role = role,
                    .source_label = source.source->display_name,
                });
                root_consumed = true;
                break;
            }
        }
    }

    std::size_t member_index = 0;
    for (const auto &segment : segments) {
        if (!root_consumed) {
            if (segment.text == expr.path_root) {
                root_consumed = true;
            }
            continue;
        }
        if (segment.text == expr.path_root) {
            continue;
        }
        if (member_index < expr.member_path.size() &&
            segment.text == expr.member_path[member_index]) {
            index.add(HoverTarget{
                .kind = HoverTargetKind::MemberAccess,
                .token_range = segment.range,
                .source_id = source.source_id,
                .typed_expr_index = expr_index,
                .local_name = segment.text,
                .role = "member access",
                .source_label = source.source->display_name,
            });
            ++member_index;
        }
    }
}

void add_typed_targets(HoverTargetIndex &index,
                       const LspSourceSnapshot &source,
                       const TypeCheckResult *type_check_result) {
    if (type_check_result == nullptr || source.source == nullptr) {
        return;
    }

    const auto &typed = type_check_result->typed_program;
    for (std::size_t i = 0; i < typed.expressions.size(); ++i) {
        const auto &expr = typed.expressions[i];
        if (!same_source(expr.source_id, source.source_id) || range_size(expr.range) == 0) {
            continue;
        }
        const auto expr_index = static_cast<std::uint32_t>(i);
        index.add(HoverTarget{
            .kind = HoverTargetKind::Expression,
            .token_range = expr.range,
            .source_id = source.source_id,
            .symbol_id = expr.resolved_symbol,
            .typed_expr_index = expr_index,
            .local_name = expr.semantic_name,
            .role = "expression",
            .source_label = source.source->display_name,
        });

        if (expr.kind == ast::ExprSyntaxKind::Path) {
            add_path_targets(index, source, &type_check_result->environment, expr, expr_index);
        }
        if (expr.kind == ast::ExprSyntaxKind::MemberAccess && !expr.member_name.empty()) {
            if (const auto range =
                    last_identifier_range(*source.source, expr.range, expr.member_name);
                range.has_value()) {
                index.add(HoverTarget{
                    .kind = HoverTargetKind::MemberAccess,
                    .token_range = *range,
                    .source_id = source.source_id,
                    .typed_expr_index = expr_index,
                    .local_name = expr.member_name,
                    .role = "member access",
                    .source_label = source.source->display_name,
                });
            }
        }

        // --------------------------------------------------------------------
        // Construct Hover (Wave-19 Lane 3 + Wave-20 Lane 1): write-side
        // registration for the priority-0 HoverTargetKind family.  The
        // read-side lives in hover_service.cpp::target_payload(…), so every
        // `index.add(…)` here must have a matching `case HoverTargetKind::…`
        // branch there.  Priority 0 beats every other priority bucket in the
        // ORDER CONTRACT, so adding these entries on the exact expression
        // range (smallest containing range) makes Construct Hover win over
        // the generic Expression, Reference, and MemberAccess entries.
        // --------------------------------------------------------------------

        // EnumLiteral: any expression that constructs a *specific* enum
        // variant value (e.g. `Priority::High`, `Color::Red`).
        //
        // Detection uses a two-key match because variant declarations do
        // not currently carry their own SymbolId (EnumVariantInfo only has
        // name/range/payload).  The detection rule:
        //   1. The expression's *result type* is the parent enum (EnumT),
        //      which carries the correct canonical_name and an optional
        //      SymbolId pointing at the enum declaration.
        //   2. The expression accesses a *specific member name*
        //      (MemberAccess.member_name for `Enum::Variant` form, or the
        //      semantic_name for plain Path form).  That member name must
        //      be present in the enum declaration's `has_variant(name)`
        //      set.
        //
        // Narrowing to the variant identifier (see COMMENT block above on
        // smallest-range tie-breaking) ensures the Construct Hover wins
        // over the enum-declaration site when the developer hovers the
        // *variant name* — the common IDE gesture.
        if (expr.type != nullptr) {
            const EnumTypeInfo *matched_enum_info = nullptr;
            const bool matched = expr.type->visit(types::Overloads{
                [&](const types::EnumT &enum_type) -> bool {
                    // (a) Resolve the concrete EnumTypeInfo for this enum
                    //     instantiation.  Prefer the SymbolId pointer; fall
                    //     back to a linear canonical_name scan.
                    const EnumTypeInfo *enum_info = nullptr;
                    if (enum_type.symbol.has_value()) {
                        auto it = type_check_result->environment.enums().find(
                            enum_type.symbol->value);
                        if (it != type_check_result->environment.enums().end()) {
                            enum_info = &it->second;
                        }
                    }
                    if (enum_info == nullptr) {
                        for (const auto &[id, ei] :
                             type_check_result->environment.enums()) {
                            (void)id;
                            if (ei.canonical_name == enum_type.canonical_name) {
                                enum_info = &ei;
                                break;
                            }
                        }
                    }
                    if (enum_info == nullptr) {
                        return false;
                    }
                    // (b) Decide which identifier names the variant.
                    std::string_view candidate_name;
                    if (expr.kind == ast::ExprSyntaxKind::MemberAccess &&
                        !expr.member_name.empty()) {
                        candidate_name = expr.member_name;
                    } else {
                        candidate_name = expr.semantic_name;
                    }
                    // semantic_name may be fully-qualified
                    // ("Priority::High"); strip to the last segment so
                    // has_variant(name) matches (variants store short names).
                    if (const auto sep = candidate_name.rfind("::");
                        sep != std::string_view::npos) {
                        candidate_name = candidate_name.substr(sep + 2);
                    }
                    const bool is_variant =
                        !candidate_name.empty() && enum_info->has_variant(candidate_name);
                    if (is_variant) {
                        matched_enum_info = enum_info;
                    }
                    return is_variant;
                },
                [](const auto &) -> bool { return false; },
            });
            if (matched) {
                SourceRange effective_range = expr.range;
                // Narrow to the variant identifier: prefer member_name when
                // populated; fall back to the last segment of semantic_name
                // (see helper try_register_construct_targets for rationale).
                std::string narrow_target;
                if (expr.kind == ast::ExprSyntaxKind::MemberAccess &&
                    !expr.member_name.empty()) {
                    narrow_target = std::string{expr.member_name};
                } else {
                    const auto sep = expr.semantic_name.rfind("::");
                    if (sep != std::string::npos) {
                        narrow_target =
                            std::string{expr.semantic_name.substr(sep + 2)};
                    } else if (!expr.semantic_name.empty()) {
                        narrow_target = std::string{expr.semantic_name};
                    }
                }
                if (!narrow_target.empty()) {
                    if (const auto narrow = last_identifier_range(
                            *source.source, expr.range, narrow_target);
                        narrow.has_value()) {
                        effective_range = *narrow;
                    }
                }
                index.add(HoverTarget{
                    .kind = HoverTargetKind::EnumLiteral,
                    .token_range = effective_range,
                    .source_id = source.source_id,
                    .symbol_id = expr.resolved_symbol,
                    // owner_symbol_id carries the enum type's SymbolId so
                    // the read-side can resolve variant payload types for
                    // tuple-variant Construct Hover (Wave-21 Lane C-1).
                    .owner_symbol_id =
                        matched_enum_info
                            ? std::optional<SymbolId>{matched_enum_info->symbol}
                            : std::nullopt,
                    .typed_expr_index = expr_index,
                    .local_name = expr.semantic_name,
                    .role = "enum-variant construct site",
                    .source_label = source.source->display_name,
                });
            }
        }

        // ConstEval: any expression that resolves to a Compile-Time Constant
        // symbol is a candidate for showing its fully-evaluated value inline
        // (not just the declaration-site type).  The renderer appends
        // `= <compile-time value>` after the canonical constant name.
        if (expr.resolved_symbol.has_value()) {
            const auto ct = type_check_result->environment.get_const_type(
                *expr.resolved_symbol);
            if (ct.has_value()) {
                index.add(HoverTarget{
                    .kind = HoverTargetKind::ConstEval,
                    .token_range = expr.range,
                    .source_id = source.source_id,
                    .symbol_id = expr.resolved_symbol,
                    .typed_expr_index = expr_index,
                    .local_name = expr.semantic_name,
                    .role = "compile-time constant reference",
                    .source_label = source.source->display_name,
                });
            }
        }
    }

    // Diagnostics: every diagnostic produced by the semantic pipeline (parse,
    // resolve, typecheck, lint) is also a priority-0 HoverTarget so hovering
    // the exact range shows the diagnostic message *before* any construct
    // hover.  Ordinal 22 (the lowest in the priority-0 group) further ensures
    // it wins over EnumLiteral / ConstEval on a fully-tied range.
    for (const auto &d : type_check_result->diagnostics.entries()) {
        if (!d.range.has_value() || range_size(*d.range) == 0) {
            continue;
        }
        // Diagnostic::source_name is an optional human-readable label.  When it
        // is set and does not match the current source, the diagnostic belongs
        // to a different unit (cross-module note lifted to the top-level bag)
        // and we skip it to avoid writing HoverTargets into the wrong file.
        if (d.source_name.has_value() && source.source != nullptr &&
            *d.source_name != source.source->display_name) {
            continue;
        }
        const auto code_text = d.code.has_value() ? *d.code : std::string{};
        index.add(HoverTarget{
            .kind = HoverTargetKind::Diagnostic,
            .token_range = *d.range,
            .source_id = source.source_id,
            .symbol_id = std::nullopt,
            .local_name = code_text,
            .role = d.message,
            .source_label = source.source->display_name,
        });
    }
}

} // namespace

void HoverTargetIndex::add(HoverTarget target) {
    target.priority = default_priority(target.kind);
    targets_.push_back(std::move(target));
}

void HoverTargetIndex::sort() {
    std::sort(targets_.begin(), targets_.end(), [](const HoverTarget &lhs, const HoverTarget &rhs) {
        if (lhs.token_range.begin_offset != rhs.token_range.begin_offset) {
            return lhs.token_range.begin_offset < rhs.token_range.begin_offset;
        }
        if (lhs.token_range.end_offset != rhs.token_range.end_offset) {
            return lhs.token_range.end_offset < rhs.token_range.end_offset;
        }
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        // ⚠️ Tie-break: enum ordinal ASC. This matches the ORDER CONTRACT in
        // hover_index.hpp above the HoverTargetKind enum — add new values to
        // the enum IN THE RIGHT PRIORITY GROUP, never in between existing
        // values within a group, because that changes tie-break semantics.
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    });
}

const HoverTarget *HoverTargetIndex::lookup(std::size_t offset) const noexcept {
    const HoverTarget *best = nullptr;
    std::size_t best_size = std::numeric_limits<std::size_t>::max();
    int best_priority = std::numeric_limits<int>::max();
    auto best_kind = HoverTargetKind::Expression; // ordinal used as 4th tie-break
    for (const auto &target : targets_) {
        if (target.token_range.begin_offset > offset) {
            break;
        }
        if (!contains(target.token_range, offset)) {
            continue;
        }
        const auto current_size = range_size(target.token_range);
        // Selection order (strict, mirrors sort comparator exactly):
        //   1. smallest containing range
        //   2. lowest numeric priority (0 = Construct family wins)
        //   3. lowest enum ordinal (see ORDER CONTRACT in hover_index.hpp)
        //   4. earlier in targets_ (implicit: first match wins on full tie)
        const bool is_better =
            best == nullptr || current_size < best_size ||
            (current_size == best_size && target.priority < best_priority) ||
            (current_size == best_size && target.priority == best_priority &&
             static_cast<int>(target.kind) < static_cast<int>(best_kind));
        if (is_better) {
            best = &target;
            best_size = current_size;
            best_priority = target.priority;
            best_kind = target.kind;
        }
    }
    return best;
}

std::size_t hover_index_key(const LspSourceSnapshot &source) noexcept {
    return source.source_id.has_value() ? source.source_id->value : 0;
}

void build_hover_indices(LspAnalysisSnapshot &snapshot) {
    snapshot.hover_indices.clear();
    for (const auto &source : snapshot.sources) {
        HoverTargetIndex index;
        add_ast_targets(index, snapshot, source);
        add_resolver_targets(index, snapshot, source);
        add_typed_targets(index, source, snapshot.type_check_result.get());
        index.sort();
        snapshot.hover_indices.emplace(hover_index_key(source), std::move(index));
    }
}

} // namespace ahfl::lsp
