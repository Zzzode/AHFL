#include "tooling/lsp/hover_service.hpp"

#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <utility>

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

[[nodiscard]] std::string type_description(TypePtr type) {
    return type != nullptr ? type->describe() : std::string{"<unknown>"};
}

[[nodiscard]] std::string source_text_for_range(const LspAnalysisSnapshot &snapshot,
                                                std::optional<SourceId> source_id,
                                                SourceRange range) {
    if (range.end_offset <= range.begin_offset) {
        return {};
    }

    const LspSourceSnapshot *source_snapshot = nullptr;
    if (source_id.has_value()) {
        source_snapshot = snapshot.source_for_id(*source_id);
    } else if (snapshot.sources.size() == 1) {
        source_snapshot = &snapshot.sources.front();
    }
    if (source_snapshot == nullptr || source_snapshot->source == nullptr) {
        return {};
    }

    const auto &content = source_snapshot->source->content;
    if (range.begin_offset >= content.size()) {
        return {};
    }
    const auto end = std::min(range.end_offset, content.size());
    if (end <= range.begin_offset) {
        return {};
    }
    return content.substr(range.begin_offset, end - range.begin_offset);
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
        // P3 (RFC §3.2.2): a trait's display category.
        return "trait";
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

// P2 (RFC §3.2.2): fn signature for hover. Generic type parameters are listed
// inline; the effect clause is rendered to the Hover "Effect" fact line (see below).
[[nodiscard]] std::string callable_signature(const FnTypeInfo &fn) {
    std::string head = "fn " + fn.canonical_name;
    if (!fn.type_param_names.empty()) {
        head += "<";
        for (std::size_t index = 0; index < fn.type_param_names.size(); ++index) {
            if (index != 0) {
                head += ", ";
            }
            head += fn.type_param_names[index];
        }
        head += ">";
    }
    head += "(" + params_signature(fn.params) + ") -> " + type_description(fn.return_type);
    return head;
}

[[nodiscard]] std::string symbol_display_name(const Symbol &symbol) {
    return symbol.local_name.empty() ? symbol.canonical_name : symbol.local_name;
}

// h-3 (Wave-17 Group C): resolve a capability symbol id back to a human-readable
// name. The symbol table lookup can fail (the id may point at a declaration
// registered but later invalidated) so we guard against empty and prefer the
// local_name over canonical_name for readability.
[[nodiscard]] std::string
capability_display_name(const ResolveResult &resolve_result, SymbolId id) {
    const auto maybe_symbol = resolve_result.symbol_table.get(id);
    if (!maybe_symbol.has_value()) {
        return {};
    }
    return symbol_display_name(*maybe_symbol);
}

// h-3 helper. Render the Hover "Effect" fact line for a declared effect clause.
// Pure/Nondet are one-word; Capability lists the resolved, inline-code-wrapped
// capability names that the declaration depends on.
[[nodiscard]] std::string describe_effect_fact_line(const FnEffectClauseInfo &effect,
                                                    const ResolveResult &resolve_result) {
    switch (static_cast<ast::EffectClauseKind>(effect.kind)) {
    case ast::EffectClauseKind::Pure:
        return "Pure";
    case ast::EffectClauseKind::Nondet:
        return "Nondet";
    case ast::EffectClauseKind::Capability: {
        if (effect.capabilities.empty()) {
            return "Capability";
        }
        std::string line = "Capability";
        const char *separator = ": ";
        for (std::size_t index = 0; index < effect.capabilities.size(); ++index) {
            const auto name = capability_display_name(resolve_result, effect.capabilities[index]);
            if (name.empty()) {
                continue;
            }
            line += separator;
            line += "`" + name + "`";
            separator = ", ";
        }
        return line;
    }
    }
    return "Capability (unknown)";
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
    payload.headline = std::move(summary);
    payload.token_range = target.token_range;
    payload.source_label = target.source_label;
    return payload;
}

void add_symbol_identity(HoverPayload &payload, const Symbol &symbol) {
    payload.canonical_name = symbol.canonical_name;
    payload.module_name = symbol.module_name;
}

void add_primary_fact(HoverPayload &payload, std::string label, std::string value) {
    add_hover_fact(payload, HoverFactImportance::Primary, std::move(label), std::move(value));
}

void add_secondary_fact(HoverPayload &payload, std::string label, std::string value) {
    add_hover_fact(payload, HoverFactImportance::Secondary, std::move(label), std::move(value));
}

[[nodiscard]] std::string inline_code(std::string_view value) {
    return "`" + std::string(value) + "`";
}

[[nodiscard]] bool contains_name(const std::vector<std::string> &values, std::string_view name) {
    return std::any_of(
        values.begin(), values.end(), [name](const std::string &value) { return value == name; });
}

[[nodiscard]] std::string state_fact_value(const AgentTypeInfo &agent, std::string_view state) {
    std::string value = inline_code(state);
    if (state == agent.initial_state) {
        value += " initial";
    }
    if (contains_name(agent.final_states, state)) {
        value += " final";
    }
    return value;
}

void add_transition_endpoint_facts(HoverPayload &payload, std::string_view transition) {
    constexpr std::string_view kArrow = " -> ";
    const auto arrow = transition.find(kArrow);
    if (arrow == std::string_view::npos) {
        return;
    }
    add_primary_fact(payload, "From", inline_code(transition.substr(0, arrow)));
    add_primary_fact(payload, "To", inline_code(transition.substr(arrow + kArrow.size())));
}

void add_state_facts(HoverPayload &payload, const AgentTypeInfo &agent) {
    for (const auto &state : agent.states) {
        add_primary_fact(payload, "", state_fact_value(agent, state));
    }
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
        payload.signature = "struct " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_struct(symbol.id); info.has_value()) {
                add_primary_fact(payload, "Fields", std::to_string(info->get().fields.size()));
            }
        }
        break;
    }
    case SymbolKind::Enum: {
        payload.signature = "enum " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_enum(symbol.id); info.has_value()) {
                add_primary_fact(payload, "Variants", std::to_string(info->get().variants.size()));
            }
        }
        break;
    }
    case SymbolKind::TypeAlias: {
        const auto *alias = payload_for_symbol<TypeAliasDeclInfo>(typed, symbol.id);
        payload.signature = "type " + symbol.canonical_name;
        if (alias != nullptr) {
            payload.signature += " = " + type_description(alias->aliased_type);
            const auto declared_spelling =
                source_text_for_range(snapshot, symbol.source_id, alias->aliased_type_range);
            if (!declared_spelling.empty()) {
                add_primary_fact(payload, "Declared as", inline_code(declared_spelling));
            }
        }
        break;
    }
    case SymbolKind::Const: {
        payload.signature = "const " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto type = environment->get_const_type(symbol.id); type.has_value()) {
                payload.signature += ": " + type_description(&type->get());
            }
        }
        break;
    }
    case SymbolKind::Capability: {
        if (environment != nullptr) {
            if (const auto info = environment->get_capability(symbol.id); info.has_value()) {
                payload.signature = callable_signature(info->get());
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
                        add_primary_fact(payload, "Effect", line);
                    }
                }
            }
        }
        if (payload.signature.empty()) {
            payload.signature = "capability " + symbol.canonical_name;
        }
        break;
    }
    case SymbolKind::Predicate: {
        if (environment != nullptr) {
            if (const auto info = environment->get_predicate(symbol.id); info.has_value()) {
                payload.signature = callable_signature(info->get());
            }
        }
        if (payload.signature.empty()) {
            payload.signature = "predicate " + symbol.canonical_name;
        }
        break;
    }
    case SymbolKind::Agent: {
        payload.signature = "agent " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_agent(symbol.id); info.has_value()) {
                add_primary_fact(
                    payload, "Input", inline_code(type_description(info->get().input_type)));
                add_primary_fact(
                    payload, "Context", inline_code(type_description(info->get().context_type)));
                add_primary_fact(
                    payload, "Output", inline_code(type_description(info->get().output_type)));
            }
        }
        break;
    }
    case SymbolKind::Workflow: {
        payload.signature = "workflow " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_workflow(symbol.id); info.has_value()) {
                add_primary_fact(
                    payload, "Input", inline_code(type_description(info->get().input_type)));
                add_primary_fact(
                    payload, "Output", inline_code(type_description(info->get().output_type)));
                add_primary_fact(payload, "Nodes", std::to_string(info->get().nodes.size()));
            }
        }
        break;
    }
    case SymbolKind::Function: {
        // P2 (RFC §3.2.2): render the fn signature, surfacing the resolved
        // param/return types and the declared effect clause.
        //
        // h-3 (Wave-17 Group C): when the effect clause is Capability with a
        // non-empty resolved symbol list, the "Effect" fact lists each
        // capability by (local) name rather than just the keyword.
        if (environment != nullptr) {
            if (const auto info = environment->get_fn(symbol.id); info.has_value()) {
                payload.signature = callable_signature(info->get());
                add_primary_fact(
                    payload,
                    "Effect",
                    describe_effect_fact_line(info->get().effect, snapshot.resolve_result));
            }
        }
        if (payload.signature.empty()) {
            payload.signature = "fn " + symbol.canonical_name;
        }
        break;
    }
    case SymbolKind::Trait: {
        // P3 (RFC §3.2.2 / type-system §1.3): trait hover surfaces the trait
        // signature + method/assoc-type counts.
        //
        // h-3 (Wave-17 Group C): additionally surface the super-trait list and
        // the trait-level where-clause bounds so a developer hovering on a
        // trait reference sees the complete contract without navigating to
        // the declaration.
        payload.signature = "trait " + symbol.canonical_name;
        if (environment != nullptr) {
            if (const auto info = environment->get_trait(symbol.id); info.has_value()) {
                const auto &trait = info->get();
                add_primary_fact(payload, "Methods", std::to_string(trait.methods.size()));
                add_primary_fact(payload,
                                 "Associated types",
                                 std::to_string(trait.assoc_types.size()));
                if (!trait.super_traits.empty()) {
                    std::string joined;
                    const char *sep = "";
                    for (const auto super_id : trait.super_traits) {
                        const auto name = capability_display_name(snapshot.resolve_result, super_id);
                        if (name.empty()) {
                            continue;
                        }
                        joined += sep;
                        joined += "`" + name + "`";
                        sep = ", ";
                    }
                    if (!joined.empty()) {
                        add_primary_fact(payload, "Super traits", joined);
                    }
                }
                if (!trait.where_clause.bounds.empty()) {
                    for (const auto &bound : trait.where_clause.bounds) {
                        std::string bound_line = bound.subject_name + ": ";
                        const char *sep = "";
                        for (const auto &trait_name : bound.trait_names) {
                            bound_line += sep;
                            bound_line += "`" + trait_name + "`";
                            sep = " + ";
                        }
                        add_primary_fact(payload, "Bound", bound_line);
                    }
                }
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
    const auto owner_symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
    const std::string owner_name =
        owner_symbol.has_value() ? symbol_display_name(owner_symbol->get()) : std::string{};

    if (!workflow && target.role == "agent property") {
        const auto info = environment.get_agent(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }

        auto payload = base_payload(target, "agent property");
        if (target.local_name == "states") {
            payload.headline = "Defines " + std::to_string(info->get().states.size()) +
                               " states for " + inline_code(owner_name);
            add_state_facts(payload, info->get());
        } else if (target.local_name == "initial") {
            payload.headline = "Initial state for " + inline_code(owner_name);
            if (!info->get().initial_state.empty()) {
                add_primary_fact(payload, "", inline_code(info->get().initial_state));
            }
        } else if (target.local_name == "final") {
            payload.headline = "Final states for " + inline_code(owner_name);
            for (const auto &state : info->get().final_states) {
                add_primary_fact(payload, "", inline_code(state));
            }
        } else if (target.local_name == "capabilities") {
            payload.headline = "Capabilities available to " + inline_code(owner_name);
            add_primary_fact(
                payload, "Count", std::to_string(info->get().capability_symbols.size()));
        }
        payload.signature = target.local_name + ": " + target.declared_spelling;
        if (owner_symbol.has_value()) {
            payload.canonical_name = owner_symbol->get().canonical_name + "." + target.local_name;
            payload.module_name = owner_symbol->get().module_name;
        }
        return payload;
    }

    auto payload = base_payload(target, workflow ? "workflow schema field" : "agent schema field");
    if (target.role == "path root") {
        payload.headline = workflow ? "Workflow path root" : "Agent path root";
    }

    const Type *type = nullptr;
    if (workflow) {
        const auto info = environment.get_workflow(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }
        if (target.local_name == "input") {
            type = info->get().input_type;
            if (target.role != "path root") {
                payload.headline = "Input schema for " + inline_code(owner_name);
            }
        } else if (target.local_name == "output") {
            type = info->get().output_type;
            if (target.role != "path root") {
                payload.headline = "Output schema for " + inline_code(owner_name);
            }
        }
    } else {
        const auto info = environment.get_agent(*target.owner_symbol_id);
        if (!info.has_value()) {
            return std::nullopt;
        }
        if (target.local_name == "input") {
            type = info->get().input_type;
            if (target.role != "path root") {
                payload.headline = "Input schema for " + inline_code(owner_name);
            }
        } else if (target.local_name == "context") {
            type = info->get().context_type;
            if (target.role != "path root") {
                payload.headline = "Context schema for " + inline_code(owner_name);
            }
        } else if (target.local_name == "output") {
            type = info->get().output_type;
            if (target.role != "path root") {
                payload.headline = "Output schema for " + inline_code(owner_name);
            }
        }
    }

    payload.signature = target.local_name + ": " + type_description(type);
    if (!target.declared_spelling.empty() && target.declared_spelling != type_description(type)) {
        add_primary_fact(payload, "Declared as", inline_code(target.declared_spelling));
    }
    if (owner_symbol.has_value()) {
        payload.canonical_name = owner_symbol->get().canonical_name + "." + target.local_name;
        payload.module_name = owner_symbol->get().module_name;
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload> struct_field_payload(const LspAnalysisSnapshot &snapshot,
                                                               const HoverTarget &target) {
    if (!target.owner_symbol_id.has_value()) {
        auto payload = base_payload(target, "struct field");
        payload.signature = target.local_name;
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
    auto payload = base_payload(target, "Field of " + inline_code(info->get().canonical_name));
    payload.signature = field->get().name + ": " + type_description(field->get().type);
    payload.canonical_name = info->get().canonical_name + "." + field->get().name;
    if (field->get().has_default) {
        add_primary_fact(payload, "", "Has default value");
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
    auto payload = base_payload(target, "Variant of " + inline_code(info->get().canonical_name));
    payload.signature = info->get().canonical_name + "::" + target.local_name;
    payload.canonical_name = payload.signature;
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
        payload.signature = param.name + ": " + type_description(param.type);
        payload.canonical_name = callable.canonical_name + "." + param.name;
        add_primary_fact(payload, "Parameter", std::to_string(index + 1));
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
    payload.signature = "state " + target.local_name;
    const auto owner_symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
    const std::string owner_name = owner_symbol.has_value()
                                       ? symbol_display_name(owner_symbol->get())
                                       : info->get().canonical_name;

    const bool is_initial = target.local_name == info->get().initial_state;
    const bool is_final = contains_name(info->get().final_states, target.local_name);
    if (target.role == "goto target") {
        payload.headline = "Jumps to " + std::string(is_final ? "final " : "") + "state " +
                           inline_code(target.local_name);
    } else if (target.role == "flow state") {
        payload.headline = "Flow state handler for " + inline_code(owner_name);
    } else if (is_initial && is_final) {
        payload.headline = "Initial and final state of " + inline_code(owner_name);
    } else if (is_initial) {
        payload.headline = "Initial state of " + inline_code(owner_name);
    } else if (is_final) {
        payload.headline = "Final state of " + inline_code(owner_name);
    } else {
        payload.headline = "State of " + inline_code(owner_name);
    }

    if (target.local_name == info->get().initial_state) {
        add_primary_fact(payload, "", "Initial state");
    }
    if (is_final) {
        add_primary_fact(payload, "", "Final state");
    }
    for (const auto &transition : info->get().transitions) {
        if (transition.from_state == target.local_name) {
            add_primary_fact(payload, "Transition to", inline_code(transition.to_state));
        }
        if (transition.to_state == target.local_name) {
            add_primary_fact(payload, "Transition from", inline_code(transition.from_state));
        }
    }
    if (owner_symbol.has_value()) {
        payload.canonical_name = owner_symbol->get().canonical_name + "." + target.local_name;
        payload.module_name = owner_symbol->get().module_name;
    }
    return payload;
}

[[nodiscard]] std::optional<HoverPayload>
agent_transition_payload(const LspAnalysisSnapshot &snapshot, const HoverTarget &target) {
    if (snapshot.type_check_result == nullptr || !target.owner_symbol_id.has_value()) {
        return std::nullopt;
    }
    const auto info = snapshot.type_check_result->environment.get_agent(*target.owner_symbol_id);
    if (!info.has_value()) {
        return std::nullopt;
    }

    auto payload = base_payload(target, "agent transition");
    payload.signature = "transition " + target.declared_spelling;
    const auto owner_symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
    const std::string owner_name = owner_symbol.has_value()
                                       ? symbol_display_name(owner_symbol->get())
                                       : info->get().canonical_name;
    payload.headline = "Transition of " + inline_code(owner_name);
    add_transition_endpoint_facts(payload, target.declared_spelling);
    if (owner_symbol.has_value()) {
        payload.canonical_name =
            owner_symbol->get().canonical_name + ".transition." + target.declared_spelling;
        payload.module_name = owner_symbol->get().module_name;
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
        payload.signature = "node " + node.name + ": " + node.target_name;
        if (!node.after.empty()) {
            std::string after;
            for (const auto &name : node.after) {
                if (!after.empty()) {
                    after += ", ";
                }
                after += inline_code(name);
            }
            add_primary_fact(payload, "After", after);
        }
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(node.target_symbol);
            symbol.has_value()) {
            payload.canonical_name = symbol->get().canonical_name;
            add_primary_fact(payload, "Agent", inline_code(symbol_display_name(symbol->get())));
        }
        return payload;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<HoverPayload>
workflow_temporal_clause_payload(const LspAnalysisSnapshot &snapshot, const HoverTarget &target) {
    auto payload = base_payload(target,
                                target.local_name == "liveness" ? "workflow liveness property"
                                                                : "workflow safety property");
    payload.signature = target.local_name + ": " + target.declared_spelling;

    if (target.owner_symbol_id.has_value()) {
        if (const auto symbol = snapshot.resolve_result.symbol_table.get(*target.owner_symbol_id);
            symbol.has_value()) {
            payload.canonical_name = symbol->get().canonical_name + "." + target.local_name;
            payload.module_name = symbol->get().module_name;
            add_primary_fact(payload, "Workflow", inline_code(symbol_display_name(symbol->get())));
        }
    }

    if (target.local_name == "liveness") {
        add_primary_fact(payload, "", "Eventual progress obligation");
    } else {
        add_primary_fact(payload, "", "Safety obligation");
    }
    return payload;
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
        payload.signature = target.local_name + ": " + expr.type->describe();
    } else if (!target.local_name.empty()) {
        payload.signature = target.local_name + ": " + expr.type->describe();
    } else {
        payload.signature = expr.type->describe();
    }
    if (!expr.semantic_name.empty()) {
        add_secondary_fact(payload, "", expr.semantic_name);
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
        payload.signature = "module " + target.role;
        payload.canonical_name = target.role;
        return payload;
    }
    case HoverTargetKind::ImportPath: {
        auto payload = base_payload(target, "import path");
        payload.signature = "import " + target.role;
        payload.canonical_name = target.role;
        return payload;
    }
    case HoverTargetKind::ImportAlias: {
        auto payload = base_payload(target, "import alias");
        payload.signature = "import " + target.role + " as " + target.local_name;
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
            payload.signature = target.local_name;
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
    case HoverTargetKind::WorkflowTemporalClause:
        return workflow_temporal_clause_payload(snapshot, target);
    case HoverTargetKind::AgentState:
        return agent_state_payload(
            snapshot, target, target.role.empty() ? "agent state" : target.role);
    case HoverTargetKind::AgentTransition:
        return agent_transition_payload(snapshot, target);
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
            payload->headline = "local binding";
            return payload;
        }
        return base_payload(target, "local binding");
    case HoverTargetKind::StructLiteral: {
        auto payload = base_payload(target, "struct literal");
        if (!target.local_name.empty()) {
            payload.signature = "Creates a `" + target.local_name + "` struct";
        }
        // StructLiteral targets are registered via the shared add_named_range_target
        // helper, which stores the struct's SymbolId into `owner_symbol_id` (not
        // `symbol_id`) to match the convention used by StructField/EnumVariant.
        // Fall back to owner_symbol_id so the payload can resolve the struct info.
        const auto sid = target.symbol_id.has_value() ? target.symbol_id
                                                       : target.owner_symbol_id;
        if (sid.has_value() && snapshot.type_check_result != nullptr) {
            const auto info = snapshot.type_check_result->environment.get_struct(*sid);
            if (info.has_value()) {
                payload.canonical_name = info->get().canonical_name;
                if (!target.local_name.empty()) {
                    payload.signature = "Creates a `" + info->get().canonical_name + "` struct";
                }
                add_primary_fact(payload,
                                 "Fields",
                                 std::to_string(info->get().fields.size()) + " total");
                for (const auto &field : info->get().fields) {
                    add_primary_fact(payload,
                                     "`" + field.name + "`",
                                     inline_code(type_description(field.type)) +
                                         (field.has_default ? " (default)" : ""));
                }
            }
        }
        return payload;
    }
    case HoverTargetKind::Diagnostic: {
        // NOTE: Diagnostic hover-targets are registered by add_typed_targets
        // for every diagnostic produced by the pipeline (parse / resolve /
        // typecheck / lint).  The primary consumer of diagnostics in LSP is
        // `textDocument/publishDiagnostics`, but we also need a minimal
        // hover payload so the tie-break order contract can be verified via
        // end-to-end hover requests (the target-index lookup picks the
        // lowest-ordinal priority-0 target; if the winner returned a null
        // payload, the service would silently fall through to the expression
        // fallback, which *looks* like the wrong target won).
        auto payload = base_payload(target, "diagnostic");
        if (!target.local_name.empty()) {
            payload.signature = inline_code(target.local_name);
        }
        if (!target.role.empty()) {
            add_primary_fact(payload, "message", target.role);
        }
        return payload;
    }
    // ---------------------------------------------------------------------
    // Wave-20 Construct Hover / Wave-21 L1 Construct payload: 4 subclass
    // payloads. Index writes are performed in hover_index.cpp's
    // add_statement_targets / add_expr_targets dispatch.
    // ---------------------------------------------------------------------
    case HoverTargetKind::EnumLiteral: {
        auto payload = base_payload(target, "enum literal");
        // Convention: target.local_name carries "Enum::Variant" spelling
        // written by add_enum_literal_target; role is optional variant
        // sub-name (used when pretty-printing nested tuples / named fields).
        const auto display =
            !target.local_name.empty() ? target.local_name : target.role;
        if (!display.empty()) {
            payload.signature = "`" + display + "`";
            payload.headline = "enum variant";
        }
        if (target.owner_symbol_id.has_value() &&
            snapshot.type_check_result != nullptr) {
            const auto info = snapshot.type_check_result->environment.get_enum(
                *target.owner_symbol_id);
            if (info.has_value()) {
                payload.canonical_name = info->get().canonical_name;
                add_primary_fact(payload,
                                 "Variants",
                                 std::to_string(info->get().variants.size()) +
                                     " total");
                // Highlight the selected variant (lookup by local_name suffix
                // or by `role`).
                const auto needle = target.role.empty() ? display : target.role;
                for (const auto &v : info->get().variants) {
                    const bool matches =
                        !needle.empty() &&
                        (v.name == needle ||
                         (info->get().canonical_name + "::" + v.name) == needle ||
                         payload.canonical_name + "::" + v.name == needle);
                    const auto tag = matches ? ("`" + v.name + "` ◀")
                                             : ("`" + v.name + "`");
                    std::string payload_desc;
                    if (v.payload.empty()) {
                        payload_desc = "unit";
                    } else {
                        payload_desc = "(";
                        for (std::size_t i = 0; i < v.payload.size(); ++i) {
                            if (i > 0) payload_desc += ", ";
                            payload_desc += type_description(v.payload[i]);
                        }
                        payload_desc += ")";
                    }
                    add_primary_fact(payload, tag, payload_desc);
                }
            }
        }
        return payload;
    }
    case HoverTargetKind::ConstEval: {
        auto payload = base_payload(target, "const eval");
        // Convention: role = const symbol name (canonical or local);
        // local_name = computed value (ConstValue describe()).
        if (!target.role.empty()) payload.signature = "`" + target.role + "`";
        if (!target.local_name.empty()) {
            add_primary_fact(payload, "evaluates to", target.local_name);
        }
        if (target.symbol_id.has_value()) {
            if (const auto sym = snapshot.resolve_result.symbol_table.get(
                    *target.symbol_id);
                sym.has_value()) {
                payload.canonical_name = sym->get().canonical_name;
            }
            if (snapshot.type_check_result != nullptr) {
                const auto ct = snapshot.type_check_result->environment
                                    .get_const_type(*target.symbol_id);
                if (ct.has_value()) {
                    add_secondary_fact(
                        payload,
                        "type",
                        inline_code(type_description(&ct->get())));
                }
            }
        }
        return payload;
    }
    case HoverTargetKind::ContractInstantiation: {
        auto payload = base_payload(target, "contract instantiation");
        // Convention: role = contract name spelling (as written by user);
        // owner_symbol_id = contract declaration SymbolId.
        if (!target.role.empty()) {
            payload.signature = "contract `" + target.role + "`";
        }
        if (target.owner_symbol_id.has_value() &&
            snapshot.type_check_result != nullptr) {
            const auto info =
                snapshot.type_check_result->environment.get_contract(
                    *target.owner_symbol_id);
            if (info.has_value()) {
                payload.canonical_name = info->get().target_name;
                add_primary_fact(
                    payload,
                    "Clauses",
                    std::to_string(info->get().clauses.size()));
            }
        }
        if (target.symbol_id.has_value()) {
            if (const auto sym = snapshot.resolve_result.symbol_table.get(
                    *target.symbol_id);
                sym.has_value() && !target.role.empty()) {
                payload.canonical_name = sym->get().canonical_name;
            }
        }
        return payload;
    }
    case HoverTargetKind::CapabilityInstantiation: {
        auto payload = base_payload(target, "capability instantiation");
        // Convention: role = capability name spelling;
        // owner_symbol_id = capability declaration SymbolId.
        if (!target.role.empty()) {
            payload.signature = "capability `" + target.role + "`";
        }
        if (target.owner_symbol_id.has_value() &&
            snapshot.type_check_result != nullptr) {
            const auto info =
                snapshot.type_check_result->environment.get_capability(
                    *target.owner_symbol_id);
            if (info.has_value()) {
                payload.canonical_name = info->get().canonical_name;
                add_primary_fact(
                    payload,
                    "Parameters",
                    std::to_string(info->get().params.size()));
                for (const auto &p : info->get().params) {
                    add_primary_fact(
                        payload, "`" + p.name + "`", type_description(p.type));
                }
                if (info->get().return_type != nullptr) {
                    add_secondary_fact(
                        payload,
                        "return",
                        inline_code(
                            type_description(info->get().return_type)));
                }
            }
        }
        return payload;
    }
    case HoverTargetKind::Sentinel_ForStaticAssert:
        // Sentinel is never instantiated as a target; defensively return
        // nullopt so the caller falls through to fallback_expression_payload.
        return std::nullopt;
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
    payload.signature = expr->type->describe();
    payload.headline = "expression";
    payload.token_range = expr->range;
    payload.source_label = source.source->display_name;
    if (!expr->semantic_name.empty()) {
        add_secondary_fact(payload, "", expr->semantic_name);
    }
    return payload;
}

} // namespace

HoverService::HoverService(HoverRenderOptions options) : options_(options) {}

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
    hover.contents_kind = options_.markup_kind;
    hover.contents = renderer_.render(*payload, options_);
    if (range_size(payload->token_range) > 0) {
        hover.range = to_lsp_range(*source.source, payload->token_range);
    }
    return hover;
}

} // namespace ahfl::lsp
