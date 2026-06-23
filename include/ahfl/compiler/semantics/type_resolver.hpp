#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/effect_judgement.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
// M2 (nominal generics): given a type symbol id, return its type-parameter
// names in declaration order. Returns nullopt for monomorphic types or for
// symbols that do not name a type (struct/enum/typealias).
using TypeParamInfoProvider = std::function<std::optional<std::vector<std::string>>(SymbolId)>;

class TypeResolver final {
  public:
    TypeResolver(const ResolveResult &resolve_result,
                 TypeContext &types,
                 TypeAliasResolutionState &alias_state,
                 TypeResolverSourceIdProvider current_source_id,
                 TypeResolverDiagnosticSink diagnose,
                 TypeAliasDeclLookup find_alias_decl,
                 TypeAliasBodyResolver resolve_alias_body,
                 TypeParamInfoProvider type_param_info = {});

    [[nodiscard]] TypePtr resolve_type(const ast::TypeSyntax &type);
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name);
    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range);
    [[nodiscard]] TypePtr resolve_type_alias(SymbolId id, SourceRange use_range);
    // M2 (nominal generics): resolve `Name<T1, T2, ...>`. Validates arity
    // against the declaration's type parameters and returns the instantiated
    // type. For type aliases, performs substitution into the aliased body.
    // `app_range` is the full source range of the application expression.
    [[nodiscard]] TypePtr resolve_app_type(const ast::AppType &app, SourceRange app_range);

    // P2 (RFC §3.2.2): set the names of type parameters currently in scope.
    // When a named type matches one of these, it resolves to a TypeVar
    // instead of looking up a type symbol. Empty set (default) means no
    // type params are in scope.
    void set_type_param_names(const std::vector<std::string> *names) {
        type_param_names_ = names;
    }

  private:
    [[nodiscard]] TypePtr make_error_type() const;
    [[nodiscard]] TypePtr resolve_std_container_type(std::string_view canonical_name,
                                                     std::vector<TypePtr> arguments,
                                                     SourceRange use_range);
    [[nodiscard]] EffectJudgement
    resolve_effect_judgement(ast::EffectClauseKind kind,
                             const std::vector<Owned<ast::QualifiedName>> &capabilities);

    const ResolveResult &resolve_result_;
    TypeContext &types_;
    TypeAliasResolutionState &alias_state_;
    TypeResolverSourceIdProvider current_source_id_;
    TypeResolverDiagnosticSink diagnose_;
    TypeAliasDeclLookup find_alias_decl_;
    TypeAliasBodyResolver resolve_alias_body_;
    TypeParamInfoProvider type_param_info_;
    // P2: currently in-scope type parameter names (nullptr = none).
    // When set, NamedType matching any of these resolves to a TypeVar.
    const std::vector<std::string> *type_param_names_{nullptr};
};

} // namespace ahfl
