#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"

namespace ahfl {

/// M4: Syntax desugaring pass.
///
/// Rewrites surface-syntax "sugar" nodes into their canonical
/// nominal-generic equivalents. After this pass runs, downstream passes
/// (resolver, typecheck, IR lowering) see only the desugared form and no
/// longer need special-case handling for built-in container syntax.
///
/// ## Type syntax desugaring
///
///   * `Optional<T>`         → `std::option::Option<T>`
///   * `List<T>`             → `std::collections::List<T>`
///   * `Set<T>`              → `std::collections::Set<T>`
///   * `Map<K, V>`           → `std::collections::Map<K, V>`
///
/// ## Expression syntax desugaring
///
///   * `none`                → `std::option::Option::None`
///   * `some expr`           → `std::option::Option::Some(expr)`
///   * `[a, b, c]`           → `std::collections::List::from_array(a, b, c)`
///   * `{a, b, c}`           → `std::collections::Set::from_array(a, b, c)`
///   * `{k1: v1, k2: v2}`    → `std::collections::Map::from_entries(k1, v1, ...)`
///
/// The pass operates bottom-up, recursively desugaring children before
/// the parent. Source ranges of desugared nodes are preserved from the
/// original sugar so diagnostics still point at the user's code.
///
/// Desugared names are fully-qualified (e.g. `std::option::Option`).
/// Name resolution treats these as ordinary qualified references — the
/// standard library modules are expected to be loadable via the project
/// search path or built-in stdlib root.
class DesugarPass final {
  public:
    DesugarPass() = default;

    /// Run desugaring over an entire program. Returns true if any node
    /// was rewritten (conservatively returns true if any desugaring may
    /// have occurred; downstream does not depend on the precise value).
    bool run(ast::Program &program) const;

  private:
    // --- declarations ---
    static void desugar_decl(ast::Decl &decl);
    static void desugar_fn_decl(ast::FnDecl &decl);
    static void desugar_struct_decl(ast::StructDecl &decl);
    static void desugar_enum_decl(ast::EnumDecl &decl);
    static void desugar_type_alias_decl(ast::TypeAliasDecl &decl);
    static void desugar_impl_decl(ast::ImplDecl &decl);
    static void desugar_trait_decl(ast::TraitDecl &decl);

    // --- type / expression / statement / block ---
    static Owned<ast::TypeSyntax> desugar_type(Owned<ast::TypeSyntax> type);
    static Owned<ast::ExprSyntax> desugar_expr(Owned<ast::ExprSyntax> expr);
    static void desugar_stmt(ast::StatementSyntax &stmt);
    static void desugar_block(ast::BlockSyntax &block);
    static void desugar_pattern(ast::PatternSyntax &pattern);
    static void desugar_match_arm(ast::MatchArmSyntax &arm);
};

} // namespace ahfl
