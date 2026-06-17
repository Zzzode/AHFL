#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ahfl {

class TypeContext;

struct TypeAliasResolutionState {
    std::unordered_map<std::size_t, TypePtr> resolved_alias_types;
    std::unordered_set<std::size_t> active_aliases;
};

using TypeResolverSourceIdProvider = std::function<std::optional<SourceId>()>;
using TypeResolverDiagnosticSink =
    std::function<void(ErrorCode<DiagnosticCategory::TypeCheck>, std::string, SourceRange)>;
using TypeAliasDeclLookup = std::function<MaybeCRef<ast::TypeAliasDecl>(SymbolId)>;
using TypeAliasBodyResolver = std::function<TypePtr(SymbolId, const ast::TypeSyntax &)>;

class TypeResolver final {
  public:
    TypeResolver(const ResolveResult &resolve_result,
                 TypeContext &types,
                 TypeAliasResolutionState &alias_state,
                 TypeResolverSourceIdProvider current_source_id,
                 TypeResolverDiagnosticSink diagnose,
                 TypeAliasDeclLookup find_alias_decl,
                 TypeAliasBodyResolver resolve_alias_body);

    [[nodiscard]] TypePtr resolve_type(const ast::TypeSyntax &type);
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name);
    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range);
    [[nodiscard]] TypePtr resolve_type_alias(SymbolId id, SourceRange use_range);

  private:
    [[nodiscard]] TypePtr make_error_type() const;

    const ResolveResult &resolve_result_;
    TypeContext &types_;
    TypeAliasResolutionState &alias_state_;
    TypeResolverSourceIdProvider current_source_id_;
    TypeResolverDiagnosticSink diagnose_;
    TypeAliasDeclLookup find_alias_decl_;
    TypeAliasBodyResolver resolve_alias_body_;
};

} // namespace ahfl
