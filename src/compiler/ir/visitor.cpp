#include "ahfl/compiler/ir/visitor.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <variant>

namespace ahfl::ir {

// =============================================================================
// ProgramVisitor
// =============================================================================

void ProgramVisitor::visit(const Program &program) {
    aborted_ = false;
    on_program(program);
    for (const auto &declaration : program.declarations) {
        if (aborted_) {
            break;
        }
        visit_decl(declaration);
    }
}

// Legacy hook defaults (no-ops)
void ProgramVisitor::on_program(const Program & /*program*/) {}
void ProgramVisitor::on_decl(const Decl & /*declaration*/) {}
void ProgramVisitor::on_expr(const Expr & /*expr*/) {}
void ProgramVisitor::on_temporal_expr(const TemporalExpr & /*expr*/) {}
void ProgramVisitor::on_statement(const Statement & /*statement*/) {}
void ProgramVisitor::on_workflow_node(const WorkflowNode & /*node*/) {}

// Pre/post hook defaults — delegate to legacy hooks
VisitAction ProgramVisitor::visit_decl_pre(const Decl &declaration) {
    on_decl(declaration);
    return VisitAction::Continue;
}
void ProgramVisitor::visit_decl_post(const Decl & /*declaration*/) {}

VisitAction ProgramVisitor::visit_expr_pre(const Expr &expr) {
    on_expr(expr);
    return VisitAction::Continue;
}
void ProgramVisitor::visit_expr_post(const Expr & /*expr*/) {}

VisitAction ProgramVisitor::visit_temporal_expr_pre(const TemporalExpr &expr) {
    on_temporal_expr(expr);
    return VisitAction::Continue;
}
void ProgramVisitor::visit_temporal_expr_post(const TemporalExpr & /*expr*/) {}

VisitAction ProgramVisitor::visit_statement_pre(const Statement &statement) {
    on_statement(statement);
    return VisitAction::Continue;
}
void ProgramVisitor::visit_statement_post(const Statement & /*statement*/) {}

VisitAction ProgramVisitor::visit_workflow_node_pre(const WorkflowNode &node) {
    on_workflow_node(node);
    return VisitAction::Continue;
}
void ProgramVisitor::visit_workflow_node_post(const WorkflowNode & /*node*/) {}

// Traversal methods

void ProgramVisitor::visit_decl(const Decl &declaration) {
    const auto action = visit_decl_pre(declaration);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return;
    }
    if (action == VisitAction::Skip) {
        visit_decl_post(declaration);
        return;
    }

    std::visit(Overloaded{
                   [&](const ConstDecl &value) {
                       if (!aborted_ && value.value) {
                           visit_expr(*value.value);
                       }
                   },
                   [&](const StructDecl &value) {
                       for (const auto &field : value.fields) {
                           if (aborted_) {
                               break;
                           }
                           if (field.default_value) {
                               visit_expr(*field.default_value);
                           }
                       }
                   },
                   [&](const CapabilityDecl &) {},
                   [&](const PredicateDecl &) {},
                   [&](const AgentDecl &) {},
                   [&](const ContractDecl &value) {
                       for (const auto &clause : value.clauses) {
                           if (aborted_) {
                               break;
                           }
                           if (const auto *expr = std::get_if<ExprRef>(&clause.value);
                               expr != nullptr && *expr) {
                               visit_expr(**expr);
                           } else if (const auto *temporal =
                                          std::get_if<TemporalExprPtr>(&clause.value);
                                      temporal != nullptr && *temporal) {
                               visit_temporal_expr(**temporal);
                           }
                       }
                   },
                   [&](const FlowDecl &value) {
                       for (const auto &handler : value.state_handlers) {
                           if (aborted_) {
                               break;
                           }
                           visit_block(handler.body);
                       }
                   },
                   [&](const WorkflowDecl &value) {
                       for (const auto &node : value.nodes) {
                           if (aborted_) {
                               break;
                           }
                           const auto node_action = visit_workflow_node_pre(node);
                           if (node_action == VisitAction::Abort) {
                               aborted_ = true;
                               break;
                           }
                           if (node_action == VisitAction::Continue && node.input) {
                               visit_expr(*node.input);
                           }
                           if (!aborted_) {
                               visit_workflow_node_post(node);
                           }
                       }
                       for (const auto &expr : value.safety) {
                           if (aborted_) {
                               break;
                           }
                           if (expr) {
                               visit_temporal_expr(*expr);
                           }
                       }
                       for (const auto &expr : value.liveness) {
                           if (aborted_) {
                               break;
                           }
                           if (expr) {
                               visit_temporal_expr(*expr);
                           }
                       }
                       if (!aborted_ && value.return_value) {
                           visit_expr(*value.return_value);
                       }
                   },
                   [](const auto &) {},
               },
               declaration);

