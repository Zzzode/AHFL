#pragma once

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

/// Controls traversal behavior when returned from pre-visit hooks.
enum class VisitAction {
    Continue, // recurse into children
    Skip,     // do not recurse into children of this node
    Abort,    // stop traversal entirely
};

/// Return type for ProgramRewriter pre-visit hooks.
struct RewriteAction {
    VisitAction action{VisitAction::Continue};
    bool modified{false};
};

class ProgramVisitor {
  public:
    virtual ~ProgramVisitor() = default;

    void visit(const Program &program);

  protected:
    // Legacy hooks — called by the default visit_*_pre implementations.
    // Subclasses overriding these continue to work unchanged.
    virtual void on_program(const Program &program);
    virtual void on_decl(const Decl &declaration);
    virtual void on_expr(const Expr &expr);
    virtual void on_temporal_expr(const TemporalExpr &expr);
    virtual void on_statement(const Statement &statement);
    virtual void on_workflow_node(const WorkflowNode &node);

    // Pre/post hooks with VisitAction control flow.
    // Default visit_*_pre delegates to on_* and returns Continue.
    // Default visit_*_post is a no-op.
    virtual VisitAction visit_decl_pre(const Decl &declaration);
    virtual void visit_decl_post(const Decl &declaration);
    virtual VisitAction visit_expr_pre(const Expr &expr);
    virtual void visit_expr_post(const Expr &expr);
    virtual VisitAction visit_temporal_expr_pre(const TemporalExpr &expr);
    virtual void visit_temporal_expr_post(const TemporalExpr &expr);
    virtual VisitAction visit_statement_pre(const Statement &statement);
    virtual void visit_statement_post(const Statement &statement);
    virtual VisitAction visit_workflow_node_pre(const WorkflowNode &node);
    virtual void visit_workflow_node_post(const WorkflowNode &node);

  private:
    bool aborted_{false};

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
    // Legacy hooks — called by the default rewrite_*_pre implementations.
    virtual bool on_program(Program &program);
    virtual bool on_decl(Decl &declaration);
    virtual bool on_expr(Expr &expr);
    virtual bool on_temporal_expr(TemporalExpr &expr);
    virtual bool on_statement(Statement &statement);
    virtual bool on_workflow_node(WorkflowNode &node);

    // Pre/post hooks with VisitAction control flow.
    // Default rewrite_*_pre delegates to on_* and returns {Continue, modified}.
    // Default rewrite_*_post returns false (no modification).
    virtual RewriteAction rewrite_decl_pre(Decl &declaration);
    virtual bool rewrite_decl_post(Decl &declaration);
    virtual RewriteAction rewrite_expr_pre(Expr &expr);
    virtual bool rewrite_expr_post(Expr &expr);
    virtual RewriteAction rewrite_temporal_expr_pre(TemporalExpr &expr);
    virtual bool rewrite_temporal_expr_post(TemporalExpr &expr);
    virtual RewriteAction rewrite_statement_pre(Statement &statement);
    virtual bool rewrite_statement_post(Statement &statement);
    virtual RewriteAction rewrite_workflow_node_pre(WorkflowNode &node);
    virtual bool rewrite_workflow_node_post(WorkflowNode &node);

  private:
    bool aborted_{false};

    [[nodiscard]] bool rewrite_decl(Decl &declaration);
    [[nodiscard]] bool rewrite_expr(Expr &expr);
    [[nodiscard]] bool rewrite_temporal_expr(TemporalExpr &expr);
    [[nodiscard]] bool rewrite_statement(Statement &statement);
    [[nodiscard]] bool rewrite_block(Block &block);
};

} // namespace ahfl::ir
