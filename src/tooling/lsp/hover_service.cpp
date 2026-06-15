#include "tooling/lsp/hover_service.hpp"

#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace ahfl::lsp {

namespace {

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

[[nodiscard]] std::size_t range_size(SourceRange range) noexcept {
    return range.end_offset >= range.begin_offset ? range.end_offset - range.begin_offset : 0;
}

[[nodiscard]] bool position_before_or_equal(Position lhs, Position rhs) noexcept {
    return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character <= rhs.character);
}

[[nodiscard]] bool position_in_range(Position position, Range range) noexcept {
    return position_before_or_equal(range.start, position) &&
           position_before_or_equal(position, range.end);
}

[[nodiscard]] std::string type_description(TypePtr type) {
    return type != nullptr ? type->describe() : std::string{"<unknown>"};
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

[[nodiscard]] std::string params_signature(const std::vector<ParamTypeInfo> &params) {
    std::string out;
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (index > 0) {
            out += ", ";
        }
        out += params[index].name + ": " + type_description(params[index].type);
    }
    return out;
}

[[nodiscard]] std::string callable_signature(const CapabilityTypeInfo &capability) {
    return "capability " + capability.canonical_name + "(" + params_signature(capability.params) +
           ") -> " + type_description(capability.return_type);
}

[[nodiscard]] std::string callable_signature(const PredicateTypeInfo &predicate) {
    return "predicate " + predicate.canonical_name + "(" + params_signature(predicate.params) +
           ") -> Bool";
}

template <typename T>
[[nodiscard]] const T *payload_for_symbol(const TypedProgram *typed, SymbolId id) {
    if (typed == nullptr) {
        return nullptr;
    }
    for (const auto &decl : typed->declarations) {
        if (!(decl.symbol == id)) {
            continue;
        }
        return std::get_if<T>(&decl.payload);
    }
    return nullptr;
}

[[nodiscard]] HoverPayload base_payload(const HoverTarget &target, std::string summary) {
    HoverPayload payload;
    payload.summary = std::move(summary);
    payload.token_range = target.token_range;
    payload.source_label = target.source_label;
    return payload;
}

void add_symbol_identity(HoverPayload &payload, const Symbol &symbol) {
    payload.canonical_name = symbol.canonical_name;
    payload.module_name = symbol.module_name;
}