    if (!aborted_) {
        visit_decl_post(declaration);
    }
}

void ProgramVisitor::visit_expr(const Expr &expr) {
    const auto action = visit_expr_pre(expr);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return;
    }
    if (action == VisitAction::Skip) {
        visit_expr_post(expr);
        return;
    }

    std::visit(Overloaded{
                   [](const NoneLiteralExpr &) {},
                   [](const BoolLiteralExpr &) {},
                   [](const IntegerLiteralExpr &) {},
                   [](const FloatLiteralExpr &) {},
                   [](const DecimalLiteralExpr &) {},
                   [](const StringLiteralExpr &) {},
                   [](const DurationLiteralExpr &) {},
                   [&](const SomeExpr &value) {
                       if (value.value) {
                           visit_expr(*value.value);
                       }
                   },
                   [](const PathExpr &) {},
                   [](const QualifiedValueExpr &) {},
                   [&](const CallExpr &value) {
                       for (const auto &argument : value.arguments) {
                           if (aborted_) {
                               break;
                           }
                           if (argument) {
                               visit_expr(*argument);
                           }
                       }
                   },
                   [&](const StructLiteralExpr &value) {
                       for (const auto &field : value.fields) {
                           if (aborted_) {
                               break;
                           }
                           if (field.value) {
                               visit_expr(*field.value);
                           }
                       }
                   },
                   [&](const ListLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           if (aborted_) {
                               break;
                           }
                           if (item) {
                               visit_expr(*item);
                           }
                       }
                   },
                   [&](const SetLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           if (aborted_) {
                               break;
                           }
                           if (item) {
                               visit_expr(*item);
                           }
                       }
                   },
                   [&](const MapLiteralExpr &value) {
                       for (const auto &entry : value.entries) {
                           if (aborted_) {
                               break;
                           }
                           if (entry.key) {
                               visit_expr(*entry.key);
                           }
                           if (!aborted_ && entry.value) {
                               visit_expr(*entry.value);
                           }
                       }
                   },
                   [&](const UnaryExpr &value) {
                       if (value.operand) {
                           visit_expr(*value.operand);
                       }
                   },
                   [&](const BinaryExpr &value) {
                       if (value.lhs) {
                           visit_expr(*value.lhs);
                       }
                       if (!aborted_ && value.rhs) {
                           visit_expr(*value.rhs);
                       }
                   },
                   [&](const MemberAccessExpr &value) {
                       if (value.base) {
                           visit_expr(*value.base);
                       }
                   },
                   [&](const IndexAccessExpr &value) {
                       if (value.base) {
                           visit_expr(*value.base);
                       }
                       if (!aborted_ && value.index) {
                           visit_expr(*value.index);
                       }
                   },
               },
               expr.node);

    if (!aborted_) {
        visit_expr_post(expr);
    }
}

void ProgramVisitor::visit_temporal_expr(const TemporalExpr &expr) {
    const auto action = visit_temporal_expr_pre(expr);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return;
    }
    if (action == VisitAction::Skip) {
        visit_temporal_expr_post(expr);
        return;
    }

    std::visit(Overloaded{
                   [&](const EmbeddedTemporalExpr &value) {
                       if (value.expr) {
                           visit_expr(*value.expr);
                       }
                   },
                   [](const CalledTemporalExpr &) {},
                   [](const InStateTemporalExpr &) {},
                   [](const RunningTemporalExpr &) {},
                   [](const CompletedTemporalExpr &) {},
                   [&](const TemporalUnaryExpr &value) {
                       if (value.operand) {
                           visit_temporal_expr(*value.operand);
                       }
                   },
                   [&](const TemporalBinaryExpr &value) {
                       if (value.lhs) {
                           visit_temporal_expr(*value.lhs);
                       }
                       if (!aborted_ && value.rhs) {
                           visit_temporal_expr(*value.rhs);
                       }
                   },
               },
               expr.node);

    if (!aborted_) {
        visit_temporal_expr_post(expr);
    }
}

