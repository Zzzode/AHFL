// Declaration-time semantic analysis: indexes the AST declarations into the
// per-pass maps, builds the canonical TypeEnvironment for every nominal kind,
// and validates declaration-level invariants (const initializers, struct
// defaults, agent context defaults).
//
// This file is one of multiple translation units that implement TypeCheckPass;
// it shares state and helpers with typecheck.cpp via typecheck_internal.hpp.

#include "ahfl/compiler/semantics/typecheck.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/builtin_hooks.hpp"

#include "compiler/semantics/typecheck_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ahfl {

using internal::ValueContext;

namespace {

[[nodiscard]] bool is_std_module(std::string_view module_name) noexcept {
    return module_name == "std" || module_name.starts_with("std::");
}

[[nodiscard]] bool matches_builtin_allow_pattern(std::string_view pattern,
                                                 std::string_view hook) noexcept {
    if (pattern == hook) {
        return true;
    }
    if (!pattern.ends_with('*')) {
        return false;
    }

    pattern.remove_suffix(1);
    return hook.starts_with(pattern);
}

// Build a WhereClauseInfo from an AST-owned WhereClauseSyntax. Per-task, this
// is pure plumbing: we copy out the raw bound spellings so downstream code can
// read them without an AST back-pointer, plus we retain the raw syntax pointer
// for diagnostic ranges. No symbol lookup or bound validation is performed.
WhereClauseInfo build_where_clause_info(const Owned<ast::WhereClauseSyntax> &syntax) {
    WhereClauseInfo info;
    info.syntax = syntax.get();
    if (!syntax) {
        return info;
    }
    info.bounds.reserve(syntax->constraints.size());
    for (const auto &constraint : syntax->constraints) {
        WhereBoundInfo bound_info;
        bound_info.source_range = constraint->range;
        if (constraint->subject && constraint->subject->is<ast::NamedType>()) {
            const auto &named = constraint->subject->as<ast::NamedType>();
            if (named.name) {
                bound_info.subject_name = named.name->spelling();
            }
        }
        if (constraint->is_predicate) {
            // Predicate constraint (e.g. `T::Eq(Int)`) → treat the trait name as
            // the single bound.
            if (!constraint->trait_name.empty()) {
                bound_info.trait_names.push_back(constraint->trait_name);
            }
        } else {
            // Bound-list constraint (e.g. `T: Eq + Show`) → each bound is a
            // NamedType whose qualified name spelling is the trait.
            bound_info.trait_names.reserve(constraint->bounds.size());
            for (const auto &bound : constraint->bounds) {
                if (!bound) continue;
                if (bound->is<ast::NamedType>()) {
                    const auto &named = bound->as<ast::NamedType>();
                    if (named.name) {
                        bound_info.trait_names.push_back(named.name->spelling());
                    }
                }
            }
        }
        info.bounds.push_back(std::move(bound_info));
    }
    return info;
}

} // namespace

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
        case ast::NodeKind::FnDecl: {
            // P2 (RFC §3.2.2): index the fn declaration under its Function
            // symbol id so build_fn_types can resolve its signature.
            const auto &decl = static_cast<const ast::FnDecl &>(*declaration);
            if (const auto symbol = find_local(SymbolNamespace::Functions, decl.name);
                symbol.has_value()) {
                index_->fn_decls.emplace(symbol->get().id.value, std::cref(decl));
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
        case ast::NodeKind::TraitDecl: {
            // C-2 (Wave-24): traits live in the Traits namespace. Check
            // Traits first, then fall back to Types (legacy path).
            const auto &decl = static_cast<const ast::TraitDecl &>(*declaration);
            auto symbol = find_local(SymbolNamespace::Traits, decl.name);
            if (!symbol.has_value()) {
                symbol = find_local(SymbolNamespace::Types, decl.name);
            }
            if (symbol.has_value() && symbol->get().kind == SymbolKind::Trait) {
                index_->trait_decls.emplace(symbol->get().id.value, std::cref(decl));
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
        case ast::NodeKind::ImplDecl: {
            // P3 (RFC §3.2.2 / type-system §1.4): impl blocks have no name;
            // index them under their source-order index. The TypedDecl record
            // uses an empty SymbolId so downstream consumers can recognise the
            // impl by kind alone.
            const auto &decl = static_cast<const ast::ImplDecl &>(*declaration);
            const auto impl_index = index_->impl_decls.size();
            index_->impl_decls.emplace(impl_index,
                                       DeclarationIndex::ImplDeclEntry{
                                           .decl = std::cref(decl),
                                           .source_id = state_->current_source_id,
                                       });
            hir_->append_declaration(TypedDecl{
                .kind = declaration->kind,
                .symbol = {},
                .range = declaration->range,
                .source_id = state_->current_source_id,
                .associated_agent_symbol = std::nullopt,
                .type = nullptr,
                .payload = {},
            });
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
    driver_->build_fn_types();
    driver_->build_flow_types();
    driver_->build_contract_types();
    driver_->build_trait_types();
    driver_->build_impl_types();

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

                // Collect type param names and push scope so the aliased type
                // can resolve type variables.
                std::vector<std::string> type_param_names;
                for (const auto &tp : alias.type_params) {
                    type_param_names.push_back(tp->name);
                }
                const auto *prev_alias_type_params = driver_->current_type_param_names_;
                driver_->current_type_param_names_ = &type_param_names;

                TypePtr aliased_type = alias.aliased_type
                                           ? driver_->resolve_type(*alias.aliased_type)
                                           : driver_->make_error_type();

                driver_->current_type_param_names_ = prev_alias_type_params;

                update.payload = TypeAliasDeclInfo{
                    .symbol = decl.symbol,
                    .canonical_name =
                        symbol.has_value() ? symbol->get().canonical_name : std::string(alias.name),
                    .local_name = alias.name,
                    .type_param_names = std::move(type_param_names),
                    .aliased_type = aliased_type,
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
        case ast::NodeKind::FnDecl:
            // P2 (RFC §3.2.2): copy FnTypeInfo out of TypeEnvironment so the
            // typed program carries the resolved fn signature for downstream
            // passes (LSP hover, lowering, future monomorphization).
            if (auto info = driver_->environment().get_fn(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::TraitDecl:
            // P3 (RFC §3.2.2 / type-system §1.3): copy TraitTypeInfo out of
            // TypeEnvironment so downstream passes (LSP hover, dump) read it
            // from the typed program without borrowing the env map storage.
            if (auto info = driver_->environment().get_trait(decl.symbol); info.has_value()) {
                update.payload = info->get();
            }
            break;
        case ast::NodeKind::ImplDecl: {
            // P3 (RFC §3.2.2 / type-system §1.4): copy ImplTypeInfo. The
            // impl is keyed by its source-order index; the typed declaration
            // was appended in source order, so its position among impl-kind
            // decls equals its impl index. Walk the typed program to find it.
            std::size_t impl_index = 0;
            for (std::size_t earlier = 0; earlier <= index; ++earlier) {
                if (driver_->result_.typed_program.declarations[earlier].kind !=
                    ast::NodeKind::ImplDecl) {
                    continue;
                }
                if (earlier == index) {
                    break;
                }
                ++impl_index;
            }
            const auto &impls = driver_->environment().impls();
            if (const auto iter = impls.find(impl_index); iter != impls.end()) {
                update.payload = iter->second;
            }
            break;
        }
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
                .type_param_names = {},
                .fields = {},
                .where_clause = build_where_clause_info(decl.get().where_clause),
                .declaration_range = decl.get().range,
                .field_index_ = {},
            };

            // Push type-parameter scope so field types can reference them.
            for (const auto &tp : decl.get().type_params) {
                info.type_param_names.push_back(tp->name);
            }
            const auto *prev_type_params = current_type_param_names_;
            current_type_param_names_ = &info.type_param_names;

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

            current_type_param_names_ = prev_type_params;

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
                .type_param_names = {},
                .variants = {},
                .where_clause = build_where_clause_info(decl.get().where_clause),
                .declaration_range = decl.get().range,
                .variant_set_ = {},
            };

            // Push type-parameter scope so variant payload types can reference them.
            for (const auto &tp : decl.get().type_params) {
                info.type_param_names.push_back(tp->name);
            }
            const auto *prev_enum_type_params = current_type_param_names_;
            current_type_param_names_ = &info.type_param_names;

            std::unordered_set<std::string> seen_variants;
            for (const auto &variant : decl.get().variants) {
                if (!seen_variants.insert(variant->name).second) {
                    typecheck_error_here(
                        error_codes::typecheck::DuplicateVariant,
                        messages::typecheck::DuplicateEnumVariant.format_with(variant->name),
                        variant->range);
                }

                // P1 (ADT, RFC §1.5): resolve the variant's optional positional
                // payload. Empty for the legacy payload-less enum form.
                EnumVariantInfo variant_info{
                    .name = variant->name,
                    .declaration_range = variant->range,
                };
                variant_info.payload.reserve(variant->payload.size());
                for (const auto &slot : variant->payload) {
                    variant_info.payload.push_back(resolve_type(*slot));
                }

                info.variants.push_back(std::move(variant_info));
            }

            current_type_param_names_ = prev_enum_type_params;

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
                .where_clause = build_where_clause_info(decl.get().where_clause),
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

            // P4a (RFC corelib-effect-system.zh.md §2.4): predicates are
            // implicitly effect Pure and may not carry an explicit effect
            // clause. The grammar does not currently permit one, but the
            // semantic check is in place as a defense-in-depth measure: if
            // the grammar is ever extended, this diagnostic fires instead
            // of silently accepting an effectful predicate.
            if (decl.get().effect_clause) {
                typecheck_error_here(error_codes::typecheck::EffectOnPredicate,
                                     messages::typecheck::EffectOnPredicate.format_with(),
                                     decl.get().effect_clause->range);
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
                // Wave-20 QW-4: `context` clause is now optional at the
                // grammar. When the user omits it we fall back to an
                // anonymous struct type (empty fields — equivalent to no
                // mutable state) AND emit a diagnostic telling them how to
                // declare a context type (AHFL grammar requires input →
                // context → output, but omitting context is allowed for
                // stateless agents).
                .context_type = decl.get().context_type != nullptr
                                    ? resolve_type(*decl.get().context_type)
                                    : make_type(TypeKind::Unit),
                .output_type = resolve_type(*decl.get().output_type),
                .capability_symbols = {},
                .declaration_range = decl.get().range,
                .input_type_range = decl.get().input_type->range,
                .context_type_range = decl.get().context_type != nullptr
                                          ? decl.get().context_type->range
                                          : decl.get().range,
                .output_type_range = decl.get().output_type->range,
                .states = {},
                .initial_state = {},
                .final_states = {},
                .transitions = {},
                .quota = {},
            };

            // Wave-20 QW-4: helpful diagnostics when optional fields are
            // omitted. These replace the old opaque ANTLR "mismatched input
            // 'output' expecting 'context'" (pre-QW-4 parse error) with a
            // friendly semantic-level note.
            if (decl.get().context_type == nullptr) {
                auto builder =
                    result_.diagnostics
                        .warning()
                        .code(error_codes::typecheck::AgentContextOmitted)
                        .message(messages::typecheck::AgentContextMissingNote,
                                 info.canonical_name)
                        .range(decl.get().range)
                        .with_note(
                            "hint: insert `context: StructType;` between `input` and `output` sections to give this agent mutable state",
                            decl.get().range);
                if (current_source_ != nullptr) {
                    std::move(builder).source(current_source_->source).emit();
                } else {
                    std::move(builder).emit();
                }
            }
            if (decl.get().capabilities.empty() &&
                decl.get().capabilities_range.empty()) {
                auto builder =
                    result_.diagnostics
                        .warning()
                        .code(error_codes::typecheck::
                                     AgentCapabilitiesOmitted)
                        .message(
                            messages::typecheck::
                                AgentCapabilitiesMissingNote,
                            info.canonical_name)
                        .range(decl.get().range)
                        .with_note(
                            "hint: insert `capabilities: [Cap1, Cap2];` (or `capabilities: [];` for none explicitly) between `final` and first `transition`",
                            decl.get().range);
                if (current_source_ != nullptr) {
                    std::move(builder)
                        .source(current_source_->source)
                        .emit();
                } else {
                    std::move(builder).emit();
                }
            }

            check_schema_boundary_decl_type(
                info.input_type, SchemaBoundaryKind::AgentInput, decl.get().input_type->range);
            // Wave-20 QW-4: when `context` is omitted (QW-4 grammar relaxed) we
            // pass the synthetic empty struct type and the agent's whole range
            // as the diagnostic anchor so warnings don't reference nullptr.
            check_schema_boundary_decl_type(
                info.context_type,
                SchemaBoundaryKind::AgentContextDefault,
                decl.get().context_type != nullptr ? decl.get().context_type->range
                                                    : decl.get().range);
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

bool TypeCheckPass::builtin_hook_allowed_by_current_source(std::string_view hook) const {
    if (current_source_ == nullptr ||
        !current_source_->compiler_intrinsics_allow.has_value()) {
        return true;
    }

    const auto &allowlist = *current_source_->compiler_intrinsics_allow;
    return std::any_of(allowlist.begin(), allowlist.end(), [&](const auto &pattern) {
        return matches_builtin_allow_pattern(pattern, hook);
    });
}

void TypeCheckPass::validate_builtin_attribute(std::string_view hook,
                                               bool has_effect_clause,
                                               SourceRange range) {
    if (!is_std_module(current_module_name_)) {
        typecheck_error_here(error_codes::typecheck::InvalidBuiltinAttribute,
                             messages::typecheck::InvalidBuiltinAttribute.format_with(),
                             range);
    }
    if (!has_effect_clause) {
        typecheck_error_here(error_codes::typecheck::MissingBuiltinEffect,
                             messages::typecheck::MissingBuiltinEffect.format_with(),
                             range);
    }
    if (!is_known_builtin_hook(hook)) {
        typecheck_error_here(error_codes::typecheck::UnknownBuiltinHook,
                             messages::typecheck::UnknownBuiltinHook.format_with(hook),
                             range);
        return;
    }
    if (!builtin_hook_allowed_by_current_source(hook)) {
        typecheck_error_here(error_codes::typecheck::BuiltinHookNotAllowed,
                             messages::typecheck::BuiltinHookNotAllowed.format_with(hook),
                             range);
    }
}

void TypeCheckPass::build_fn_types() {
    // P2 (RFC §3.2.2 / §3.2.3 / §2 / §6): resolve each fn's signature into a
    // FnTypeInfo registered under its Function symbol id. The body is NOT
    // type-checked here: fn bodies can call other fns (mutual recursion) and
    // may use generic type parameters that need the full environment, so body
    // typecheck is deferred to FnSema after the environment is fully built.
    //
    // The signature-only pass is sufficient for call-site resolution: a
    // fn call `f(args)` needs f's param/return types, which are resolved
    // here. Generic instantiation at the call site substitutes type_args
    // into the resolved signature (P2d monomorphization records these
    // call sites).
    for (const auto &[id, decl] : fn_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            FnTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .local_name = decl.get().name,
                .params = {},
                .return_type = nullptr,
                .return_type_range =
                    decl.get().return_type ? decl.get().return_type->range : SourceRange{},
                .type_param_names = {},
                .where_clause = build_where_clause_info(decl.get().where_clause),
                .effect = {},
                .has_body = static_cast<bool>(decl.get().body),
                .declaration_range = decl.get().range,
                .builtin_name = decl.get().builtin_name, // P5: propagate @builtin name
            };

            if (decl.get().builtin_name.has_value()) {
                validate_builtin_attribute(*decl.get().builtin_name,
                                           static_cast<bool>(decl.get().effect_clause),
                                           decl.get().range);
            }

            for (const auto &type_param : decl.get().type_params) {
                info.type_param_names.push_back(type_param->name);
            }

            // P2: activate the type parameter scope so resolve_type produces
            // TypeVar for names matching a type parameter (instead of treating
            // them as unknown types).
            const auto *prev_type_params = current_type_param_names_;
            current_type_param_names_ = &info.type_param_names;

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = param->type ? resolve_type(*param->type) : make_error_type(),
                    .declaration_range = param->range,
                });
            }

            info.return_type =
                decl.get().return_type ? resolve_type(*decl.get().return_type) : make_error_type();

            // Restore previous type param scope.
            current_type_param_names_ = prev_type_params;

            // Resolve the effect clause: Pure/Nondet pass through as the
            // canonical kind; Capability resolves each named capability to its
            // symbol id (the resolver already emitted a diagnostic for any
            // unresolvable capability, so missing references become nullopt
            // silently here — the typed signature just omits them).
            if (decl.get().effect_clause) {
                const auto &clause = *decl.get().effect_clause;
                info.effect.kind = static_cast<int>(clause.kind);
                info.effect.source_range = clause.range;
                info.effect.has_decreases = static_cast<bool>(clause.decreases_expr);
                if (info.effect.has_decreases) {
                    info.effect.decreases_expr_range = clause.decreases_expr->range;
                }
                if (clause.kind == ast::EffectClauseKind::Capability) {
                    for (const auto &capability_name : clause.capabilities) {
                        const auto reference = find_reference_here(ReferenceKind::AgentCapability,
                                                                   capability_name->range);
                        if (reference.has_value()) {
                            info.effect.capabilities.push_back(reference->get().target);
                        }
                    }
                }
                // P4a (RFC corelib-effect-system.zh.md §2.2 / §6.1): project
                // the declared clause to the signature-level EffectJudgement.
                // Capability clauses become CapabilitySet over the resolved
                // capability symbols; Pure/Nondet pass through directly.
                info.effect.judgement = build_effect_judgement(clause);

                // P4a (RFC §3.4 / §4.5 V2): effect Pure functions must declare
                // a `decreases` termination measure (unless the function has no
                // body — i.e. it's a declaration only, like an extern or trait
                // method signature — in which case the measure is supplied by
                // the implementation).
                if (clause.kind == ast::EffectClauseKind::Pure && !info.effect.has_decreases &&
                    info.has_body) {
                    typecheck_error_here(
                        error_codes::typecheck::NoDecreases,
                        messages::typecheck::NoDecreases.format_with(decl.get().name),
                        clause.range);
                }
            }

            environment().functions_.emplace(id, std::move(info));
        });
    }
}

