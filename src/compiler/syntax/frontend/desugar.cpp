#include "ahfl/compiler/frontend/desugar.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/ownership.hpp"

#include <utility>
#include <vector>

namespace ahfl {

namespace {

// ---------------------------------------------------------------------------
// Desugaring target paths (fully-qualified stdlib names)
// ---------------------------------------------------------------------------

const std::vector<std::string> kOptionTypePath = {"std", "option", "Option"};
const std::vector<std::string> kOptionVariantSome = {"std", "option", "Option", "Some"};
const std::vector<std::string> kOptionVariantNone = {"std", "option", "Option", "None"};

const std::vector<std::string> kListTypePath = {"std", "collections", "List"};
const std::vector<std::string> kListFromArrayPath = {"std", "collections", "list_from_array"};

const std::vector<std::string> kSetTypePath = {"std", "collections", "Set"};
const std::vector<std::string> kSetFromArrayPath = {"std", "collections", "set_from_array"};

const std::vector<std::string> kMapTypePath = {"std", "collections", "Map"};
const std::vector<std::string> kMapFromEntriesPath = {"std", "collections", "map_from_entries"};

// ---------------------------------------------------------------------------
// Synthetic AST node builders (file-local helpers)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Forward declarations (mutual recursion)
// ---------------------------------------------------------------------------

Owned<ast::TypeSyntax> desugar_type_node(Owned<ast::TypeSyntax> type);
Owned<ast::ExprSyntax> desugar_expr_node(Owned<ast::ExprSyntax> expr);
void desugar_block_node(ast::BlockSyntax &block);
void desugar_stmt_node(ast::StatementSyntax &stmt);

// ---------------------------------------------------------------------------
// Type syntax desugaring
// ---------------------------------------------------------------------------

Owned<ast::TypeSyntax> desugar_type_node(Owned<ast::TypeSyntax> type) {
    // Step 1: recursively desugar children (bottom-up)
    std::visit(Overloaded{
                   [&](ast::NamedType &t) {
                       for (auto &arg : t.type_args) {
                           arg = desugar_type_node(std::move(arg));
                       }
                   },
                   [&](ast::AppType &t) {
                       for (auto &arg : t.arguments) {
                           arg = desugar_type_node(std::move(arg));
                       }
                   },
                   [&](ast::FnType &t) {
                       for (auto &p : t.params) {
                           p = desugar_type_node(std::move(p));
                       }
                       if (t.return_type) {
                           t.return_type = desugar_type_node(std::move(t.return_type));
                       }
                   },
                   [](auto &) { /* leaf: no child types */ },
               },
               type->node);

    // Step 2: desugar this node if it's a sugar form
    // (Syntax sugar types Optional/List/Set/Map are now parsed directly as NamedType
    //  with type_args by P5.5, so no translation needed at this layer.)

    return type;
}

// ---------------------------------------------------------------------------
// Expression syntax desugaring
// ---------------------------------------------------------------------------

void desugar_expr_list(std::vector<Owned<ast::ExprSyntax>> &list) {
    for (auto &e : list) {
        e = desugar_expr_node(std::move(e));
    }
}

Owned<ast::ExprSyntax> desugar_expr_node(Owned<ast::ExprSyntax> expr) {
    // Step 1: recursively desugar children
    std::visit(Overloaded{
                   [&](ast::CallExpr &e) {
                       for (auto &type_arg : e.type_args) {
                           type_arg = desugar_type_node(std::move(type_arg));
                       }
                       desugar_expr_list(e.arguments);
                   },
                   [&](ast::MethodCallExpr &e) {
                       e.receiver = desugar_expr_node(std::move(e.receiver));
                       for (auto &type_arg : e.type_args) {
                           type_arg = desugar_type_node(std::move(type_arg));
                       }
                       desugar_expr_list(e.arguments);
                   },
                   [&](ast::StructLiteralExpr &e) {
                       for (auto &f : e.fields) {
                           f->value = desugar_expr_node(std::move(f->value));
                       }
                   },
                   [&](ast::UnaryExpr &e) { e.operand = desugar_expr_node(std::move(e.operand)); },
                   [&](ast::BinaryExpr &e) {
                       e.lhs = desugar_expr_node(std::move(e.lhs));
                       e.rhs = desugar_expr_node(std::move(e.rhs));
                   },
                   [&](ast::MemberAccessExpr &e) { e.base = desugar_expr_node(std::move(e.base)); },
                   [&](ast::IndexAccessExpr &e) {
                       e.base = desugar_expr_node(std::move(e.base));
                       e.index = desugar_expr_node(std::move(e.index));
                   },
                   [&](ast::GroupExpr &e) { e.inner = desugar_expr_node(std::move(e.inner)); },
                   [&](ast::MatchExpr &e) {
                       e.scrutinee = desugar_expr_node(std::move(e.scrutinee));
                       for (auto &arm : e.arms) {
                           if (arm->guard) {
                               arm->guard = desugar_expr_node(std::move(arm->guard));
                           }
                           arm->body = desugar_expr_node(std::move(arm->body));
                       }
                   },
                   [&](ast::LambdaExpr &e) {
                       e.body = desugar_expr_node(std::move(e.body));
                       for (auto &param : e.params) {
                           if (param->type) {
                               param->type = desugar_type_node(std::move(param->type));
                           }
                       }
                   },
                   [](auto &) { /* leaf: no child exprs */ },
               },
               expr->node);

    // Step 2: desugar this node if it's a sugar form
    // (Expr sugar variants none/some/[a,b]/set{}/map{} are now parsed via ADT
    //  constructors by P5.6a; dedicated desugar patterns deferred to P5.6b.)

    return expr;
}

// ---------------------------------------------------------------------------
// Statement / block desugaring
// ---------------------------------------------------------------------------

void desugar_stmt_node(ast::StatementSyntax &stmt) {
    switch (stmt.kind) {
    case ast::StatementSyntaxKind::Let:
        if (stmt.let_stmt->type) {
            stmt.let_stmt->type = desugar_type_node(std::move(stmt.let_stmt->type));
        }
        if (stmt.let_stmt->initializer) {
            stmt.let_stmt->initializer = desugar_expr_node(std::move(stmt.let_stmt->initializer));
        }
        break;
    case ast::StatementSyntaxKind::Assign:
        stmt.assign_stmt->value = desugar_expr_node(std::move(stmt.assign_stmt->value));
        break;
    case ast::StatementSyntaxKind::If:
        stmt.if_stmt->condition = desugar_expr_node(std::move(stmt.if_stmt->condition));
        desugar_block_node(*stmt.if_stmt->then_block);
        if (stmt.if_stmt->else_block) {
            desugar_block_node(*stmt.if_stmt->else_block);
        }
        break;
    case ast::StatementSyntaxKind::Return:
        if (stmt.return_stmt->value) {
            stmt.return_stmt->value = desugar_expr_node(std::move(stmt.return_stmt->value));
        }
        break;
    case ast::StatementSyntaxKind::Assert:
        stmt.assert_stmt->condition = desugar_expr_node(std::move(stmt.assert_stmt->condition));
        break;
    case ast::StatementSyntaxKind::Expr:
        stmt.expr_stmt->expr = desugar_expr_node(std::move(stmt.expr_stmt->expr));
        break;
    case ast::StatementSyntaxKind::Goto:
        break; // no expressions
    }
}

void desugar_block_node(ast::BlockSyntax &block) {
    for (auto &stmt : block.statements) {
        desugar_stmt_node(*stmt);
    }
}

// ---------------------------------------------------------------------------
// Declaration-level desugaring helpers
// ---------------------------------------------------------------------------

void desugar_fn_decl_body(ast::FnDecl &decl) {
    for (auto &param : decl.params) {
        if (param->type)
            param->type = desugar_type_node(std::move(param->type));
    }
    if (decl.return_type)
        decl.return_type = desugar_type_node(std::move(decl.return_type));
    if (decl.body)
        desugar_block_node(*decl.body);
}

} // namespace

// ---------------------------------------------------------------------------
// Public DesugarPass interface
// ---------------------------------------------------------------------------

bool DesugarPass::run(ast::Program &program) const {
    for (auto &decl : program.declarations) {
        desugar_decl(*decl);
    }
    return true;
}

void DesugarPass::desugar_decl(ast::Decl &decl) {
    // Decl uses a class hierarchy (not a variant). Dispatch via dynamic_cast.
    if (auto *d = dynamic_cast<ast::ConstDecl *>(&decl)) {
        if (d->type)
            d->type = desugar_type_node(std::move(d->type));
        if (d->value)
            d->value = desugar_expr_node(std::move(d->value));
        return;
    }
    if (auto *d = dynamic_cast<ast::TypeAliasDecl *>(&decl)) {
        if (d->aliased_type) {
            d->aliased_type = desugar_type_node(std::move(d->aliased_type));
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::StructDecl *>(&decl)) {
        for (auto &field : d->fields) {
            if (field->type)
                field->type = desugar_type_node(std::move(field->type));
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::EnumDecl *>(&decl)) {
        for (auto &variant : d->variants) {
            for (auto &ptype : variant->payload) {
                ptype = desugar_type_node(std::move(ptype));
            }
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::FnDecl *>(&decl)) {
        desugar_fn_decl_body(*d);
        return;
    }
    if (auto *d = dynamic_cast<ast::TraitDecl *>(&decl)) {
        for (auto &item : d->items) {
            if (item->kind == ast::TraitItemKind::Fn) {
                for (auto &param : item->params) {
                    if (param->type) {
                        param->type = desugar_type_node(std::move(param->type));
                    }
                }
                if (item->return_type) {
                    item->return_type = desugar_type_node(std::move(item->return_type));
                }
            } else if (item->kind == ast::TraitItemKind::AssocType && item->assoc_type) {
                for (auto &bound : item->assoc_type->bounds) {
                    if (bound) {
                        bound = desugar_type_node(std::move(bound));
                    }
                }
                if (item->assoc_type->default_type) {
                    item->assoc_type->default_type =
                        desugar_type_node(std::move(item->assoc_type->default_type));
                }
            } else if (item->kind == ast::TraitItemKind::AssocConst && item->assoc_const) {
                if (item->assoc_const->type) {
                    item->assoc_const->type =
                        desugar_type_node(std::move(item->assoc_const->type));
                }
                if (item->assoc_const->default_value) {
                    item->assoc_const->default_value =
                        desugar_expr_node(std::move(item->assoc_const->default_value));
                }
            }
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::ImplDecl *>(&decl)) {
        if (d->target_type)
            d->target_type = desugar_type_node(std::move(d->target_type));
        if (d->trait_ref)
            d->trait_ref = desugar_type_node(std::move(d->trait_ref));
        for (auto &method : d->methods) {
            desugar_fn_decl_body(*method);
        }
        for (auto &assoc : d->assoc_items) {
            if (assoc && assoc->type) {
                assoc->type = desugar_type_node(std::move(assoc->type));
            }
        }
        for (auto &assoc_const : d->const_items) {
            if (!assoc_const) continue;
            if (assoc_const->type) {
                assoc_const->type = desugar_type_node(std::move(assoc_const->type));
            }
            if (assoc_const->value) {
                assoc_const->value = desugar_expr_node(std::move(assoc_const->value));
            }
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::CapabilityDecl *>(&decl)) {
        for (auto &param : d->params) {
            if (param->type)
                param->type = desugar_type_node(std::move(param->type));
        }
        if (d->return_type)
            d->return_type = desugar_type_node(std::move(d->return_type));
        return;
    }
    if (auto *d = dynamic_cast<ast::PredicateDecl *>(&decl)) {
        for (auto &param : d->params) {
            if (param->type)
                param->type = desugar_type_node(std::move(param->type));
        }
        return;
    }
    if (auto *d = dynamic_cast<ast::AgentDecl *>(&decl)) {
        if (d->input_type)
            d->input_type = desugar_type_node(std::move(d->input_type));
        if (d->context_type)
            d->context_type = desugar_type_node(std::move(d->context_type));
        if (d->output_type)
            d->output_type = desugar_type_node(std::move(d->output_type));
        // TODO: desugar transition when/assert bodies
        return;
    }
    if (auto *d = dynamic_cast<ast::WorkflowDecl *>(&decl)) {
        if (d->input_type)
            d->input_type = desugar_type_node(std::move(d->input_type));
        if (d->output_type)
            d->output_type = desugar_type_node(std::move(d->output_type));
        // TODO: desugar node bodies and return_value
        return;
    }
    // ModuleDecl, ImportDecl, ContractDecl, FlowDecl: handled elsewhere or
    // contain no sugar types/expressions that need desugaring here.
}

void DesugarPass::desugar_fn_decl(ast::FnDecl &decl) {
    desugar_fn_decl_body(decl);
}

void DesugarPass::desugar_struct_decl(ast::StructDecl &decl) {
    for (auto &field : decl.fields) {
        if (field->type)
            field->type = desugar_type_node(std::move(field->type));
    }
}

void DesugarPass::desugar_enum_decl(ast::EnumDecl &decl) {
    for (auto &variant : decl.variants) {
        for (auto &ptype : variant->payload) {
            ptype = desugar_type_node(std::move(ptype));
        }
    }
}

void DesugarPass::desugar_type_alias_decl(ast::TypeAliasDecl &decl) {
    if (decl.aliased_type) {
        decl.aliased_type = desugar_type_node(std::move(decl.aliased_type));
    }
}

void DesugarPass::desugar_trait_decl(ast::TraitDecl &decl) {
    for (auto &item : decl.items) {
        if (item->kind == ast::TraitItemKind::Fn) {
            for (auto &param : item->params) {
                if (param->type)
                    param->type = desugar_type_node(std::move(param->type));
            }
            if (item->return_type) {
                item->return_type = desugar_type_node(std::move(item->return_type));
            }
        } else if (item->kind == ast::TraitItemKind::AssocType && item->assoc_type) {
            for (auto &bound : item->assoc_type->bounds) {
                if (bound) bound = desugar_type_node(std::move(bound));
            }
            if (item->assoc_type->default_type) {
                item->assoc_type->default_type =
                    desugar_type_node(std::move(item->assoc_type->default_type));
            }
        } else if (item->kind == ast::TraitItemKind::AssocConst && item->assoc_const) {
            if (item->assoc_const->type) {
                item->assoc_const->type =
                    desugar_type_node(std::move(item->assoc_const->type));
            }
            if (item->assoc_const->default_value) {
                item->assoc_const->default_value =
                    desugar_expr_node(std::move(item->assoc_const->default_value));
            }
        }
    }
}

void DesugarPass::desugar_impl_decl(ast::ImplDecl &decl) {
    if (decl.target_type)
        decl.target_type = desugar_type_node(std::move(decl.target_type));
    if (decl.trait_ref)
        decl.trait_ref = desugar_type_node(std::move(decl.trait_ref));
    for (auto &method : decl.methods) {
        desugar_fn_decl_body(*method);
    }
    for (auto &assoc : decl.assoc_items) {
        if (assoc && assoc->type) {
            assoc->type = desugar_type_node(std::move(assoc->type));
        }
    }
    for (auto &assoc_const : decl.const_items) {
        if (!assoc_const) continue;
        if (assoc_const->type) {
            assoc_const->type = desugar_type_node(std::move(assoc_const->type));
        }
        if (assoc_const->value) {
            assoc_const->value = desugar_expr_node(std::move(assoc_const->value));
        }
    }
}

Owned<ast::TypeSyntax> DesugarPass::desugar_type(Owned<ast::TypeSyntax> type) {
    return desugar_type_node(std::move(type));
}

Owned<ast::ExprSyntax> DesugarPass::desugar_expr(Owned<ast::ExprSyntax> expr) {
    return desugar_expr_node(std::move(expr));
}

void DesugarPass::desugar_stmt(ast::StatementSyntax &stmt) {
    desugar_stmt_node(stmt);
}

void DesugarPass::desugar_block(ast::BlockSyntax &block) {
    desugar_block_node(block);
}

void DesugarPass::desugar_pattern(ast::PatternSyntax &) {
    // Patterns currently contain no type/expression sugar. This is a
    // no-op placeholder for future pattern type-ascription desugaring.
}

void DesugarPass::desugar_match_arm(ast::MatchArmSyntax &arm) {
    if (arm.pattern)
        desugar_pattern(*arm.pattern);
    if (arm.guard)
        arm.guard = desugar_expr_node(std::move(arm.guard));
    if (arm.body)
        arm.body = desugar_expr_node(std::move(arm.body));
}

} // namespace ahfl