void ProgramVisitor::visit_statement(const Statement &statement) {
    const auto action = visit_statement_pre(statement);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return;
    }
    if (action == VisitAction::Skip) {
        visit_statement_post(statement);
        return;
    }

    std::visit(Overloaded{
                   [&](const LetStatement &value) {
                       if (value.initializer) {
                           visit_expr(*value.initializer);
                       }
                   },
                   [&](const AssignStatement &value) {
                       if (value.value) {
                           visit_expr(*value.value);
                       }
                   },
                   [&](const IfStatement &value) {
                       if (value.condition) {
                           visit_expr(*value.condition);
                       }
                       if (!aborted_ && value.then_block) {
                           visit_block(*value.then_block);
                       }
                       if (!aborted_ && value.else_block) {
                           visit_block(*value.else_block);
                       }
                   },
                   [](const GotoStatement &) {},
                   [&](const ReturnStatement &value) {
                       if (value.value) {
                           visit_expr(*value.value);
                       }
                   },
                   [&](const AssertStatement &value) {
                       if (value.condition) {
                           visit_expr(*value.condition);
                       }
                   },
                   [&](const ExprStatement &value) {
                       if (value.expr) {
                           visit_expr(*value.expr);
                       }
                   },
               },
               statement.node);

    if (!aborted_) {
        visit_statement_post(statement);
    }
}

void ProgramVisitor::visit_block(const Block &block) {
    for (const auto &statement : block.statements) {
        if (aborted_) {
            break;
        }
        if (statement) {
            visit_statement(*statement);
        }
    }
}

// =============================================================================
// ProgramRewriter
// =============================================================================

bool ProgramRewriter::rewrite(Program &program) {
    aborted_ = false;
    bool modified = on_program(program);
    for (auto &declaration : program.declarations) {
        if (aborted_) {
            break;
        }
        modified = rewrite_decl(declaration) || modified;
    }
    return modified;
}

// Legacy hook defaults
bool ProgramRewriter::on_program(Program & /*program*/) {
    return false;
}
bool ProgramRewriter::on_decl(Decl & /*declaration*/) {
    return false;
}
bool ProgramRewriter::on_expr(Expr & /*expr*/) {
    return false;
}
bool ProgramRewriter::on_temporal_expr(TemporalExpr & /*expr*/) {
    return false;
}
bool ProgramRewriter::on_statement(Statement & /*statement*/) {
    return false;
}
bool ProgramRewriter::on_workflow_node(WorkflowNode & /*node*/) {
    return false;
}

// Pre/post hook defaults — delegate to legacy hooks
RewriteAction ProgramRewriter::rewrite_decl_pre(Decl &declaration) {
    return {VisitAction::Continue, on_decl(declaration)};
}
bool ProgramRewriter::rewrite_decl_post(Decl & /*declaration*/) {
    return false;
}

RewriteAction ProgramRewriter::rewrite_expr_pre(Expr &expr) {
    return {VisitAction::Continue, on_expr(expr)};
}
bool ProgramRewriter::rewrite_expr_post(Expr & /*expr*/) {
    return false;
}

RewriteAction ProgramRewriter::rewrite_temporal_expr_pre(TemporalExpr &expr) {
    return {VisitAction::Continue, on_temporal_expr(expr)};
}
bool ProgramRewriter::rewrite_temporal_expr_post(TemporalExpr & /*expr*/) {
    return false;
}

RewriteAction ProgramRewriter::rewrite_statement_pre(Statement &statement) {
    return {VisitAction::Continue, on_statement(statement)};
}
bool ProgramRewriter::rewrite_statement_post(Statement & /*statement*/) {
    return false;
}

RewriteAction ProgramRewriter::rewrite_workflow_node_pre(WorkflowNode &node) {
    return {VisitAction::Continue, on_workflow_node(node)};
}
bool ProgramRewriter::rewrite_workflow_node_post(WorkflowNode & /*node*/) {
    return false;
}

// Traversal methods

