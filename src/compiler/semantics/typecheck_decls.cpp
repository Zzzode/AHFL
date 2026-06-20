// Declaration-time semantic analysis: indexes the AST declarations into the
// per-pass maps, builds the canonical TypeEnvironment for every nominal kind,
// and validates declaration-level invariants (const initializers, struct
// defaults, agent context defaults).
//
// This file is one of multiple translation units that implement TypeCheckPass;
// it shares state and helpers with typecheck.cpp via typecheck_internal.hpp.

#include "ahfl/compiler/semantics/typecheck.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"

#include "compiler/semantics/typecheck_internal.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ahfl {

using internal::ValueContext;

MaybeCRef<Symbol> DeclarationIndexBuilder::find_local(SymbolNamespace name_space,
                                                      std::string_view name) const {
    if (!state_->current_module_name.empty()) {
        return session_->resolve_result.symbol_table.find_local(
            name_space, name, state_->current_module_name);
    }

    return session_->resolve_result.symbol_table.find_local(name_space, name);
}

void DeclarationIndexBuilder::index_program_declarations(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        switch (declaration->kind) {
        case ast::NodeKind::ModuleDecl: {
            const auto &decl = static_cast<const ast::ModuleDecl &>(*declaration);
            hir_->append_declaration(TypedDecl{
                .kind = declaration->kind,
                .symbol = {},
                .range = declaration->range,
                .source_id = state_->current_source_id,
                .associated_agent_symbol = std::nullopt,
                .type = nullptr,
                .payload =
                    ModuleDeclInfo{
                        .name = decl.name ? decl.name->spelling() : std::string{},
                        .declaration_range = declaration->range,
                    },
            });
            break;
        }
        case ast::NodeKind::ImportDecl: {
            const auto &decl = static_cast<const ast::ImportDecl &>(*declaration);
            hir_->append_declaration(TypedDecl{
                .kind = declaration->kind,
                .symbol = {},
                .range = declaration->range,
                .source_id = state_->current_source_id,
                .associated_agent_symbol = std::nullopt,
                .type = nullptr,
                .payload =
                    ImportDeclInfo{
                        .target_module = decl.path ? decl.path->spelling() : std::string{},
                        .alias = decl.alias,
                        .declaration_range = declaration->range,
                    },
            });
            break;
        }
        case ast::NodeKind::ConstDecl: {
            const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Consts, decl.name);
                symbol.has_value()) {
                index_->const_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::TypeAliasDecl: {
            const auto &decl = static_cast<const ast::TypeAliasDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                index_->type_alias_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::StructDecl: {
            const auto &decl = static_cast<const ast::StructDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                index_->struct_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::EnumDecl: {
            const auto &decl = static_cast<const ast::EnumDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                index_->enum_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::CapabilityDecl: {
            const auto &decl = static_cast<const ast::CapabilityDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Capabilities, decl.name);
                symbol.has_value()) {
                index_->capability_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::PredicateDecl: {
            const auto &decl = static_cast<const ast::PredicateDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Predicates, decl.name);
                symbol.has_value()) {
                index_->predicate_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::AgentDecl: {
            const auto &decl = static_cast<const ast::AgentDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Agents, decl.name);
                symbol.has_value()) {
                index_->agent_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::WorkflowDecl: {
            const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Workflows, decl.name);
                symbol.has_value()) {
                index_->workflow_decls.emplace(symbol->get().id.value, std::cref(decl));
                hir_->append_declaration(TypedDecl{
                    .kind = declaration->kind,
                    .symbol = symbol->get().id,
                    .range = declaration->range,
                    .source_id = state_->current_source_id,
                    .associated_agent_symbol = std::nullopt,
                    .type = nullptr,
                    .payload = {},
                });
            }
            break;
        }
        case ast::NodeKind::Program:
        case ast::NodeKind::ContractDecl:
        case ast::NodeKind::FlowDecl:
            break;
        }
    }
}

void DeclarationIndexBuilder::run() {
    if (session_->graph != nullptr) {
        for (const auto &source : session_->graph->sources) {
            state_->enter_source(source);
            index_program_declarations(
                require(source.program.get(), "source graph program must exist before typecheck"));
            state_->leave_source();
        }
        return;
    }

    index_program_declarations(require(session_->program, "typecheck program must exist"));
}

