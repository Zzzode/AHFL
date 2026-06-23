#include "ahfl/compiler/semantics/type_resolver.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/semantics/effect_judgement.hpp"
#include "ahfl/compiler/semantics/monomorphization.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "compiler/semantics/std_container_types.hpp"

#include <utility>

namespace ahfl {

TypeResolver::TypeResolver(const ResolveResult &resolve_result,
                           TypeContext &types,
                           TypeAliasResolutionState &alias_state,
                           TypeResolverSourceIdProvider current_source_id,
                           TypeResolverDiagnosticSink diagnose,
                           TypeAliasDeclLookup find_alias_decl,
                           TypeAliasBodyResolver resolve_alias_body,
                           TypeParamInfoProvider type_param_info)
    : resolve_result_(resolve_result), types_(types), alias_state_(alias_state),
      current_source_id_(std::move(current_source_id)), diagnose_(std::move(diagnose)),
      find_alias_decl_(std::move(find_alias_decl)),
      resolve_alias_body_(std::move(resolve_alias_body)),
      type_param_info_(std::move(type_param_info)) {}

TypePtr TypeResolver::resolve_type(const ast::TypeSyntax &type) {
    return std::visit(
        Overloaded{
            [&](const ast::UnitType &) { return types_.make(TypeKind::Unit); },
            [&](const ast::BoolType &) { return types_.make(TypeKind::Bool); },
            [&](const ast::IntType &) { return types_.make(TypeKind::Int); },
            [&](const ast::FloatType &) { return types_.make(TypeKind::Float); },
            [&](const ast::StringType &) { return types_.string(); },
            [&](const ast::BoundedStringType &t) {
                return types_.bounded_string(static_cast<std::int64_t>(t.min_length),
                                             static_cast<std::int64_t>(t.max_length));
            },
            [&](const ast::UuidType &) { return types_.make(TypeKind::UUID); },
            [&](const ast::TimestampType &) { return types_.make(TypeKind::Timestamp); },
            [&](const ast::DurationType &) { return types_.make(TypeKind::Duration); },
            [&](const ast::DecimalType &t) {
                return types_.decimal(static_cast<std::int64_t>(t.scale));
            },
            [&](const ast::NamedType &t) {
                std::vector<TypePtr> args;
                args.reserve(t.type_args.size());
                for (const auto &arg : t.type_args) {
                    args.push_back(resolve_type(*arg));
                }
                return resolve_named_type(*t.name, std::move(args), t.name->range);
            },
            [&](const ast::FnType &t) {
                std::vector<TypePtr> param_types;
                param_types.reserve(t.params.size());
                for (const auto &param : t.params) {
                    param_types.push_back(resolve_type(*param));
                }
                TypePtr return_type =
                    t.return_type ? resolve_type(*t.return_type) : types_.make(TypeKind::Unit);
                EffectJudgement effect =
                    t.has_effect_clause
                        ? resolve_effect_judgement(t.effect_kind, t.effect_capabilities)
                        : EffectJudgement::make_pure();
                return types_.fn(std::move(param_types), return_type, std::move(effect));
            },
            [&](const ast::AppType &t) { return resolve_app_type(t, type.range); },
        },
        type.node);

    return make_error_type();
}

TypePtr TypeResolver::resolve_named_type(const ast::QualifiedName &name,
                                         std::vector<TypePtr> args,
                                         SourceRange use_range) {
    // Dispatch the built-in parameterised types that used to have dedicated
    // syntax nodes (Optional/List/Set/Map). Any other type is expected to have
    // no generic arguments and is resolved as a plain named reference.
    if (name.segments.size() == 1 && !args.empty()) {
        const auto &head = name.segments.front();
        std::string_view canonical_name;
        std::size_t expected_arity = 0;
        if (head == "Optional") {
            canonical_name = stdlib_bridge::kOptionType;
            expected_arity = 1;
        } else if (head == "List") {
            canonical_name = stdlib_bridge::kListType;
            expected_arity = 1;
        } else if (head == "Set") {
            canonical_name = stdlib_bridge::kSetType;
            expected_arity = 1;
        } else if (head == "Map") {
            canonical_name = stdlib_bridge::kMapType;
            expected_arity = 2;
        }
        if (!canonical_name.empty()) {
            if (args.size() == expected_arity) {
                if (TypePtr nominal =
                        resolve_std_container_type(canonical_name, args, use_range);
                    nominal != nullptr) {
                    return nominal;
                }
                diagnose_(
                    error_codes::typecheck::InvalidTypeReference,
                    messages::typecheck::StdContainerTypeUnavailable.format_with(canonical_name),
                    use_range);
                return make_error_type();
            }
            // Arity mismatch falls through to UnknownType diagnostic below.
        }
    }

    if (!args.empty()) {
        diagnose_(error_codes::typecheck::UnknownType,
                  messages::typecheck::UnknownType.format_with(name.spelling()),
                  use_range);
        return make_error_type();
    }
    return resolve_named_type(name);
}

TypePtr TypeResolver::resolve_named_type(const ast::QualifiedName &name) {
    // P2: if this name matches a currently-in-scope type parameter, return a
    // TypeVar instead of looking it up as a type symbol.
    // The TypeVar's `index` is the zero-based position in the type-param list —
    // it's the canonical identity used for substitution (O(1) index lookup,
    // industry standard practice), not the name.
    if (type_param_names_ != nullptr) {
        for (std::size_t i = 0; i < type_param_names_->size(); ++i) {
            if (name.spelling() == (*type_param_names_)[i]) {
                return types_.type_var(static_cast<std::uint32_t>(i), (*type_param_names_)[i]);
            }
        }
    }

    const auto reference =
        resolve_result_.find_reference(ReferenceKind::TypeName, name.range, current_source_id_());
    if (!reference.has_value()) {
        diagnose_(error_codes::typecheck::UnknownType,
                  messages::typecheck::UnknownType.format_with(name.spelling()),
                  name.range);
        return make_error_type();
    }

    return resolve_type_symbol(reference->get().target, name.range);
}

TypePtr TypeResolver::resolve_type_symbol(SymbolId id, SourceRange use_range) {
    const auto symbol = resolve_result_.symbol_table.get(id);
    if (!symbol.has_value()) {
        diagnose_(error_codes::typecheck::InvalidTypeReference,
                  messages::typecheck::ResolvedTypeSymbolMissing.format_with(),
                  use_range);
        return make_error_type();
    }

    switch (symbol->get().kind) {
    case SymbolKind::Struct:
        return types_.struct_type(symbol->get().canonical_name, id);
    case SymbolKind::Enum:
        return types_.enum_type(symbol->get().canonical_name, id);
    case SymbolKind::TypeAlias:
        return resolve_type_alias(id, use_range);
    case SymbolKind::Const:
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
    case SymbolKind::Function:
    case SymbolKind::Trait:
        // P2 (RFC §3.2.2): a function name does not name a type. Surfaced
        // here so `fn f(...) -> f { ... }` (or similar) is diagnosed at the
        // point of use rather than silently producing an opaque type.
        // P3 (RFC §3.2.2 / type-system §1.3): a trait name also does not
        // name a type — a trait is a bound/capability, only usable at bound
        // (`T: Trait`) or impl (`impl Trait for T`) positions. A reference
        // such as `fn f() -> Ord` is diagnosed here.
        diagnose_(
            error_codes::typecheck::InvalidTypeReference,
            messages::typecheck::SymbolDoesNotNameType.format_with(symbol->get().canonical_name),
            use_range);
        return make_error_type();
    }

    return make_error_type();
}

TypePtr TypeResolver::resolve_type_alias(SymbolId id, SourceRange use_range) {
    if (const auto cached = alias_state_.resolved_alias_types.find(id.value);
        cached != alias_state_.resolved_alias_types.end()) {
        return cached->second ? cached->second->clone() : make_error_type();
    }

    if (!alias_state_.active_aliases.insert(id.value).second) {
        diagnose_(error_codes::typecheck::InvalidTypeReference,
                  messages::typecheck::TypeAliasCycleDuringResolution.format_with(),
                  use_range);
        return make_error_type();
    }

    const auto alias_decl = find_alias_decl_(id);
    if (!alias_decl.has_value()) {
        alias_state_.active_aliases.erase(id.value);
        diagnose_(error_codes::typecheck::InvalidTypeReference,
                  messages::typecheck::TypeAliasDeclarationMissing.format_with(),
                  use_range);
        return make_error_type();
    }

    auto resolved = resolve_alias_body_(id, *alias_decl->get().aliased_type);
    alias_state_.resolved_alias_types.emplace(id.value,
                                              resolved ? resolved->clone() : make_error_type());
    alias_state_.active_aliases.erase(id.value);
    return resolved;
}

TypePtr TypeResolver::resolve_app_type(const ast::AppType &app, SourceRange app_range) {
    // Resolve the base name. If it's a type variable, that's an error —
    // type variables are not generic constructors.
    if (type_param_names_ != nullptr) {
        for (std::size_t i = 0; i < type_param_names_->size(); ++i) {
            if (app.name->spelling() == (*type_param_names_)[i]) {
                diagnose_(
                    error_codes::typecheck::InvalidTypeReference,
                    messages::typecheck::SymbolDoesNotNameType.format_with(app.name->spelling()),
                    app.name->range);
                return make_error_type();
            }
        }
    }

    const auto reference = resolve_result_.find_reference(
        ReferenceKind::TypeName, app.name->range, current_source_id_());
    if (!reference.has_value()) {
        diagnose_(error_codes::typecheck::UnknownType,
                  messages::typecheck::UnknownType.format_with(app.name->spelling()),
                  app.name->range);
        return make_error_type();
    }

    const SymbolId base_id = reference->get().target;
    const auto base_symbol = resolve_result_.symbol_table.get(base_id);
    if (!base_symbol.has_value()) {
        diagnose_(error_codes::typecheck::InvalidTypeReference,
                  messages::typecheck::ResolvedTypeSymbolMissing.format_with(),
                  app.name->range);
        return make_error_type();
    }

    // Resolve all type arguments first (so we have concrete TypePtrs).
    std::vector<TypePtr> arg_types;
    arg_types.reserve(app.arguments.size());
    for (const auto &arg : app.arguments) {
        arg_types.push_back(resolve_type(*arg));
    }

    // Look up the declaration's type-parameter count for arity checking.
    std::size_t expected_arity = 0;
    if (type_param_info_) {
        if (const auto params = type_param_info_(base_id); params.has_value()) {
            expected_arity = params->size();
        }
    }

    if (arg_types.size() != expected_arity) {
        diagnose_(error_codes::typecheck::WrongArity,
                  messages::typecheck::WrongArity.format_with("type",
                                                              app.name->spelling(),
                                                              std::to_string(expected_arity),
                                                              std::to_string(arg_types.size())),
                  app_range);
        return make_error_type();
    }

    const auto &canonical_name = base_symbol->get().canonical_name;
    switch (base_symbol->get().kind) {
    case SymbolKind::Struct:
        return types_.struct_type(canonical_name, base_id, std::move(arg_types));
    case SymbolKind::Enum:
        return types_.enum_type(canonical_name, base_id, std::move(arg_types));
    case SymbolKind::TypeAlias: {
        // For type aliases, resolve the alias body with type parameters
        // substituted by the provided arguments.
        TypeSubstitutionMap subst{arg_types.begin(), arg_types.end()};
        TypePtr aliased = resolve_type_alias(base_id, app.name->range);
        return substitute_type(aliased, subst, types_);
    }
    case SymbolKind::Const:
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
    case SymbolKind::Function:
    case SymbolKind::Trait:
        diagnose_(error_codes::typecheck::InvalidTypeReference,
                  messages::typecheck::SymbolDoesNotNameType.format_with(canonical_name),
                  app.name->range);
        return make_error_type();
    }

    return make_error_type();
}

TypePtr TypeResolver::make_error_type() const {
    return types_.error_type();
}

TypePtr TypeResolver::resolve_std_container_type(std::string_view canonical_name,
                                                 std::vector<TypePtr> arguments,
                                                 SourceRange use_range) {
    const auto symbol =
        resolve_result_.symbol_table.find_canonical(SymbolNamespace::Types, canonical_name);
    if (!symbol.has_value()) {
        return nullptr;
    }

    std::size_t expected_arity = arguments.size();
    if (type_param_info_) {
        if (const auto params = type_param_info_(symbol->get().id); params.has_value()) {
            expected_arity = params->size();
        }
    }
    if (arguments.size() != expected_arity) {
        return nullptr;
    }

    switch (symbol->get().kind) {
    case SymbolKind::Struct:
        return types_.struct_type(
            symbol->get().canonical_name, symbol->get().id, std::move(arguments));
    case SymbolKind::Enum:
        return types_.enum_type(
            symbol->get().canonical_name, symbol->get().id, std::move(arguments));
    case SymbolKind::TypeAlias: {
        TypeSubstitutionMap subst{arguments.begin(), arguments.end()};
        TypePtr aliased = resolve_type_alias(symbol->get().id, use_range);
        return substitute_type(aliased, subst, types_);
    }
    case SymbolKind::Const:
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
    case SymbolKind::Function:
    case SymbolKind::Trait:
        return nullptr;
    }

    return nullptr;
}

EffectJudgement
TypeResolver::resolve_effect_judgement(ast::EffectClauseKind kind,
                                       const std::vector<Owned<ast::QualifiedName>> &capabilities) {
    switch (kind) {
    case ast::EffectClauseKind::Pure:
        return EffectJudgement::make_pure();
    case ast::EffectClauseKind::Nondet:
        return EffectJudgement::make_nondet();
    case ast::EffectClauseKind::Capability: {
        CapabilitySymbolSet caps;
        for (const auto &capability_name : capabilities) {
            const auto reference = resolve_result_.find_reference(
                ReferenceKind::AgentCapability, capability_name->range, current_source_id_());
            if (reference.has_value()) {
                caps.insert(reference->get().target);
            }
        }
        return EffectJudgement::make_capability_set(std::move(caps));
    }
    }
    return EffectJudgement::make_pure();
}

} // namespace ahfl
