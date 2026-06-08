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
#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl {

using internal::ValueContext;

void TypeCheckPass::index_program_declarations(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        switch (declaration->kind) {
        case ast::NodeKind::ConstDecl: {
            const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Consts, decl.name);
                symbol.has_value()) {
                const_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::TypeAliasDecl: {
            const auto &decl = static_cast<const ast::TypeAliasDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                type_alias_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::StructDecl: {
            const auto &decl = static_cast<const ast::StructDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                struct_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::EnumDecl: {
            const auto &decl = static_cast<const ast::EnumDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                enum_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::CapabilityDecl: {
            const auto &decl = static_cast<const ast::CapabilityDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Capabilities, decl.name);
                symbol.has_value()) {
                capability_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::PredicateDecl: {
            const auto &decl = static_cast<const ast::PredicateDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Predicates, decl.name);
                symbol.has_value()) {
                predicate_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::AgentDecl: {
            const auto &decl = static_cast<const ast::AgentDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Agents, decl.name);
                symbol.has_value()) {
                agent_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::WorkflowDecl: {
            const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Workflows, decl.name);
                symbol.has_value()) {
                workflow_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::Program:
        case ast::NodeKind::ModuleDecl:
        case ast::NodeKind::ImportDecl:
        case ast::NodeKind::ContractDecl:
        case ast::NodeKind::FlowDecl:
            break;
        }
    }
}

void TypeCheckPass::index_declarations() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            index_program_declarations(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    index_program_declarations(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::build_type_environment() {
    build_const_types();
    build_struct_types();
    build_enum_types();
    build_capability_types();
    build_predicate_types();
    build_agent_types();
    build_workflow_types();
}

void TypeCheckPass::build_const_types() {
    for (const auto &[id, decl] : const_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            if (!decl.get().type) {
                return;
            }

            result_.environment.const_types_.emplace(id, resolve_type(*decl.get().type));
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
            };

            std::unordered_set<std::string> seen_fields;
            for (const auto &field : decl.get().fields) {
                if (!seen_fields.insert(field->name).second) {
                    typecheck_error_here(error_codes::typecheck::DuplicateField,
                                         "duplicate struct field '" + field->name + "'",
                                         field->range);
                }

                info.fields.push_back(StructFieldInfo{
                    .name = field->name,
                    .type = resolve_type(*field->type),
                    .has_default = static_cast<bool>(field->default_value),
                    .declaration_range = field->range,
                });
            }

            result_.environment.index_struct(id, std::move(info));
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
            };

            std::unordered_set<std::string> seen_variants;
            for (const auto &variant : decl.get().variants) {
                if (!seen_variants.insert(variant->name).second) {
                    typecheck_error_here(error_codes::typecheck::DuplicateVariant,
                                         "duplicate enum variant '" + variant->name + "'",
                                         variant->range);
                }

                info.variants.push_back(EnumVariantInfo{
                    .name = variant->name,
                    .declaration_range = variant->range,
                });
            }

            result_.environment.index_enum(id, std::move(info));
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
            };

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = resolve_type(*param->type),
                    .declaration_range = param->range,
                });
            }

            info.return_type = resolve_type(*decl.get().return_type);
            result_.environment.capabilities_.emplace(id, std::move(info));
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

            result_.environment.predicates_.emplace(id, std::move(info));
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
            };

            if (info.input_type && info.input_type->kind != TypeKind::Struct) {
                error_here("agent input type must resolve to a struct type",
                           decl.get().input_type->range);
            }

            if (info.context_type && info.context_type->kind != TypeKind::Struct) {
                error_here("agent context type must resolve to a struct type",
                           decl.get().context_type->range);
            }

            if (info.output_type && info.output_type->kind != TypeKind::Struct) {
                error_here("agent output type must resolve to a struct type",
                           decl.get().output_type->range);
            }

            for (const auto &capability_name : decl.get().capabilities) {
                const auto capability_symbol =
                    find_local_here(SymbolNamespace::Capabilities, capability_name);
                if (!capability_symbol.has_value()) {
                    error_here("unknown capability '" + capability_name +
                                   "' in agent capability list",
                               decl.get().range);
                    continue;
                }

                info.capability_symbols.push_back(capability_symbol->get().id);
            }

            result_.environment.agents_.emplace(id, std::move(info));

            // Pre-compute agent context struct set for O(1) is_agent_context_struct queries.
            const auto &stored = result_.environment.agents().at(id);
            if (stored.context_type && stored.context_type->kind == TypeKind::Struct &&
                stored.context_type->nominal_symbol.has_value()) {
                result_.environment.mark_agent_context_struct(*stored.context_type->nominal_symbol);
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
            };

            if (info.input_type && info.input_type->kind != TypeKind::Struct) {
                error_here("workflow input type must resolve to a struct type",
                           decl.get().input_type->range);
            }

            if (info.output_type && info.output_type->kind != TypeKind::Struct) {
                error_here("workflow output type must resolve to a struct type",
                           decl.get().output_type->range);
            }

            result_.environment.workflows_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::check_const_initializers_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ConstDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
        const auto symbol = find_local_here(SymbolNamespace::Consts, decl.name);
        if (!symbol.has_value()) {
            continue;
        }

        const auto declared_type = result_.environment.get_const_type(symbol->get().id);
        if (!declared_type.has_value()) {
            continue;
        }

        const ValueContext context;
        auto value = check_const_expr(*decl.value, context, declared_type, "const initializer");
        (void)check_assignable(*value.typed_value.type,
                               declared_type->get(),
                               decl.value->range,
                               "const initializer");
    }
}

