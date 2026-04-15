
// Generated from grammar/AHFL.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "AHFLVisitor.h"


/**
 * This class provides an empty implementation of AHFLVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  AHFLBaseVisitor : public AHFLVisitor {
public:

  virtual std::any visitProgram(AHFLParser::ProgramContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTopLevelDecl(AHFLParser::TopLevelDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitModuleDecl(AHFLParser::ModuleDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportDecl(AHFLParser::ImportDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQualifiedIdent(AHFLParser::QualifiedIdentContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQualifiedIdentList(AHFLParser::QualifiedIdentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentList(AHFLParser::IdentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentListOpt(AHFLParser::IdentListOptContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitType_(AHFLParser::Type_Context *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimitiveType(AHFLParser::PrimitiveTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDecl(AHFLParser::ConstDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeAliasDecl(AHFLParser::TypeAliasDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructDecl(AHFLParser::StructDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructFieldDecl(AHFLParser::StructFieldDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEnumDecl(AHFLParser::EnumDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEnumVariant(AHFLParser::EnumVariantContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCapabilityDecl(AHFLParser::CapabilityDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPredicateDecl(AHFLParser::PredicateDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParamList(AHFLParser::ParamListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParam(AHFLParser::ParamContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAgentDecl(AHFLParser::AgentDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInputDecl(AHFLParser::InputDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitContextDecl(AHFLParser::ContextDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOutputDecl(AHFLParser::OutputDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStatesDecl(AHFLParser::StatesDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInitialDecl(AHFLParser::InitialDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFinalDecl(AHFLParser::FinalDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQuotaDecl(AHFLParser::QuotaDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQuotaItem(AHFLParser::QuotaItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTransitionDecl(AHFLParser::TransitionDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitContractDecl(AHFLParser::ContractDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitContractItem(AHFLParser::ContractItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRequiresDecl(AHFLParser::RequiresDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEnsuresDecl(AHFLParser::EnsuresDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInvariantDecl(AHFLParser::InvariantDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForbidDecl(AHFLParser::ForbidDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFlowDecl(AHFLParser::FlowDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStateHandler(AHFLParser::StateHandlerContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStatePolicy(AHFLParser::StatePolicyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStatePolicyItem(AHFLParser::StatePolicyItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowDecl(AHFLParser::WorkflowDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowItem(AHFLParser::WorkflowItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlock(AHFLParser::BlockContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStatement(AHFLParser::StatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLetStmt(AHFLParser::LetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssignStmt(AHFLParser::AssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStmt(AHFLParser::IfStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGotoStmt(AHFLParser::GotoStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReturnStmt(AHFLParser::ReturnStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssertStmt(AHFLParser::AssertStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprStmt(AHFLParser::ExprStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLValue(AHFLParser::LValueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpr(AHFLParser::ExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImpliesExpr(AHFLParser::ImpliesExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOrExpr(AHFLParser::OrExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAndExpr(AHFLParser::AndExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqualityExpr(AHFLParser::EqualityExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCompareExpr(AHFLParser::CompareExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddExpr(AHFLParser::AddExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulExpr(AHFLParser::MulExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryExpr(AHFLParser::UnaryExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixExpr(AHFLParser::PostfixExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryExpr(AHFLParser::PrimaryExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPathExpr(AHFLParser::PathExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPathRoot(AHFLParser::PathRootContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQualifiedValueExpr(AHFLParser::QualifiedValueExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCallExpr(AHFLParser::CallExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprList(AHFLParser::ExprListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLiteral(AHFLParser::LiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIntegerLiteral(AHFLParser::IntegerLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFloatLiteral(AHFLParser::FloatLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDecimalLiteral(AHFLParser::DecimalLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringLiteral(AHFLParser::StringLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDurationLiteral(AHFLParser::DurationLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListLiteral(AHFLParser::ListLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetLiteral(AHFLParser::SetLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapLiteral(AHFLParser::MapLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntryList(AHFLParser::MapEntryListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntry(AHFLParser::MapEntryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructLiteral(AHFLParser::StructLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructInitList(AHFLParser::StructInitListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructInit(AHFLParser::StructInitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstExpr(AHFLParser::ConstExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalExpr(AHFLParser::TemporalExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalOrExpr(AHFLParser::TemporalOrExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalAndExpr(AHFLParser::TemporalAndExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalUntilExpr(AHFLParser::TemporalUntilExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTemporalAtom(AHFLParser::TemporalAtomContext *ctx) override {
    return visitChildren(ctx);
  }


};