EnvironmentBuildResult EnvironmentBuilder::run() {
    EnvironmentBuildResult build_result;

    struct EnvironmentScope {
        TypeCheckPass &driver;
        TypeEnvironment *previous;

        ~EnvironmentScope() {
            driver.environment_ = previous;
        }
    };

    EnvironmentScope scope{*driver_, driver_->environment_};
    driver_->environment_ = &build_result.environment;

    driver_->build_const_types();
    driver_->build_struct_types();
    driver_->build_enum_types();
    driver_->build_capability_types();
    driver_->build_predicate_types();
    driver_->build_agent_types();
    driver_->build_workflow_types();
    driver_->build_flow_types();
    driver_->build_contract_types();

    build_result.declaration_updates.reserve(driver_->result_.typed_program.declarations.size());
    for (std::size_t index = 0; index < driver_->result_.typed_program.declarations.size();
         ++index) {
        const auto &decl = driver_->result_.typed_program.declarations[index];
        DeclarationPayloadUpdate update{
            .declaration_index = index,
            .type = nullptr,
            .payload = {},
        };

        // Copy structural payloads out of TypeEnvironment so TypedProgram can
        // be consumed without borrowing environment map storage.
        switch (decl.kind) {
        case ast::NodeKind::ConstDecl:
            if (const auto type = driver_->environment().get_const_type(decl.symbol);
                type.has_value()) {
                update.type = type->get().clone();
            }
            if (const auto ast_decl_iter = driver_->const_decls_.find(decl.symbol.value);
                ast_decl_iter != driver_->const_decls_.end()) {
                const auto &ast_decl = ast_decl_iter->second.get();
                const auto symbol = driver_->symbol_of(decl.symbol);
                update.payload = ConstDeclInfo{
                    .canonical_name = symbol.has_value() ? symbol->get().canonical_name
                                                         : std::string(ast_decl.name),
                    .local_name = ast_decl.name,
                    .type = update.type,
                    .type_range = ast_decl.type ? ast_decl.type->range : SourceRange{},
                    .value_range = ast_decl.value ? ast_decl.value->range : SourceRange{},
                    .declaration_range = ast_decl.range,
                };
            }
            break;
        case ast::NodeKind::TypeAliasDecl:
            if (const auto ast_decl = driver_->alias_decl_of(decl.symbol); ast_decl.has_value()) {
                const auto &alias = ast_decl->get();
                const auto symbol = driver_->symbol_of(decl.symbol);
                update.payload = TypeAliasDeclInfo{
                    .symbol = decl.symbol,
                    .canonical_name =
                        symbol.has_value() ? symbol->get().canonical_name : std::string(alias.name),
                    .local_name = alias.name,
                    .aliased_type = alias.aliased_type ? driver_->resolve_type(*alias.aliased_type)
                                                       : driver_->make_error_type(),
                    .aliased_type_range =
                        alias.aliased_type ? alias.aliased_type->range : SourceRange{},
                    .declaration_range = alias.range,
                };
            }
            break;
        case ast::NodeKind::StructDecl:
            if (auto info = driver_->environment().get_struct(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::EnumDecl:
            if (auto info = driver_->environment().get_enum(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::CapabilityDecl:
            if (auto info = driver_->environment().get_capability(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::PredicateDecl:
            if (auto info = driver_->environment().get_predicate(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::AgentDecl:
            if (auto info = driver_->environment().get_agent(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::WorkflowDecl:
            if (auto info = driver_->environment().get_workflow(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        default:
            break;
        }

        if (update.type != nullptr || !std::holds_alternative<std::monostate>(update.payload)) {
            build_result.declaration_updates.push_back(std::move(update));
        }
    }

    return build_result;
}

void TypeCheckPass::build_const_types() {
    for (const auto &[id, decl] : const_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            if (!decl.get().type) {
                return;
            }

            environment().const_types_.emplace(id, resolve_type(*decl.get().type));
        });
    }
}

void TypeCheckPass::build_struct_types() {
    for (const auto &[id, decl] : struct_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            StructTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .fields = {},
                .declaration_range = decl.get().range,
                .field_index_ = {},
            };

            std::unordered_set<std::string> seen_fields;
            for (const auto &field : decl.get().fields) {
                if (!seen_fields.insert(field->name).second) {
                    typecheck_error_here(
                        error_codes::typecheck::DuplicateField,
                        messages::typecheck::DuplicateStructField.format_with(field->name),
                        field->range);
                }

                info.fields.push_back(StructFieldInfo{
                    .name = field->name,
                    .type = resolve_type(*field->type),
                    .has_default = static_cast<bool>(field->default_value),
                    .default_value_range =
                        field->default_value ? field->default_value->range : SourceRange{},
                    .declaration_range = field->range,
                });
            }

            environment().index_struct(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_enum_types() {
    for (const auto &[id, decl] : enum_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            EnumTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .variants = {},
                .declaration_range = decl.get().range,
                .variant_set_ = {},
            };

            std::unordered_set<std::string> seen_variants;
            for (const auto &variant : decl.get().variants) {
                if (!seen_variants.insert(variant->name).second) {
                    typecheck_error_here(
                        error_codes::typecheck::DuplicateVariant,
                        messages::typecheck::DuplicateEnumVariant.format_with(variant->name),
                        variant->range);
                }

                info.variants.push_back(EnumVariantInfo{
                    .name = variant->name,
                    .declaration_range = variant->range,
                });
            }

            environment().index_enum(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_capability_types() {
    for (const auto &[id, decl] : capability_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            CapabilityTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .params = {},
                .return_type = nullptr,
                .declaration_range = decl.get().range,
                .effect = {},
            };

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = resolve_type(*param->type),
                    .declaration_range = param->range,
                });
            }

            info.return_type = resolve_type(*decl.get().return_type);

            // Fill effect spec for T1.8 typed-store lowering.
            if (decl.get().effect) {
                const auto &eff = *decl.get().effect;
                info.effect.declared = true;
                info.effect.effect_kind = static_cast<int>(eff.effect_kind);
                info.effect.receipt_mode = static_cast<int>(eff.receipt_mode);
                info.effect.retry_mode = static_cast<int>(eff.retry_mode);
                info.effect.source_range = eff.range;
                if (eff.domain)
                    info.effect.domain = eff.domain->spelling();
                if (eff.idempotency_key)
                    info.effect.idempotency_key = eff.idempotency_key->spelling();
                if (eff.timeout)
                    info.effect.timeout = eff.timeout->spelling;
                if (eff.compensation)
                    info.effect.compensation = eff.compensation->spelling();
                info.effect.policies.reserve(eff.policies.size());
                for (const auto &policy : eff.policies)
                    info.effect.policies.push_back(policy->spelling());
            }

            environment().capabilities_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_predicate_types() {
    for (const auto &[id, decl] : predicate_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            PredicateTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .params = {},
                .declaration_range = decl.get().range,
            };

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = resolve_type(*param->type),
                    .declaration_range = param->range,
                });
            }

            environment().predicates_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_agent_types() {
    for (const auto &[id, decl] : agent_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            AgentTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .input_type = resolve_type(*decl.get().input_type),
                .context_type = resolve_type(*decl.get().context_type),
                .output_type = resolve_type(*decl.get().output_type),
                .capability_symbols = {},
                .declaration_range = decl.get().range,
                .input_type_range = decl.get().input_type->range,
                .context_type_range = decl.get().context_type->range,
                .output_type_range = decl.get().output_type->range,
                .states = {},
                .initial_state = {},
                .final_states = {},
                .transitions = {},
                .quota = {},
            };

            check_schema_boundary_decl_type(
                info.input_type, SchemaBoundaryKind::AgentInput, decl.get().input_type->range);
            check_schema_boundary_decl_type(info.context_type,
                                            SchemaBoundaryKind::AgentContextDefault,
                                            decl.get().context_type->range);
            check_schema_boundary_decl_type(
                info.output_type, SchemaBoundaryKind::AgentOutput, decl.get().output_type->range);

            for (const auto &capability_name : decl.get().capabilities) {
                const auto capability_symbol =
                    find_local_here(SymbolNamespace::Capabilities, capability_name);
                if (!capability_symbol.has_value()) {
                    typecheck_error_here(
                        error_codes::typecheck::UnknownCapability,
                        messages::typecheck::UnknownCapabilityInAgent.format_with(capability_name),
                        decl.get().range);
                    continue;
                }

                info.capability_symbols.push_back(capability_symbol->get().id);
            }

            // Fill structural payloads for T1.8 typed-store lowering.
            info.states = decl.get().states;
            info.initial_state = decl.get().initial_state;
            info.final_states = decl.get().final_states;

            info.transitions.reserve(decl.get().transitions.size());
            for (const auto &t : decl.get().transitions) {
                info.transitions.push_back(AgentTransitionInfo{
                    .from_state = t->from_state,
                    .to_state = t->to_state,
                });
            }

            if (decl.get().quota) {
                info.quota.reserve(decl.get().quota->items.size());
                for (const auto &item : decl.get().quota->items) {
                    std::string value;
                    if (item->integer_value)
                        value = item->integer_value->spelling;
                    else if (item->duration_value)
                        value = item->duration_value->spelling;
                    std::string name;
                    switch (item->kind) {
                    case ast::AgentQuotaItemKind::MaxToolCalls:
                        name = "max_tool_calls";
                        break;
                    case ast::AgentQuotaItemKind::MaxExecutionTime:
                        name = "max_execution_time";
                        break;
                    }
                    info.quota.push_back(
                        AgentQuotaInfo{.name = std::move(name), .value = std::move(value)});
                }
            }

            environment().agents_.emplace(id, std::move(info));

            // Pre-compute agent context struct set for O(1) is_agent_context_struct queries.
            const auto &stored = environment().agents().at(id);
            if (stored.context_type) {
                if (const auto *ctx = stored.context_type->get_if<types::StructT>();
                    ctx != nullptr && ctx->symbol.has_value()) {
                    environment().mark_agent_context_struct(*ctx->symbol);
                }
            }
        });
    }
}

void TypeCheckPass::build_workflow_types() {
    for (const auto &[id, decl] : workflow_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            WorkflowTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .input_type = resolve_type(*decl.get().input_type),
                .output_type = resolve_type(*decl.get().output_type),
                .declaration_range = decl.get().range,
                .input_type_range = decl.get().input_type->range,
                .output_type_range = decl.get().output_type->range,
                .nodes = {},
                .safety_ranges = {},
                .liveness_ranges = {},
                .return_value_range = {},
            };

            check_schema_boundary_decl_type(
                info.input_type, SchemaBoundaryKind::WorkflowInput, decl.get().input_type->range);
            check_schema_boundary_decl_type(info.output_type,
                                            SchemaBoundaryKind::WorkflowOutput,
                                            decl.get().output_type->range);

            // T1.8 Phase 2: populate node/temporal data.
            info.nodes.reserve(decl.get().nodes.size());
            for (const auto &node : decl.get().nodes) {
                WorkflowNodeInfo node_info{
                    .name = node->name,
                    .target_name = node->target->spelling(),
                    .after = node->after,
                    .source_range = node->range,
                    .input_expr_range = node->input->range,
                    .target_range = node->target->range,
                };
                const auto target =
                    find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
                if (target.has_value()) {
                    node_info.target_symbol = target->get().target;
                }
                info.nodes.push_back(std::move(node_info));
            }

            info.safety_ranges.reserve(decl.get().safety.size());
            for (const auto &formula : decl.get().safety) {
                info.safety_ranges.push_back(formula->range);
            }

            info.liveness_ranges.reserve(decl.get().liveness.size());
            for (const auto &formula : decl.get().liveness) {
                info.liveness_ranges.push_back(formula->range);
            }

            if (decl.get().return_value) {
                info.return_value_range = decl.get().return_value->range;
            }

            environment().workflows_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_flow_types() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            build_flow_types_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    build_flow_types_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::build_flow_types_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::FlowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::FlowDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::FlowTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        FlowTypeInfo info{
            .symbol = target->get().target,
            .target_name = decl.target->spelling(),
            .target_symbol = target->get().target,
            .target_range = decl.target->range,
            .state_handlers = {},
            .declaration_range = decl.range,
        };

        for (const auto &handler : decl.state_handlers) {
            FlowStateHandlerInfo handler_info{
                .state_name = handler->state_name,
                .policy = {},
                .body_range = handler->body->range,
                .source_range = handler->range,
            };

            if (handler->policy) {
                handler_info.policy.reserve(handler->policy->items.size());
                for (const auto &item : handler->policy->items) {
                    StatePolicyItemInfo policy_item{};
                    switch (item->kind) {
                    case ast::StatePolicyItemKind::Retry:
                        policy_item.kind = StatePolicyKind::Retry;
                        if (item->retry_limit) {
                            policy_item.value = item->retry_limit->spelling;
                        }
                        break;
                    case ast::StatePolicyItemKind::RetryOn:
                        policy_item.kind = StatePolicyKind::RetryOn;
                        for (const auto &retry_target : item->retry_on) {
                            policy_item.retry_on_targets.push_back(retry_target->spelling());
                        }
                        break;
                    case ast::StatePolicyItemKind::Timeout:
                        policy_item.kind = StatePolicyKind::Timeout;
                        if (item->timeout) {
                            policy_item.value = item->timeout->spelling;
                        }
                        break;
                    }
                    handler_info.policy.push_back(std::move(policy_item));
                }
            }

            info.state_handlers.push_back(std::move(handler_info));
        }

        environment().flows_.emplace(target->get().target.value, std::move(info));
    }
}

void TypeCheckPass::build_contract_types() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            build_contract_types_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    build_contract_types_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::build_contract_types_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ContractDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::ContractTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        ContractTypeInfo info{
            .symbol = target->get().target,
            .target_name = decl.target->spelling(),
            .target_symbol = target->get().target,
            .target_range = decl.target->range,
            .clauses = {},
            .declaration_range = decl.range,
        };

        for (const auto &clause : decl.clauses) {
            const bool is_temporal = (!clause->expr && static_cast<bool>(clause->temporal_expr));
            SourceRange expr_range;
            if (is_temporal) {
                expr_range = clause->temporal_expr->range;
            } else if (clause->expr) {
                expr_range = clause->expr->range;
            }
            ContractClauseInfo clause_info{
                .clause_kind = static_cast<int>(clause->kind),
                .is_temporal = is_temporal,
                .expr_range = expr_range,
                .source_range = clause->range,
            };
            info.clauses.push_back(std::move(clause_info));
        }

        environment().contracts_.emplace(target->get().target.value, std::move(info));
    }
}

void ConstSema::check_const_initializers_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ConstDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
        const auto symbol = driver_->find_local_here(SymbolNamespace::Consts, decl.name);
        if (!symbol.has_value()) {
            continue;
        }

        (void)ensure_const_value(symbol->get().id,
                                 decl.value != nullptr ? decl.value->range : decl.range);
    }
}

void ConstSema::check_const_initializers() {
    if (driver_->graph_ != nullptr) {
        for (const auto &source : driver_->graph_->sources) {
            driver_->enter_source(source);
            check_const_initializers_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            driver_->leave_source();
        }
        return;
    }

    check_const_initializers_in_program(require(driver_->program_, "typecheck program must exist"));
}

void ConstSema::check_struct_defaults() {
    for (const auto &[id, decl] : driver_->struct_decls_) {
        driver_->with_symbol_context(SymbolId{id}, [&]() {
            const auto struct_info = driver_->environment().get_struct(SymbolId{id});
            if (!struct_info.has_value()) {
                return;
            }

            const bool is_context_struct =
                driver_->environment().is_agent_context_struct(SymbolId{id});
            for (std::size_t index = 0; index < decl.get().fields.size(); ++index) {
                const auto &field_decl = decl.get().fields[index];
                if (!field_decl->default_value) {
                    continue;
                }

                const auto &field_info = struct_info->get().fields[index];
                const auto default_policy = classify_const_struct_default_validation(
                    *field_info.type, field_info.declaration_range, is_context_struct);
                const ValueContext context;
                auto value = check_const_expr(*field_decl->default_value,
                                              context,
                                              std::cref(*field_info.type),
                                              default_policy.context_label);
                ConstTypeRelationValidator const_relations{
                    driver_->relations_,
                    ConstDiagnosticEmitter{
                        driver_->result_.diagnostics,
                        driver_->current_source_ != nullptr ? &driver_->current_source_->source
                                                            : nullptr,
                    },
                };
                (void)const_relations.check_struct_default(*value.checked_expr.type,
                                                           *field_info.type,
                                                           field_decl->default_value->range,
                                                           default_policy);
            }
        });
    }
}

void ConstSema::check_agent_context_defaults() {
    std::unordered_set<std::size_t> checked_contexts;
    for (const auto &[id, agent] : driver_->environment().agents()) {
        (void)id;
        if (!agent.context_type) {
            continue;
        }
        const auto *ctx = agent.context_type->get_if<types::StructT>();
        if (ctx == nullptr || !ctx->symbol.has_value() ||
            !checked_contexts.insert(ctx->symbol->value).second) {
            continue;
        }

        const auto context_struct = driver_->environment().get_struct(*ctx->symbol);
        if (!context_struct.has_value()) {
            continue;
        }

        driver_->with_symbol_context(context_struct->get().symbol, [&]() {
            for (const auto &field : context_struct->get().fields) {
                if (!field.has_default) {
                    driver_->typecheck_error_here(
                        error_codes::typecheck::MissingField,
                        messages::typecheck::MissingAgentContextDefault.format_with(field.name),
                        field.declaration_range);
                }
            }
        });
    }
}

} // namespace ahfl
