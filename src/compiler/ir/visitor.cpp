#include "ahfl/compiler/ir/visitor.hpp"

#include "base/support/overloaded.hpp"

#include <variant>

namespace ahfl::ir {

void ProgramVisitor::visit(const Program &program) {
    on_program(program);
    for (const auto &declaration : program.declarations) {
        visit_decl(declaration);
    }
}

void ProgramVisitor::on_program(const Program & /*program*/) {}
void ProgramVisitor::on_decl(const Decl & /*declaration*/) {}
void ProgramVisitor::on_expr(const Expr & /*expr*/) {}
void ProgramVisitor::on_temporal_expr(const TemporalExpr & /*expr*/) {}
void ProgramVisitor::on_statement(const Statement & /*statement*/) {}
void ProgramVisitor::on_workflow_node(const WorkflowNode & /*node*/) {}

void ProgramVisitor::visit_decl(const Decl &declaration) {
    on_decl(declaration);
    std::visit(Overloaded{
                   [&](const ConstDecl &value) {
                       if (value.value) {
                           visit_expr(*value.value);
                       }
                   },
                   [&](const StructDecl &value) {
                       for (const auto &field : value.fields) {
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
                           if (const auto *expr = std::get_if<ExprPtr>(&clause.value);
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
                           visit_block(handler.body);
                       }
                   },
                   [&](const WorkflowDecl &value) {
                       for (const auto &node : value.nodes) {
                           on_workflow_node(node);
                           if (node.input) {
                               visit_expr(*node.input);
                           }
                       }
                       for (const auto &expr : value.safety) {
                           if (expr) {
                               visit_temporal_expr(*expr);
                           }
                       }
                       for (const auto &expr : value.liveness) {
                           if (expr) {
                               visit_temporal_expr(*expr);
                           }
                       }
                       if (value.return_value) {
                           visit_expr(*value.return_value);
                       }
                   },
                   [](const auto &) {},
               },
               declaration);
}

void ProgramVisitor::visit_expr(const Expr &expr) {
    on_expr(expr);
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
                           if (argument) {
                               visit_expr(*argument);
                           }
                       }
                   },
                   [&](const StructLiteralExpr &value) {
                       for (const auto &field : value.fields) {
                           if (field.value) {
                               visit_expr(*field.value);
                           }
                       }
                   },
                   [&](const ListLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           if (item) {
                               visit_expr(*item);
                           }
                       }
                   },
                   [&](const SetLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           if (item) {
                               visit_expr(*item);
                           }
                       }
                   },
                   [&](const MapLiteralExpr &value) {
                       for (const auto &entry : value.entries) {
                           if (entry.key) {
                               visit_expr(*entry.key);
                           }
                           if (entry.value) {
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
                       if (value.rhs) {
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
                       if (value.index) {
                           visit_expr(*value.index);
                       }
                   },
                   [&](const GroupExpr &value) {
                       if (value.expr) {
                           visit_expr(*value.expr);
                       }
                   },
               },
               expr.node);
}

void ProgramVisitor::visit_temporal_expr(const TemporalExpr &expr) {
    on_temporal_expr(expr);
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
                       if (value.rhs) {
                           visit_temporal_expr(*value.rhs);
                       }
                   },
               },
               expr.node);
}

void ProgramVisitor::visit_statement(const Statement &statement) {
    on_statement(statement);
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
                       if (value.then_block) {
                           visit_block(*value.then_block);
                       }
                       if (value.else_block) {
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
}

void ProgramVisitor::visit_block(const Block &block) {
    for (const auto &statement : block.statements) {
        if (statement) {
            visit_statement(*statement);
        }
    }
}

bool ProgramRewriter::rewrite(Program &program) {
    bool modified = on_program(program);
    for (auto &declaration : program.declarations) {
        modified = rewrite_decl(declaration) || modified;
    }
    return modified;
}

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

bool ProgramRewriter::rewrite_decl(Decl &declaration) {
    bool modified = on_decl(declaration);
    modified = std::visit(Overloaded{
                              [&](ConstDecl &value) {
                                  return value.value != nullptr && rewrite_expr(*value.value);
                              },
                              [&](StructDecl &value) {
                                  bool changed = false;
                                  for (auto &field : value.fields) {
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
                                      if (auto *expr = std::get_if<ExprPtr>(&clause.value);
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
                                      changed = rewrite_block(handler.body) || changed;
                                  }
                                  return changed;
                              },
                              [&](WorkflowDecl &value) {
                                  bool changed = false;
                                  for (auto &node : value.nodes) {
                                      changed = on_workflow_node(node) || changed;
                                      if (node.input) {
                                          changed = rewrite_expr(*node.input) || changed;
                                      }
                                  }
                                  for (auto &expr : value.safety) {
                                      if (expr) {
                                          changed = rewrite_temporal_expr(*expr) || changed;
                                      }
                                  }
                                  for (auto &expr : value.liveness) {
                                      if (expr) {
                                          changed = rewrite_temporal_expr(*expr) || changed;
                                      }
                                  }
                                  if (value.return_value) {
                                      changed = rewrite_expr(*value.return_value) || changed;
                                  }
                                  return changed;
                              },
                              [](auto &) { return false; },
                          },
                          declaration) ||
               modified;
    return modified;
}

bool ProgramRewriter::rewrite_expr(Expr &expr) {
    bool modified = on_expr(expr);
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
                                      if (argument) {
                                          changed = rewrite_expr(*argument) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](StructLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &field : value.fields) {
                                      if (field.value) {
                                          changed = rewrite_expr(*field.value) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](ListLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &item : value.items) {
                                      if (item) {
                                          changed = rewrite_expr(*item) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](SetLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &item : value.items) {
                                      if (item) {
                                          changed = rewrite_expr(*item) || changed;
                                      }
                                  }
                                  return changed;
                              },
                              [&](MapLiteralExpr &value) {
                                  bool changed = false;
                                  for (auto &entry : value.entries) {
                                      if (entry.key) {
                                          changed = rewrite_expr(*entry.key) || changed;
                                      }
                                      if (entry.value) {
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
                                  if (value.rhs) {
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
                                  if (value.index) {
                                      changed = rewrite_expr(*value.index) || changed;
                                  }
                                  return changed;
                              },
                              [&](GroupExpr &value) {
                                  return value.expr != nullptr && rewrite_expr(*value.expr);
                              },
                          },
                          expr.node) ||
               modified;
    return modified;
}

bool ProgramRewriter::rewrite_temporal_expr(TemporalExpr &expr) {
    bool modified = on_temporal_expr(expr);
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
                           if (value.rhs) {
                               changed = rewrite_temporal_expr(*value.rhs) || changed;
                           }
                           return changed;
                       },
                   },
                   expr.node) ||
        modified;
    return modified;
}

bool ProgramRewriter::rewrite_statement(Statement &statement) {
    bool modified = on_statement(statement);
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
                           if (value.then_block) {
                               changed = rewrite_block(*value.then_block) || changed;
                           }
                           if (value.else_block) {
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
    return modified;
}

bool ProgramRewriter::rewrite_block(Block &block) {
    bool modified = false;
    for (auto &statement : block.statements) {
        if (statement) {
            modified = rewrite_statement(*statement) || modified;
        }
    }
    return modified;
}

} // namespace ahfl::ir