namespace {

// P3 (RFC §3.2.2 / type-system §1.3 / §1.4): structurally compare a trait
// method's signature to an impl method's signature. The driver member
// functions build_trait_types / build_impl_types own the actual resolution
// (they need private resolve_type / make_error_type); these free helpers only
// do post-resolution diagnostics rendering, so they stay out of the driver's
// private surface. Kept in the anonymous namespace so they do not leak into
// the header.

} // namespace

// P3 (RFC §3.2.2 / type-system §1.4): lift an FnEffectClauseInfo out of an
// ast::EffectClauseSyntax in the same shape as build_fn_types. Member function
// so it can reach the private find_reference_here (which honours the current
// source/module context the same way build_fn_types does).
FnEffectClauseInfo
TypeCheckPass::resolve_effect_clause_info(const Owned<ast::EffectClauseSyntax> &clause) {
    FnEffectClauseInfo info{};
    if (!clause) {
        return info;
    }
    info.kind = static_cast<int>(clause->kind);
    info.source_range = clause->range;
    info.has_decreases = static_cast<bool>(clause->decreases_expr);
    if (info.has_decreases) {
        info.decreases_expr_range = clause->decreases_expr->range;
    }
    if (clause->kind == ast::EffectClauseKind::Capability) {
        for (const auto &capability_name : clause->capabilities) {
            const auto reference =
                find_reference_here(ReferenceKind::AgentCapability, capability_name->range);
            if (reference.has_value()) {
                info.capabilities.push_back(reference->get().target);
            }
        }
    }
    info.judgement = build_effect_judgement(*clause);
    return info;
}