[[nodiscard]] HoverPayload symbol_payload(const LspAnalysisSnapshot &snapshot,
                                          const HoverTarget &target,
                                          const Symbol &symbol) {
    auto payload = base_payload(target, symbol_detail(symbol.kind));
    add_symbol_identity(payload, symbol);

    const auto *typed = snapshot.typed_program();
    const auto *type_result = snapshot.type_check_result.get();
    const auto *environment = type_result != nullptr ? &type_result->environment : nullptr;

    switch (symbol.kind) {
    case SymbolKind::Struct: {
        payload.ahfl_signature = "struct " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_struct(symbol.id); info.has_value()) {
                payload.facts.push_back("fields " + std::to_string(info->get().fields.size()));
            }
        }
        break;
    }
    case SymbolKind::Enum: {
        payload.ahfl_signature = "enum " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_enum(symbol.id); info.has_value()) {
                payload.facts.push_back("variants " + std::to_string(info->get().variants.size()));
            }
        }
        break;
    }
    case SymbolKind::TypeAlias: {
        const auto *alias = payload_for_symbol<TypeAliasDeclInfo>(typed, symbol.id);
        payload.ahfl_signature = "type " + symbol.canonical_name;
        if (alias != nullptr) {
            payload.ahfl_signature += " = " + type_description(alias->aliased_type);
            payload.declared_spelling = alias->aliased_type_spelling;
        }
        break;
    }
    case SymbolKind::Const: {
        payload.ahfl_signature = "const " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto type = environment->get_const_type(symbol.id); type.has_value()) {
                payload.ahfl_signature += ": " + type_description(&type->get());
            }
        }
        break;
    }
    case SymbolKind::Capability: {
        if (environment != nullptr) {
            if (const auto info = environment->get_capability(symbol.id); info.has_value()) {
                payload.ahfl_signature = callable_signature(info->get());
                if (info->get().effect.declared) {
                    std::vector<std::string> facts;
                    if (!info->get().effect.domain.empty()) {
                        facts.push_back("domain `" + info->get().effect.domain + "`");
                    }
                    if (!info->get().effect.timeout.empty()) {
                        facts.push_back("timeout `" + info->get().effect.timeout + "`");
                    }
                    if (!facts.empty()) {
                        std::string line = "effect";
                        for (const auto &fact : facts) {
                            line += " " + fact;
                        }
                        payload.facts.push_back(line);
                    }
                }
            }
        }
        if (payload.ahfl_signature.empty()) {
            payload.ahfl_signature = "capability " + symbol.canonical_name;
        }
        break;
    }
    case SymbolKind::Predicate: {
        if (environment != nullptr) {
            if (const auto info = environment->get_predicate(symbol.id); info.has_value()) {
                payload.ahfl_signature = callable_signature(info->get());
            }
        }
        if (payload.ahfl_signature.empty()) {
            payload.ahfl_signature = "predicate " + symbol.canonical_name;
        }
        break;
    }
    case SymbolKind::Agent: {
        payload.ahfl_signature = "agent " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_agent(symbol.id); info.has_value()) {
                payload.facts.push_back("input `" + type_description(info->get().input_type) + "`");
                payload.facts.push_back("context `" + type_description(info->get().context_type) +
                                        "`");
                payload.facts.push_back("output `" + type_description(info->get().output_type) +
                                        "`");
            }
        }
        break;
    }
    case SymbolKind::Workflow: {
        payload.ahfl_signature = "workflow " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_workflow(symbol.id); info.has_value()) {
                payload.facts.push_back("input `" + type_description(info->get().input_type) + "`");
                payload.facts.push_back("output `" + type_description(info->get().output_type) +
                                        "`");
                payload.facts.push_back("nodes " + std::to_string(info->get().nodes.size()));
            }
        }
        break;
    }
    }

    return payload;
}

