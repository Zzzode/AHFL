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

    // RFC 0013 P2-S1 (R0): set the scope id stamped onto every TypeVar
    // created through the type-param-scope branch of resolve_named_type.
    // The id identifies the generic declaration whose type-param scope is
    // active (allocated by TypeCheckPass); kUnknownTypeVarScopeId (default)
    // leaves TypeVars unstamped. Always paired with set_type_param_names by
    // the driver — the names decide WHETHER a name becomes a TypeVar, the
    // scope id decides WHICH declaration it belongs to.
    void set_type_param_scope_id(std::uint32_t scope_id) {
        type_param_scope_id_ = scope_id;
    }

    // RFC 0013 P2-S1 (R0.1): set the per-method scope id for method-level
    // type params. When set, TypeVars at index >= method_tparam_offset use
    // method_scope_id instead of type_param_scope_id (which carries the
    // impl's scope). This distinguishes same-named method-level type params
    // across methods in the same impl (e.g. `flat_map<U>` calling
    // `fold<List<U>>` — without the split, both U's share the impl's scope
    // and re-substitution at the call site double-wraps the type).
    void set_method_scope_id(std::uint32_t scope_id, std::size_t tparam_offset) {
        method_scope_id_ = scope_id;
        method_tparam_offset_ = tparam_offset;
    }

    // P3c (RFC 0013): set the self-type override. When non-null, a named
    // type `Self` resolves to this type instead of a type parameter or a
    // symbol lookup. Used inside impl blocks, where Self is the impl's
    // target type.
    void set_self_type_override(TypePtr self_type) {
        self_type_override_ = self_type;
    }

  private:
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name,
                                             std::vector<TypePtr> args,
                                             SourceRange use_range);
    [[nodiscard]] TypePtr make_error_type() const;
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
    // RFC 0013 P2-S1 (R0): scope id stamped onto TypeVars created while the
    // above type-param scope is active. kUnknownTypeVarScopeId when no
    // stamped scope is active (struct/enum/alias declaration types).
    std::uint32_t type_param_scope_id_{kUnknownTypeVarScopeId};
    // RFC 0013 P2-S1 (R0.1): per-method scope for method-level type params.
    // kUnknownTypeVarScopeId when not in an impl method context.
    std::uint32_t method_scope_id_{kUnknownTypeVarScopeId};
    // Number of impl-level type params (prefix of type_param_names_ that
    // uses type_param_scope_id_). Indices >= this offset use method_scope_id_.
    std::size_t method_tparam_offset_{0};
    // P3c (RFC 0013): when non-null, `Self` resolves to this type.
    TypePtr self_type_override_{nullptr};
};

} // namespace ahfl
