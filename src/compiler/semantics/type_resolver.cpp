#include "ahfl/compiler/semantics/type_resolver.hpp"

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
    switch (type.kind) {
    case ast::TypeSyntaxKind::Unit:
        return types_.make(TypeKind::Unit);
    case ast::TypeSyntaxKind::Bool:
        return types_.make(TypeKind::Bool);
    case ast::TypeSyntaxKind::Int:
        return types_.make(TypeKind::Int);
    case ast::TypeSyntaxKind::Float:
        return types_.make(TypeKind::Float);
    case ast::TypeSyntaxKind::String:
        return types_.string();
    case ast::TypeSyntaxKind::BoundedString:
        return types_.bounded_string(type.string_bounds->first, type.string_bounds->second);
    case ast::TypeSyntaxKind::UUID:
        return types_.make(TypeKind::UUID);
    case ast::TypeSyntaxKind::Timestamp:
        return types_.make(TypeKind::Timestamp);
    case ast::TypeSyntaxKind::Duration:
        return types_.make(TypeKind::Duration);
    case ast::TypeSyntaxKind::Decimal:
        return types_.decimal(*type.decimal_scale);
    case ast::TypeSyntaxKind::Named:
        return resolve_named_type(*type.name);
    case ast::TypeSyntaxKind::Optional:
        return types_.optional(resolve_type(*type.first));
    case ast::TypeSyntaxKind::List:
        return types_.list(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Set:
        return types_.set(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Map:
        return types_.map(resolve_type(*type.first), resolve_type(*type.second));
    }

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