[[nodiscard]] std::optional<HoverPayload>
schema_payload(const LspAnalysisSnapshot &snapshot, const HoverTarget &target, bool workflow) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }

    const auto &environment = snapshot.type_check_result->environment;
    if (!workflow && target.role == "agent property") {
        const auto info = environment.get_agent(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }

        auto payload = base_payload(target, "agent property");
        if (target.local_name == "states") {
            payload.summary = "agent state set";
            payload.facts.push_back("state count " + std::to_string(info->get().states.size()));
        } else if (target.local_name == "initial") {
            payload.summary = "agent initial state";
        } else if (target.local_name == "final") {
            payload.summary = "agent final states";
        } else if (target.local_name == "capabilities") {
            payload.summary = "agent capabilities";
            payload.facts.push_back("capability count " +
                                    std::to_string(info->get().capability_symbols.size()));
        }
        payload.ahfl_signature = target.local_name + ": " + target.declared_spelling;
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
            symbol.has_value()) {
            payload.canonical_name = symbol->get().canonical_name + "." + target.local_name;
            payload.module_name = symbol->get().module_name;
        }
        return payload;
    }

    auto payload = base_payload(target, workflow ? "workflow schema field" : "agent schema field");
    if (target.role == "path root") {
        payload.summary = workflow ? "workflow path root" : "agent path root";
    }

    const Type *type = nullptr;
    if (workflow) {
        const auto info = environment.get_workflow(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }
        if (target.local_name == "input") {
            type = info->get().input_type;
        } else if (target.local_name == "output") {
            type = info->get().output_type;
        }
    } else {
        const auto info = environment.get_agent(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }
        if (target.local_name == "input") {
            type = info->get().input_type;
        } else if (target.local_name == "context") {
            type = info->get().context_type;
        } else if (target.local_name == "output") {
            type = info->get().output_type;
        }
    }

    payload.ahfl_signature = target.local_name + ": " + type_description(type);
    payload.declared_spelling = target.declared_spelling;
    if (const auto symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
        symbol.has_value()) {
        payload.module_name = symbol->get().module_name;
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload> struct_field_payload(const LspAnalysisSnapshot &snapshot,
                                                               const HoverTarget &target) {
    if (!target.owner_symbol_id.has_value()) {
        auto payload = base_payload(target, "struct field");
        payload.ahfl_signature = "field " + target.local_name;
        return payload;
    }
    if (snapshot.type_check_result == nullptr) {
        return std::nullopt;
    }
    const auto info = snapshot.type_check_result->environment.get_struct(*target.owner_symbol_id);
    if (!info.has_value()) {
        return std::nullopt;
    }
    const auto field = info->get().find_field(target.local_name);
    if (!field.has_value()) {
        return std::nullopt;
    }
    auto payload = base_payload(target, "struct field");
    payload.ahfl_signature =
        "field " + field->get().name + ": " + type_description(field->get().type);
    payload.canonical_name = info->get().canonical_name + "." + field->get().name;
    if (field->get().has_default) {
        payload.facts.push_back("has default value");
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload> enum_variant_payload(const LspAnalysisSnapshot &snapshot,
                                                               const HoverTarget &target) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }
    const auto info = snapshot.type_check_result->environment.get_enum(*target.owner_symbol_id);
    if (!info.has_value() || !info->get().has_variant(target.local_name)) {
        return std::nullopt;
    }
    auto payload = base_payload(target, "enum variant");
    payload.ahfl_signature = info->get().canonical_name + "::" + target.local_name;
    payload.canonical_name = payload.ahfl_signature;
    return payload;
}

template <typename CallableInfo>
[[nodiscard]] std::optional<HoverPayload>
param_payload(const HoverTarget &target, const CallableInfo &callable, std::string summary) {
    for (std::size_t index = 0; index < callable.params.size(); ++index) {
        const auto &param = callable.params[index];
        if (param.name != target.local_name) {
            continue;
        }
        auto payload = base_payload(target, std::move(summary));
        payload.ahfl_signature = param.name + ": " + type_description(param.type);
        payload.canonical_name = callable.canonical_name + "." + param.name;
        payload.facts.push_back("parameter " + std::to_string(index + 1));
        return payload;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<HoverPayload> callable_param_payload(
    const LspAnalysisSnapshot &snapshot, const HoverTarget &target, bool predicate) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }
    const auto &environment = snapshot.type_check_result->environment;
    if (predicate) {
        const auto info = environment.get_predicate(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }
        return param_payload(target, info->get(), "predicate parameter");
    }

    const auto info = environment.get_capability(*target.owner_symbol_id);
    if (!info.has_value()) {
        return std::nullopt;
    }
    return param_payload(target, info->get(), "capability parameter");
}

[[nodiscard]] std::optional<HoverPayload> agent_state_payload(const LspAnalysisSnapshot &snapshot,
                                                              const HoverTarget &target,
                                                              std::string summary) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }
    const auto info = snapshot.type_check_result->environment.get_agent(*target.owner_symbol_id);
    if (!info.has_value()) {
        return std::nullopt;
    }

    auto payload = base_payload(target, std::move(summary));
    payload.ahfl_signature = "state " + target.local_name;
    if (target.local_name == info->get().initial_state) {
        payload.facts.push_back("initial state");
    }
    if (std::find(info->get().final_states.begin(),
                  info->get().final_states.end(),
                  target.local_name) != info->get().final_states.end()) {
        payload.facts.push_back("final state");
    }
    for (const auto &transition : info->get().transitions) {
        if (transition.from_state == target.local_name) {
            payload.facts.push_back("transition to `" + transition.to_state + "`");
        }
        if (transition.to_state == target.local_name) {
            payload.facts.push_back("transition from `" + transition.from_state + "`");
        }
    }
    if (const auto symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
        symbol.has_value()) {
        payload.canonical_name = symbol->get().canonical_name + "." + target.local_name;
        payload.module_name = symbol->get().module_name;
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload> workflow_node_payload(const LspAnalysisSnapshot &snapshot,
                                                                const HoverTarget &target,
                                                                bool dependency) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }
    const auto info = snapshot.type_check_result->environment.get_workflow(*target.owner_symbol_id);
    if (!info.has_value()) {
        return std::nullopt;
    }

    for (const auto &node : info->get().nodes) {
        if (node.name != target.local_name) {
            continue;
        }
        auto payload = base_payload(target, dependency ? "workflow dependency" : "workflow node");
        payload.ahfl_signature = "node " + node.name + ": " + node.target_name;
        if (!node.after.empty()) {
            std::string after = "after";
            for (const auto &name : node.after) {
                after += " `" + name + "`";
            }
            payload.facts.push_back(after);
        }
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(node.target_symbol);
            symbol.has_value()) {
            payload.canonical_name = symbol->get().canonical_name;
        }
        return payload;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<HoverPayload>