// P4a (RFC corelib-effect-system.zh.md §2.2 / §6.1): project an
// ast::EffectClauseSyntax to the signature-level EffectJudgement. Pure/Nondet
// pass through; Capability becomes a CapabilitySet over the resolved
// capability symbols (missing references are dropped — the resolver already
// diagnosed them).
EffectJudgement TypeCheckPass::build_effect_judgement(const ast::EffectClauseSyntax &clause) {
    switch (clause.kind) {
    case ast::EffectClauseKind::Pure:
        return EffectJudgement::make_pure();
    case ast::EffectClauseKind::Nondet:
        return EffectJudgement::make_nondet();
    case ast::EffectClauseKind::Capability: {
        CapabilitySymbolSet caps;
        for (const auto &capability_name : clause.capabilities) {
            const auto reference =
                find_reference_here(ReferenceKind::AgentCapability, capability_name->range);
            if (reference.has_value()) {
                caps.insert(reference->get().target);
            }
        }
        return EffectJudgement::make_capability_set(std::move(caps));
    }
    }
    return EffectJudgement::make_pure();
}

// P4a (RFC corelib-effect-system.zh.md §2.6.4 / §4.5 V2): check that a
// function's inferred body effect is covered by its declared effect. The
// declared effect must be an upper bound of the body effect (body ⊑ declared).
// If the body effect is stronger (i.e. not covered by the declared effect),
// emit E::effect_underdeclared.
//
// The body effect is passed as an ExprEffect (the expression-level inference
// lattice, produced by check_expr / check_block) and projected to the
// signature-level EffectJudgement via `project()` before comparison.
//
// TODO(FnSema): call this from the FnSema pass after type-checking the
// function body. The body's overall ExprEffect is the join of all
// statement/expression effects in the body (see check_block / check_expr).
// For now, this function is unused but ready for the FnSema integration.
void TypeCheckPass::check_fn_effect_underdeclared(SymbolId fn_symbol,
                                                  ExprEffect body_effect,
                                                  SourceRange body_range) {
    const auto fn = environment().get_fn(fn_symbol);
    if (!fn.has_value()) {
        return;
    }

    const auto &declared = fn->get().effect.judgement;
    const auto body_judgement = project(body_effect);

    // The declared effect must be an upper bound: body ⊑ declared.
    // If not, the function under-declares its effect.
    if (!judgement_le(body_judgement, declared)) {
        typecheck_error_here(error_codes::typecheck::EffectUnderdeclared,
                             messages::typecheck::EffectUnderdeclared.format_with(
                                 fn->get().canonical_name,
                                 std::string(to_string(declared)),
                                 std::string(to_string(body_judgement))),
                             body_range);
    }
}