void TypeCheckPass::check_const_initializers() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_const_initializers_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_const_initializers_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_struct_defaults() {
    for (const auto &[id, decl] : struct_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto struct_info = result_.environment.get_struct(SymbolId{id});
            if (!struct_info.has_value()) {
                return;
            }

            const bool is_context_struct =
                result_.environment.is_agent_context_struct(SymbolId{id});
            for (std::size_t index = 0; index < decl.get().fields.size(); ++index) {
                const auto &field_decl = decl.get().fields[index];
                if (!field_decl->default_value) {
                    continue;
                }

                const auto &field_info = struct_info->get().fields[index];
                const ValueContext context;
                auto value = check_const_expr(*field_decl->default_value,
                                              context,
                                              std::cref(*field_info.type),
                                              "struct field default");
                if (is_context_struct) {
                    (void)check_exact_schema_boundary(*value.typed_value.type,
                                                      *field_info.type,
                                                      SchemaBoundaryKind::AgentContextDefault,
                                                      field_decl->default_value->range,
                                                      field_info.declaration_range);
                } else {
                    (void)check_assignable(*value.typed_value.type,
                                           *field_info.type,
                                           field_decl->default_value->range,
                                           "struct field default",
                                           field_info.declaration_range);
                }
            }
        });
    }
}

void TypeCheckPass::check_agent_context_defaults() {
    std::unordered_set<std::size_t> checked_contexts;
    for (const auto &[id, agent] : result_.environment.agents()) {
        (void)id;
        if (!agent.context_type || agent.context_type->kind != TypeKind::Struct ||
            !agent.context_type->nominal_symbol.has_value() ||
            !checked_contexts.insert(agent.context_type->nominal_symbol->value).second) {
            continue;
        }

        const auto context_struct =
            result_.environment.get_struct(*agent.context_type->nominal_symbol);
        if (!context_struct.has_value()) {
            continue;
        }

        with_symbol_context(context_struct->get().symbol, [&]() {
            for (const auto &field : context_struct->get().fields) {
                if (!field.has_default) {
                    typecheck_error_here(
                        error_codes::typecheck::MissingField,
                        "agent context field '" + field.name + "' must declare a default value",
                        field.declaration_range);
                }
            }
        });
    }
}

} // namespace ahfl
