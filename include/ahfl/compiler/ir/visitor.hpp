#pragma once

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

class ProgramVisitor {
  public:
    virtual ~ProgramVisitor() = default;

    void visit(const Program &program);

  protected:
    virtual void on_program(const Program &program);
    virtual void on_decl(const Decl &declaration);
    virtual void on_expr(const Expr &expr);
    virtual void on_temporal_expr(const TemporalExpr &expr);
    virtual void on_statement(const Statement &statement);
    virtual void on_workflow_node(const WorkflowNode &node);

  private:
    void visit_decl(const Decl &declaration);
    void visit_expr(const Expr &expr);
    void visit_temporal_expr(const TemporalExpr &expr);
    void visit_statement(const Statement &statement);
    void visit_block(const Block &block);
};

class ProgramRewriter {
  public:
    virtual ~ProgramRewriter() = default;

    [[nodiscard]] bool rewrite(Program &program);

  protected:
    virtual bool on_program(Program &program);
    virtual bool on_decl(Decl &declaration);
    virtual bool on_expr(Expr &expr);
    virtual bool on_temporal_expr(TemporalExpr &expr);
    virtual bool on_statement(Statement &statement);
    virtual bool on_workflow_node(WorkflowNode &node);

  private:
    [[nodiscard]] bool rewrite_decl(Decl &declaration);
    [[nodiscard]] bool rewrite_expr(Expr &expr);
    [[nodiscard]] bool rewrite_temporal_expr(TemporalExpr &expr);
    [[nodiscard]] bool rewrite_statement(Statement &statement);
    [[nodiscard]] bool rewrite_block(Block &block);
};

} // namespace ahfl::ir