bool ProgramRewriter::rewrite_decl(Decl &declaration) {
    auto [action, modified] = rewrite_decl_pre(declaration);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return modified;
    }
    if (action == VisitAction::Skip) {
        modified = rewrite_decl_post(declaration) || modified;
        return modified;
    }

    modified = std::visit(Overloaded{
                              [&](ConstDecl &value) {
                                  return value.value != nullptr && rewrite_expr(*value.value);
                              },
                              [&](StructDecl &value) {
                                  bool changed = false;
                                  for (auto &field : value.fields) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (field.default_value) {
                                          changed = rewrite_expr(*field.default_value) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [](CapabilityDecl &) { return false; },
                              [](PredicateDecl &) { return false; },
                              [](AgentDecl &) { return false; },
                              [&](ContractDecl &value) {
                                  bool changed = false;
                                  for (auto &clause : value.clauses) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (auto *expr = std::get_if<ExprRef>(&clause.value);
                                          expr != nullptr && *expr) {
                                          changed = rewrite_expr(**expr) || changed;
                                      } else if (auto *temporal =
                                                     std::get_if<TemporalExprPtr>(&clause.value);
                                                 temporal != nullptr && *temporal) {
                                          changed = rewrite_temporal_expr(**temporal) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](FlowDecl &value) {
                                  bool changed = false;
                                  for (auto &handler : value.state_handlers) {
                                      if (aborted_) {
                                          break;
                                      }
                                      changed = rewrite_block(handler.body) || changed;
                                  }
                                  return changed;
                              },
                              [&](WorkflowDecl &value) {
                                  bool changed = false;
                                  for (auto &node : value.nodes) {
                                      if (aborted_) {
                                          break;
                                      }
                                      auto [node_action, node_modified] =
                                          rewrite_workflow_node_pre(node);
                                      changed = node_modified || changed;
                                      if (node_action == VisitAction::Abort) {
                                          aborted_ = true;
                                          break;
                                      }
                                      if (node_action == VisitAction::Continue && node.input) {
                                          changed = rewrite_expr(*node.input) || changed;
                                      }
                                      if (!aborted_) {
                                          changed = rewrite_workflow_node_post(node) || changed;
                                      }
                                  }
                                  for (auto &expr : value.safety) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (expr) {
                                          changed = rewrite_temporal_expr(*expr) || changed;
                                      }
                                  }
                                  for (auto &expr : value.liveness) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (expr) {
                                          changed = rewrite_temporal_expr(*expr) || changed;
                                      }
                                  }
                                  if (!aborted_ && value.return_value) {
                                      changed = rewrite_expr(*value.return_value) || changed;
                                  }
                                  return changed;
                              },
                              [](auto &) { return false; },
                          },
                          declaration) ||
               modified;

    if (!aborted_) {
        modified = rewrite_decl_post(declaration) || modified;
    }
    return modified;
}