expression_payload(const LspAnalysisSnapshot &snapshot, const HoverTarget &target, bool member) {
    const auto *typed = snapshot.typed_program();
    if (typed == nullptr || !target.typed_expr_index.has_value() ||
        *target.typed_expr_index >= typed->expressions.size()) {
        return std::nullopt;
    }
    const auto &expr = typed->expressions[*target.typed_expr_index];
    if (expr.type == nullptr) {
        return std::nullopt;
    }

    auto payload = base_payload(target, member ? "member access" : "expression");
    if (member && !target.local_name.empty()) {
        payload.ahfl_signature = target.local_name + ": " + expr.type->describe();
    } else if (!target.local_name.empty()) {
        payload.ahfl_signature = target.local_name + ": " + expr.type->describe();
    } else {
        payload.ahfl_signature = expr.type->describe();
    }
    if (!expr.semantic_name.empty()) {
        payload.facts.push_back(expr.semantic_name);
    }
    if (expr.resolved_symbol.has_value()) {
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(*expr.resolved_symbol);
            symbol.has_value()) {
            payload.canonical_name = symbol->get().canonical_name;
        }
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload> target_payload(const LspAnalysisSnapshot &snapshot,
                                                         const HoverTarget &target) {
    switch (target.kind) {
    case HoverTargetKind::ModuleName: {
        auto payload = base_payload(target, "module");
        payload.ahfl_signature = "module " + target.role;
        payload.canonical_name = target.role;
        return payload;
    }
    case HoverTargetKind::ImportPath: {
        auto payload = base_payload(target, "import path");
        payload.ahfl_signature = "import " + target.role;
        payload.canonical_name = target.role;
        return payload;
    }
    case HoverTargetKind::ImportAlias: {
        auto payload = base_payload(target, "import alias");
        payload.ahfl_signature = "import " + target.role + " as " + target.local_name;
        payload.canonical_name = target.role;
        return payload;
    }
    case HoverTargetKind::DeclarationName:
    case HoverTargetKind::TypeReference:
    case HoverTargetKind::ConstReference:
    case HoverTargetKind::CallableReference:
        if (target.symbol_id.has_value()) {
            if (const auto symbol = snapshot.resolve_result.symbol_table.get(*target.symbol_id);
                symbol.has_value()) {
                return symbol_payload(snapshot, target, symbol->get());
            }
        }
        if (target.kind == HoverTargetKind::TypeReference && target.role == "builtin type") {
            auto payload = base_payload(target, "builtin type");
            payload.ahfl_signature = target.local_name;
            payload.canonical_name = target.local_name;
            return payload;
        }
        return std::nullopt;
    case HoverTargetKind::StructField:
        return struct_field_payload(snapshot, target);
    case HoverTargetKind::EnumVariant:
        return enum_variant_payload(snapshot, target);
    case HoverTargetKind::CapabilityParam:
        return callable_param_payload(snapshot, target, false);
    case HoverTargetKind::PredicateParam:
        return callable_param_payload(snapshot, target, true);
    case HoverTargetKind::AgentSchemaLabel:
        return schema_payload(snapshot, target, false);
    case HoverTargetKind::WorkflowSchemaLabel:
        return schema_payload(snapshot, target, true);
    case HoverTargetKind::AgentState:
        return agent_state_payload(
            snapshot, target, target.role.empty() ? "agent state" : target.role);
    case HoverTargetKind::AgentTransition:
        return agent_state_payload(snapshot, target, "agent transition");
    case HoverTargetKind::FlowState:
        return agent_state_payload(snapshot, target, "flow state");
    case HoverTargetKind::WorkflowNode:
        return workflow_node_payload(snapshot, target, false);
    case HoverTargetKind::WorkflowDependency:
        return workflow_node_payload(snapshot, target, true);
    case HoverTargetKind::Expression:
        return expression_payload(snapshot, target, false);
    case HoverTargetKind::MemberAccess:
        return expression_payload(snapshot, target, true);
    case HoverTargetKind::LocalBinding:
        if (auto payload = expression_payload(snapshot, target, false); payload.has_value()) {
            payload->summary = "local binding";
            return payload;
        }
        return base_payload(target, "local binding");
    case HoverTargetKind::Diagnostic:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<HoverPayload> diagnostic_payload_at(const LspAnalysisSnapshot &snapshot,
                                                                const LspSourceSnapshot &source,
                                                                Position position) {
    if (source.source == nullptr) {
        return std::nullopt;
    }
    for (const auto &diagnostic : snapshot.diagnostics_for_uri(source.uri)) {
        if (!position_in_range(position, diagnostic.range)) {
            continue;
        }
        auto payload = HoverPayload{};
        payload.summary = "diagnostic";
        payload.diagnostics.push_back(diagnostic.message);
        payload.token_range = SourceRange{
            .begin_offset = offset_at(*source.source, diagnostic.range.start),
            .end_offset = offset_at(*source.source, diagnostic.range.end),
        };
        payload.source_label = source.source->display_name;
        return payload;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<HoverPayload> fallback_expression_payload(
    const LspAnalysisSnapshot &snapshot, const LspSourceSnapshot &source, std::size_t offset) {
    const auto *typed = snapshot.typed_program();
    if (typed == nullptr || source.source == nullptr) {
        return std::nullopt;
    }
    const auto *expr = typed->find_expr_containing(offset, source.source_id);
    if (expr == nullptr || expr->type == nullptr) {
        return std::nullopt;
    }

    HoverPayload payload;
    payload.ahfl_signature = expr->type->describe();
    payload.summary = "expression";
    payload.token_range = expr->range;
    payload.source_label = source.source->display_name;
    if (!expr->semantic_name.empty()) {
        payload.facts.push_back(expr->semantic_name);
    }
    return payload;
}

} // namespace

std::optional<HoverPayload> HoverService::payload_at(const LspAnalysisSnapshot &snapshot,
                                                     const LspSourceSnapshot &source,
                                                     Position position) const {
    if (source.source == nullptr) {
        return std::nullopt;
    }
    const auto offset = offset_at(*source.source, position);

    const auto index_iter = snapshot.hover_indices.find(hover_index_key(source));
    if (index_iter != snapshot.hover_indices.end()) {
        if (const auto *target = index_iter->second.lookup(offset); target != nullptr) {
            if (auto payload = target_payload(snapshot, *target); payload.has_value()) {
                return payload;
            }
        }
    }

    if (auto diagnostic = diagnostic_payload_at(snapshot, source, position);
        diagnostic.has_value()) {
        return diagnostic;
    }

    return fallback_expression_payload(snapshot, source, offset);
}

std::optional<Hover> HoverService::hover_at(const LspAnalysisSnapshot &snapshot,
                                            const LspSourceSnapshot &source,
                                            Position position) const {
    auto payload = payload_at(snapshot, source, position);
    if (!payload.has_value() || source.source == nullptr) {
        return std::nullopt;
    }

    Hover hover;
    hover.contents = renderer_.render(*payload);
    if (range_size(payload->token_range) > 0) {
        hover.range = to_lsp_range(*source.source, payload->token_range);
    }
    return hover;
}

} // namespace ahfl::lsp