// P3 (RFC §3.2.2 / type-system §1.3): turn one trait-item method signature
// (no body) into a TraitMethodInfo. Self is left opaque — the impl matcher
// substitutes the impl target type at resolution time.
TraitMethodInfo TypeCheckPass::resolve_trait_method_info(const ast::TraitItemSyntax &item) {
    TraitMethodInfo info{
        .name = item.name,
        .params = {},
        .return_type = item.return_type ? resolve_type(*item.return_type) : make_error_type(),
        .return_type_range = item.return_type ? item.return_type->range : SourceRange{},
        .type_param_names = {},
        .effect = resolve_effect_clause_info(item.effect_clause),
        .declaration_range = item.range,
    };
    for (const auto &type_param : item.type_params) {
        info.type_param_names.push_back(type_param->name);
    }
    for (const auto &param : item.params) {
        info.params.push_back(ParamTypeInfo{
            .name = param->name,
            .type = param->type ? resolve_type(*param->type) : make_error_type(),
            .declaration_range = param->range,
        });
    }
    return info;
}

void TypeCheckPass::build_trait_types() {
    // P3 (RFC §3.2.2 / type-system §1.3): resolve each trait's method
    // signatures, super-traits, and associated types into a TraitTypeInfo
    // keyed by the Trait symbol id. Method-call resolution is deferred to the
    // call-site typecheck pass (none today). Super-trait references are
    // resolved to Trait symbols; an unresolved super-trait or a non-trait
    // symbol is diagnosed here so a `trait B: NotATrait` is caught.
    for (const auto &[id, decl] : trait_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            TraitTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .local_name = decl.get().name,
                .type_param_names = {},
                .super_traits = {},
                .methods = {},
                .assoc_types = {},
                .where_clause = build_where_clause_info(decl.get().where_clause),
                .declaration_range = decl.get().range,
            };

            for (const auto &type_param : decl.get().type_params) {
                info.type_param_names.push_back(type_param->name);
            }

            // B-2: augment with implicit "Self" as the front-most type
            // parameter. The trait-level scope (when checking trait
            // signatures, impl-method dispatch prefixes, super-trait
            // bounds) is always:
            //     [Self] + declared_type_params
            // Any TypeVarT indexed from a trait method signature must use
            // this exact same ordering, so we keep a single canonical
            // copy on TraitTypeInfo instead of re-deriving per call site.
            info.self_augmented_type_param_names.clear();
            info.self_augmented_type_param_names.reserve(1 + info.type_param_names.size());
            info.self_augmented_type_param_names.push_back("Self");
            for (const auto &tp : info.type_param_names) {
                info.self_augmented_type_param_names.push_back(tp);
            }

            for (const auto &super_type : decl.get().super_traits) {
                const auto super_named = super_type ? super_type->is<ast::NamedType>() : false;
                if (!super_named) {
                    continue;
                }
                // C-2 (Wave-24): the resolver may have registered the super-trait
                // as either a TypeName reference (pre-C-2) or a TraitBound reference
                // (post-C-2). Check both, preferring TraitBound.
                const auto super_ref =
                    find_reference_here(ReferenceKind::TraitBound, super_type->range);
                std::optional<SymbolId> super_id;
                if (super_ref.has_value()) {
                    super_id = super_ref->get().target;
                } else {
                    // Fallback 1: TypeName reference (legacy path / cross-module)
                    const auto type_ref =
                        find_reference_here(ReferenceKind::TypeName, super_type->range);
                    if (type_ref.has_value()) {
                        super_id = type_ref->get().target;
                    }
                }
                if (!super_id.has_value()) {
                    // Fallback 2: direct namespace lookup — try Traits first,
                    // then Types (legacy).
                    const auto super_spelling =
                        super_type->as<ast::NamedType>().name->spelling();
                    auto fb = find_local_here(SymbolNamespace::Traits, super_spelling);
                    if (!fb.has_value()) {
                        fb = find_local_here(SymbolNamespace::Types, super_spelling);
                    }
                    if (fb.has_value()) {
                        super_id = fb->get().id;
                    }
                }
                if (!super_id.has_value()) {
                    typecheck_error_here(error_codes::typecheck::ImplTraitUnknown,
                                         messages::typecheck::ImplTraitUnknown.format_with(
                                             super_type->as<ast::NamedType>().name->spelling()),
                                         super_type->range);
                    continue;
                }
                const auto super_symbol = symbol_of(*super_id);
                if (!super_symbol.has_value() || super_symbol->get().kind != SymbolKind::Trait) {
                    typecheck_error_here(
                        error_codes::typecheck::InvalidTypeReference,
                        messages::typecheck::SymbolDoesNotNameType.format_with(
                            super_symbol.has_value()
                                ? super_symbol->get().canonical_name
                                : super_type->as<ast::NamedType>().name->spelling()),
                        super_type->range);
                    continue;
                }
                info.super_traits.push_back(*super_id);
            }

            for (const auto &item : decl.get().items) {
                if (item->kind == ast::TraitItemKind::Fn) {
                    info.methods.push_back(resolve_trait_method_info(*item));
                    continue;
                }
                if (item->kind == ast::TraitItemKind::AssocType && item->assoc_type) {
                    TraitAssocTypeInfo assoc{
                        .name = item->assoc_type->name,
                        .type_param_names = {},
                        .default_type = item->assoc_type->default_type
                                            ? resolve_type(*item->assoc_type->default_type)
                                            : nullptr,
                        .declaration_range = item->assoc_type->range,
                    };
                    for (const auto &type_param : item->assoc_type->type_params) {
                        assoc.type_param_names.push_back(type_param->name);
                    }
                    info.assoc_types.push_back(std::move(assoc));
                    continue;
                }

                // P3c.S1: associated constant trait items are accepted by the
                // syntactic boundary; semantic handling (default-value typing +
                // impl-side signature matching) is deferred to P3b (trait/impl
                // signature matching pass). We record them here as syntactically
                // present so downstream passes can iterate trait items uniformly.
                if (item->kind == ast::TraitItemKind::AssocConst && item->assoc_const) {
                    continue;
                }
            }

            environment().traits_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_impl_types() {
    // P3 (RFC §3.2.2 / type-system §1.4): resolve each impl block:
    //   1. target_type -> a nominal Struct/Enum type (RFC §1.4 TypeRef). The
    //      orphan-rule check (RFC §2.2) compares the impl's module against the
    //      target type's defining module.
    //   2. trait_ref (when present) -> a Trait symbol; the impl method set is
    //      matched against the trait's method signatures + super-trait
    //      coverage. Inherent impls (no trait_ref) skip signature matching.
    //   3. duplicate impl detection: two trait impls for the same
    //      (trait, target) pair are rejected (RFC §2.1 coherence).
    // Impl method bodies are checked by ImplSema after the complete
    // environment is available, mirroring the fn signature/body split.
    //
    // Impl index = source order, matching the indexing in
    // DeclarationIndexBuilder::index_program_declarations.
    // P3c.S4a: strict 100% coherence duplicate detection. Walks the impl table
    // in source declaration order and for each new impl that targets a trait
    // re-checks against every previously-inserted impl using the shared
    // impls_conflict_for_type predicate — that predicate is the single
    // source of truth also used by find_impls candidate filtering and by
    // check_impl_coherence (orphan-rule comparisons). Conflicts are diagnosed
    // with typecheck.COHERENCE_CONFLICT.
    std::vector<std::reference_wrapper<const ImplTypeInfo>> seen_impls;
    seen_impls.reserve(impl_decls_.size());
    auto coherence_conflict = [&](const ImplTypeInfo &candidate) -> bool {
        for (const auto &existing_ref : seen_impls) {
            const auto &existing = existing_ref.get();
            if (!TypeEnvironment::impls_conflict_for_type(existing, candidate)) {
                continue;
            }
            const auto trait_name = candidate.trait_name;
            const auto target_name = nominal_describe(*candidate.target_type);
            std::vector<Diagnostic::Related> notes;
            notes.push_back(Diagnostic::Related{
                .message = messages::typecheck::CoherenceConflictPrevious.format_with(
                    trait_name,
                    existing.target_type ? nominal_describe(*existing.target_type) : target_name),
                .range = existing.declaration_range,
            });
            typecheck_error_here(error_codes::typecheck::CoherenceConflict,
                                 messages::typecheck::CoherenceConflict.format_with(
                                     trait_name, target_name),
                                 candidate.declaration_range,
                                 std::move(notes));
            return true;
        }
        return false;
    };

    for (const auto &[impl_index, entry] : impl_decls_) {
        const auto &decl = entry.decl.get();
        with_symbol_context_for_impl(entry.source_id, [&]() {
            ImplTypeInfo info{
                .index = impl_index,
                .is_inherent = !decl.trait_ref,
                .target_type = nullptr,
                .type_param_names = {},
                .methods = {},
                .assoc_items = {},
                .declaration_range = decl.range,
                .trait_ref_range = decl.trait_ref ? decl.trait_ref->range : SourceRange{},
                .target_type_range = decl.target_type ? decl.target_type->range : SourceRange{},
                .source_id = entry.source_id,
                .module_name = module_name_of(entry.source_id),
            };

            for (const auto &type_param : decl.type_params) {
                info.type_param_names.push_back(type_param->name);
            }

            // --- Scope: impl-level type params -------------------------------
            // P3c: impl headers can carry generics (impl<T> Foo<T>, impl<T> Bar for
            // List<T>, …). These type param names must be in scope when we
            // resolve the impl target type, the trait ref type, every method
            // signature (return type, param types), and every associated-type
            // RHS. Previously this scope was missing, which made impl bodies
            // type-annotated with `T` fail with unknown-type errors.
            //
            // B-2 (gap 3): the full scope for an impl block is four layers:
            //     [Self] → [trait tparams] → [impl tparams] → [method tparams]
            // We inject `Self` unconditionally (both inherent and trait impls
            // reference Self in method signatures / body closures). For trait
            // impls we additionally walk the matched TraitDecl to pull in its
            // declared type-param names so `\x: TraitT -> x` closures inside
            // method bodies resolve correctly. The names are prepended *before*
            // the impl-level names so method_info.type_param_names (which is
            // snapshotted from impl_and_method_tparams after method tparams are
            // appended) carries the full four-layer prefix in canonical order;
            // that snapshot is what ImplSema::check_impl_method_body uses as
            // its TypeVarT-index baseline (vector B).
            const auto *prev_type_params = current_type_param_names_;
            // Reserve room for impl-level names + per-method extra params so
            // that appending method type params in the loop below never
            // reallocates the backing store (current_type_param_names_ points
            // directly into this vector).
            //
            // B-2 note: Self and trait-level type params are NOT injected
            // here. The impl-level scope is strictly the impl-declared tparams
            // (e.g. [T] for `impl<T> List<T>`). Self is resolved by the type
            // resolver through the reference recorded by the resolver pass
            // (the resolver's generic_type_params_ suppresses the UNKNOWN_SYMBOL
            // diagnostic for "Self" inside impl blocks). Trait-level tparams
            // are appended at the END of the body scope in check_impl_method_body
            // via dedup_push_range, so they don't shift the TypeVarT indices
            // of impl/method tparams embedded in method signatures.
            std::vector<std::string> impl_and_method_tparams;
            impl_and_method_tparams.reserve(info.type_param_names.size() + 16);
            impl_and_method_tparams.insert(
                impl_and_method_tparams.end(),
                info.type_param_names.begin(),
                info.type_param_names.end());
            current_type_param_names_ = &impl_and_method_tparams;

            // Resolve target type. RFC §1.4 TypeRef must resolve to a nominal
            // struct/enum (P3 does not impl compound types — RFC leaves it open
            // but the type system has no path-type today).
            if (decl.target_type) {
                info.target_type = resolve_type(*decl.target_type);
            } else {
                info.target_type = make_error_type();
            }
            if (info.target_type) {
                info.target_symbol = nominal_symbol_of(*info.target_type);
            }

            // Resolve trait_ref -> Trait symbol. C-2 (Wave-24): traits live in
            // their own namespace; resolver writes TraitBound references.
            if (decl.trait_ref) {
                const auto trait_named = decl.trait_ref->is<ast::NamedType>();
                if (trait_named) {
                    std::optional<SymbolId> trait_id;
                    // Primary: TraitBound reference (C-2 path)
                    const auto trait_bound_ref =
                        find_reference_here(ReferenceKind::TraitBound, decl.trait_ref->range);
                    if (trait_bound_ref.has_value()) {
                        trait_id = trait_bound_ref->get().target;
                    } else {
                        // Fallback 1: TypeName reference (legacy / cross-module)
                        const auto type_ref =
                            find_reference_here(ReferenceKind::TypeName, decl.trait_ref->range);
                        if (type_ref.has_value()) {
                            trait_id = type_ref->get().target;
                        }
                    }
                    if (!trait_id.has_value()) {
                        // Fallback 2: direct namespace lookup — Traits first,
                        // then Types (legacy).
                        const auto trait_spelling =
                            decl.trait_ref->as<ast::NamedType>().name->spelling();
                        auto fb = find_local_here(SymbolNamespace::Traits, trait_spelling);
                        if (!fb.has_value()) {
                            fb = find_local_here(SymbolNamespace::Types, trait_spelling);
                        }
                        if (fb.has_value()) {
                            trait_id = fb->get().id;
                        }
                    }
                    if (!trait_id.has_value()) {
                        typecheck_error_here(
                            error_codes::typecheck::ImplTraitUnknown,
                            messages::typecheck::ImplTraitUnknown.format_with(
                                decl.trait_ref->as<ast::NamedType>().name->spelling()),
                            decl.trait_ref->range);
                    } else {
                        const auto trait_symbol = symbol_of(*trait_id);
                        if (!trait_symbol.has_value() ||
                            trait_symbol->get().kind != SymbolKind::Trait) {
                            typecheck_error_here(
                                error_codes::typecheck::InvalidTypeReference,
                                messages::typecheck::SymbolDoesNotNameType.format_with(
                                    trait_symbol.has_value()
                                        ? trait_symbol->get().canonical_name
                                        : decl.trait_ref->as<ast::NamedType>().name->spelling()),
                                decl.trait_ref->range);
                        } else {
                            info.trait_symbol = trait_symbol->get().id;
                            info.trait_name = trait_symbol->get().canonical_name;
                        }
                    }
                }
            }

            // Resolve impl method signatures.
            for (const auto &method : decl.methods) {
                // --- Scope: append this method's type params to the impl-
                // level names already in impl_and_method_tparams. Pop them
                // at the end of the loop body so the next iteration starts
                // from the impl-level baseline.
                //
                // NOTE: we construct method_info.type_param_names by REFERENCE
                // (copied below) but resolution happens WITH the appended
                // names active. This mirrors build_fn_types: scope is set
                // before param/return-type resolution. Previously we built
                // ImplMethodInfo with `declaration->type_params` (impl-head
                // names only) and THEN appended method tparams — meaning
                // method-level names like <U> weren't active when we resolved
                // `Fn(T) -> U` / `Option<U>`, producing spurious "unknown type
                // U" errors.
                const auto method_tparam_base = impl_and_method_tparams.size();
                for (const auto &tp : method->type_params) {
                    impl_and_method_tparams.push_back(tp->name);
                }
                // --- Scope: impl-level + method-level type param names combined.
                // The TypeResolver builds TypeVarT using index = position in the
                // combined vector. When check_impl_method_call later builds
                // `subst.assign(method.type_param_names.size())`, the
                // substitution map indices must align with the TypeVarT indices
                // embedded in `method.params` / `method.return_type`. That
                // alignment requires method_info.type_param_names to carry
                // IMPL-LEVEL + METHOD-LEVEL names IN ORDER, matching
                // `impl_and_method_tparams` used during resolve_type() below.
                // Previously method_info.type_param_names carried ONLY the
                // method-level names, so index 0 of subst meant 'U' (method
                // level), but TypeVarT{index=0} inside the type graph meant 'T'
                // (impl level), producing bogus unification like "expected
                // Fn(U)->U, got Fn(T)->U".
                std::vector<std::string> method_type_param_names;
                method_type_param_names.reserve(impl_and_method_tparams.size());
                method_type_param_names.insert(method_type_param_names.end(),
                                               impl_and_method_tparams.begin(),
                                               impl_and_method_tparams.end());
                ImplMethodInfo method_info{
                    .name = method->name,
                    .params = {},
                    .return_type = method->return_type ? resolve_type(*method->return_type)
                                                       : make_error_type(),
                    .return_type_range =
                        method->return_type ? method->return_type->range : SourceRange{},
                    .type_param_names = std::move(method_type_param_names),
                    .effect = resolve_effect_clause_info(method->effect_clause),
                    .has_body = static_cast<bool>(method->body),
                    .declaration_range = method->range,
                    .builtin_name = method->builtin_name,
                };
                // P5 (RFC §3.3): @builtin hook gating — mirror the top-level fn
                // rules (see build_fn_types above). Only stdlib modules may
                // declare @builtin methods, every @builtin method must carry
                // an effect clause (the compiler expects a hook descriptor for
                // effect derivation), and the hook name must be a known builtin.
                //
                // BLK-01: impl methods without a body are the prototype-shape
                // @builtin facades; they must carry a valid @builtin attribute
                // (otherwise the method has no implementation and would fail
                // lowering). User methods always retain a body block.
                if (!method_info.has_body && !method_info.builtin_name.has_value()) {
                    typecheck_error_here(error_codes::typecheck::InvalidBuiltinAttribute,
                                         messages::typecheck::InvalidBuiltinAttribute.format_with(),
                                         method->range);
                }
                if (method->builtin_name.has_value()) {
                    validate_builtin_attribute(*method->builtin_name,
                                               static_cast<bool>(method->effect_clause),
                                               method->range);
                }
                for (const auto &param : method->params) {
                    // P3 (RFC §1.4 inherent impl method semantics): the first
                    // parameter of an inherent impl method is `self`, and the
                    // grammar allows `self` to appear without an explicit type
                    // annotation (equivalent to Swift/Rust shorthand). In that
                    // case the self type is the impl's target type, built from
                    // impl.type_param_names instantiated with TypeVar for each
                    // name — matching how `target_type` is constructed above.
                    // Without this synthesis, `self` has an error type in the
                    // method signature, which makes every subsequent `self.X`
                    // dispatch fail (INVALID_MEMBER_ACCESS), and destroys the
                    // scrutinee context for `match self` (so `Some(value)`
                    // arms never bind the payload name).
                    TypePtr param_type;
                    if (param->type != nullptr) {
                        param_type = resolve_type(*param->type);
                    } else if (!param->name.empty() && param->name[0] == 's' &&
                               param->name == "self" && info.target_type != nullptr) {
                        // target_type is already built from impl type params
                        // under the same current_type_param_names_ scope, so
                        // the TypeVar indices are compatible.
                        param_type = info.target_type;
                    } else {
                        param_type = make_error_type();
                    }
                    method_info.params.push_back(ParamTypeInfo{
                        .name = param->name,
                        .type = std::move(param_type),
                        .declaration_range = param->range,
                    });
                }
                if (method->effect_clause &&
                    method->effect_clause->kind == ast::EffectClauseKind::Pure &&
                    !method_info.effect.has_decreases && method_info.has_body) {
                    typecheck_error_here(error_codes::typecheck::NoDecreases,
                                         messages::typecheck::NoDecreases.format_with(method->name),
                                         method->effect_clause->range);
                }
                info.methods.push_back(std::move(method_info));
                // Pop method-level type params (see scope note at loop top).
                impl_and_method_tparams.resize(method_tparam_base);
            }

            for (const auto &assoc : decl.assoc_items) {
                info.assoc_items.push_back(ImplAssocItemInfo{
                    .name = assoc->name,
                    .type = assoc->type ? resolve_type(*assoc->type) : make_error_type(),
                    .declaration_range = assoc->range,
                });
            }

            // P3c.S1: impl associated constants are accepted at the syntactic
            // boundary; full semantics (typing the default value against the
            // trait's assoc-const signature + value evaluation) are deferred to
            // the P3b trait/impl unified pass.
            for (const auto &ac : decl.const_items) {
                (void)ac; // no-op placeholder: full typing comes in P3b
            }

            // Coherence (orphan rule) — RFC §2.2. Only enforced when both the
            // trait and target resolve cleanly; resolution failures already
            // produced diagnostics above.
            check_impl_coherence(info);

            // Trait-impl signature matching (RFC §2.1) + 100% coherence
            // duplicate detection via the shared impls_conflict_for_type
            // predicate. If signature-matching or conflict detection fail the
            // impl is still inserted into the environment so downstream
            // diagnostics (find_impls, super-trait coverage) can iterate it —
            // the TypeCheckResult::has_errors flag will already be true.
            if (info.trait_symbol.has_value() && info.target_type != nullptr) {
                check_trait_impl_signature_match(info);
                (void)coherence_conflict(info);
            }

            // Record whether the impl is a non-inherent trait impl *before*
            // moving `info` into the environment map, so the value is well
            // defined when we push onto the coherence seen-impls list below.
            const bool register_as_trait_impl =
                !info.is_inherent && info.trait_symbol.has_value();

            environment().impls_.emplace(impl_index, std::move(info));
            // P3c.S5a: register the impl into the declaration-layer impl_index
            // right after it lands in the stable environment map. The index is
            // keyed by (trait, normalized-type) so downstream O(1) lookups
            // never need to rescan impls_.
            const auto &stored = environment().impls_.at(impl_index);
            environment().register_impl_index(impl_index, stored);
            // Track the reference now-pointing into the environment's
            // stable ImplTypeInfo.
            if (register_as_trait_impl) {
                seen_impls.emplace_back(stored);
            }

            // Restore outer type-parameter scope (see impl-level scope note).
            current_type_param_names_ = prev_type_params;
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
                .is_wildcard = clause->is_wildcard,
                .expr_range = expr_range,
                .source_range = clause->range,
                .has_decreases = static_cast<bool>(clause->decreases),
                .decreases_exprs = {},
                // P4.S7b: standalone ContractClauseKind::Decreases carries
                // the wildcard flag on `clause->is_wildcard`; the attached
                // `clause->decreases` syntax is only used for per-clause
                // decreases annotations on requires/ensures/invariant. Merge
                // both flags so typed_hir_lower and the assurance counter
                // derive the same "total decreases expressions" count.
                .decreases_is_wildcard = [&]() {
                    if (clause->kind == ast::ContractClauseKind::Decreases) {
                        return clause->is_wildcard;
                    }
                    return clause->decreases ? clause->decreases->decreases_is_wildcard : false;
                }(),
                .decreases_range = clause->decreases ? clause->decreases->range : SourceRange{},
            };
            if (clause->decreases) {
                clause_info.decreases_exprs.reserve(clause->decreases->decreases_exprs.size());
                clause_info.decreases_expr_ranges.reserve(clause->decreases->decreases_exprs.size());
                for (const auto &decr_expr : clause->decreases->decreases_exprs) {
                    const SourceRange range = decr_expr ? decr_expr->range : SourceRange{};
                    clause_info.decreases_exprs.push_back(DecreasesExprInfo{.expr_range = range});
                    clause_info.decreases_expr_ranges.push_back(range);
                }
            }
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
                    &driver_->resolve_result_.symbol_table,
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