bool ProgramRewriter::rewrite_expr(Expr &expr) {
    auto [action, modified] = rewrite_expr_pre(expr);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return modified;
    }
    if (action == VisitAction::Skip) {
        modified = rewrite_expr_post(expr) || modified;
        return modified;
    }

    modified = std::visit(Overloaded{
                              [](NoneLiteralExpr &) { return false; },
                              [](BoolLiteralExpr &) { return false; },
                              [](IntegerLiteralExpr &) { return false; },
                              [](FloatLiteralExpr &) { return false; },
                              [](DecimalLiteralExpr &) { return false; },
                              [](StringLiteralExpr &) { return false; },
                              [](DurationLiteralExpr &) { return false; },
                              [&](SomeExpr &value) {
                                  return value.value != nullptr && rewrite_expr(*value.value);
                              },
                              [](PathExpr &) { return false; },
                              [](QualifiedValueExpr &) { return false; },
                              [&](CallExpr &value) {
                                  bool changed = false;
                                  for (auto &argument : value.arguments) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (argument) {
                                          changed = rewrite_expr(*argument) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](StructLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &field : value.fields) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (field.value) {
                                          changed = rewrite_expr(*field.value) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](ListLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &item : value.items) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (item) {
                                          changed = rewrite_expr(*item) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](SetLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &item : value.items) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (item) {
                                          changed = rewrite_expr(*item) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](MapLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &entry : value.entries) {
                                      if (aborted_) {
                                          break;
                                      }
                                      if (entry.key) {
                                          changed = rewrite_expr(*entry.key) || changed;
                                      }
                                      if (!aborted_ && entry.value) {
                                          changed = rewrite_expr(*entry.value) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](UnaryExpr &value) {
                                  return value.operand != nullptr && rewrite_expr(*value.operand);
                              },
                              [&](BinaryExpr &value) {
                                  bool changed = false;
                                  if (value.lhs) {
                                      changed = rewrite_expr(*value.lhs) || changed;
                                  }
                                  if (!aborted_ && value.rhs) {
                                      changed = rewrite_expr(*value.rhs) || changed;
                                  }
                                  return changed;
                              },
                              [&](MemberAccessExpr &value) {
                                  return value.base != nullptr && rewrite_expr(*value.base);
                              },
                              [&](IndexAccessExpr &value) {
                                  bool changed = false;
                                  if (value.base) {
                                      changed = rewrite_expr(*value.base) || changed;
                                  }
                                  if (!aborted_ && value.index) {
                                      changed = rewrite_expr(*value.index) || changed;
                                  }
                                  return changed;
                              },
                          },
                          expr.node) ||
               modified;

    if (!aborted_) {
        modified = rewrite_expr_post(expr) || modified;
    }
    return modified;
}

bool ProgramRewriter::rewrite_temporal_expr(TemporalExpr &expr) {
    auto [action, modified] = rewrite_temporal_expr_pre(expr);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return modified;
    }
    if (action == VisitAction::Skip) {
        modified = rewrite_temporal_expr_post(expr) || modified;
        return modified;
    }

    modified =
        std::visit(Overloaded{
                       [&](EmbeddedTemporalExpr &value) {
                           return value.expr != nullptr && rewrite_expr(*value.expr);
                       },
                       [](CalledTemporalExpr &) { return false; },
                       [](InStateTemporalExpr &) { return false; },
                       [](RunningTemporalExpr &) { return false; },
                       [](CompletedTemporalExpr &) { return false; },
                       [&](TemporalUnaryExpr &value) {
                           return value.operand != nullptr && rewrite_temporal_expr(*value.operand);
                       },
                       [&](TemporalBinaryExpr &value) {
                           bool changed = false;
                           if (value.lhs) {
                               changed = rewrite_temporal_expr(*value.lhs) || changed;
                           }
                           if (!aborted_ && value.rhs) {
                               changed = rewrite_temporal_expr(*value.rhs) || changed;
                           }
                           return changed;
                       },
                   },
                   expr.node) ||
        modified;

    if (!aborted_) {
        modified = rewrite_temporal_expr_post(expr) || modified;
    }
    return modified;
}

bool ProgramRewriter::rewrite_statement(Statement &statement) {
    auto [action, modified] = rewrite_statement_pre(statement);
    if (action == VisitAction::Abort) {
        aborted_ = true;
        return modified;
    }
    if (action == VisitAction::Skip) {
        modified = rewrite_statement_post(statement) || modified;
        return modified;
    }

    modified =
        std::visit(Overloaded{
                       [&](LetStatement &value) {
                           return value.initializer != nullptr && rewrite_expr(*value.initializer);
                       },
                       [&](AssignStatement &value) {
                           return value.value != nullptr && rewrite_expr(*value.value);
                       },
                       [&](IfStatement &value) {
                           bool changed = false;
                           if (value.condition) {
                               changed = rewrite_expr(*value.condition) || changed;
                           }
                           if (!aborted_ && value.then_block) {
                               changed = rewrite_block(*value.then_block) || changed;
                           }
                           if (!aborted_ && value.else_block) {
                               changed = rewrite_block(*value.else_block) || changed;
                           }
                           return changed;
                       },
                       [](GotoStatement &) { return false; },
                       [&](ReturnStatement &value) {
                           return value.value != nullptr && rewrite_expr(*value.value);
                       },
                       [&](AssertStatement &value) {
                           return value.condition != nullptr && rewrite_expr(*value.condition);
                       },
                       [&](ExprStatement &value) {
                           return value.expr != nullptr && rewrite_expr(*value.expr);
                       },
                   },
                   statement.node) ||
        modified;

    if (!aborted_) {
        modified = rewrite_statement_post(statement) || modified;
    }
    return modified;
}

bool ProgramRewriter::rewrite_block(Block &block) {
    bool modified = false;
    for (auto &statement : block.statements) {
        if (aborted_) {
            break;
        }
        if (statement) {
            modified = rewrite_statement(*statement) || modified;
        }
    }
    return modified;
}

} // namespace ahfl::ir
