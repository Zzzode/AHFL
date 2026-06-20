#include "ahfl/compiler/semantics/type_resolver.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"

#include <utility>

namespace ahfl {

TypeResolver::TypeResolver(const ResolveResult &resolve_result,
                           TypeContext &types,
                           TypeAliasResolutionState &alias_state,
                           TypeResolverSourceIdProvider current_source_id,
                           TypeResolverDiagnosticSink diagnose,
                           TypeAliasDeclLookup find_alias_decl,
                           TypeAliasBodyResolver resolve_alias_body)
    : resolve_result_(resolve_result), types_(types), alias_state_(alias_state),
      current_source_id_(std::move(current_source_id)), diagnose_(std::move(diagnose)),
      find_alias_decl_(std::move(find_alias_decl)),
      resolve_alias_body_(std::move(resolve_alias_body)) {}

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
            [&](const ast::NamedType &t) { return resolve_named_type(*t.name); },
            [&](const ast::OptionalType &t) { return types_.optional(resolve_type(*t.inner)); },
            [&](const ast::ListType &t) { return types_.list(resolve_type(*t.element)); },
            [&](const ast::SetType &t) { return types_.set(resolve_type(*t.element)); },
            [&](const ast::MapType &t) {
                return types_.map(resolve_type(*t.key_type), resolve_type(*t.value_type));
            },
        },
        type.node);

    return make_error_type();
}

TypePtr TypeResolver::resolve_named_type(const ast::QualifiedName &name) {
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

TypePtr TypeResolver::make_error_type() const {
    return types_.error_type();
}

} // namespace ahfl
