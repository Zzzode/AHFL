
// Generated from grammar/AHFL.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "AHFLParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by AHFLParser.
 */
class  AHFLVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by AHFLParser.
   */
    virtual std::any visitProgram(AHFLParser::ProgramContext *context) = 0;

    virtual std::any visitTopLevelDecl(AHFLParser::TopLevelDeclContext *context) = 0;

    virtual std::any visitModuleDecl(AHFLParser::ModuleDeclContext *context) = 0;

    virtual std::any visitImportDecl(AHFLParser::ImportDeclContext *context) = 0;

    virtual std::any visitQualifiedIdent(AHFLParser::QualifiedIdentContext *context) = 0;

    virtual std::any visitQualifiedIdentList(AHFLParser::QualifiedIdentListContext *context) = 0;

    virtual std::any visitIdentList(AHFLParser::IdentListContext *context) = 0;

    virtual std::any visitIdentListOpt(AHFLParser::IdentListOptContext *context) = 0;

    virtual std::any visitQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext *context) = 0;

    virtual std::any visitType_(AHFLParser::Type_Context *context) = 0;

    virtual std::any visitPrimitiveType(AHFLParser::PrimitiveTypeContext *context) = 0;

    virtual std::any visitConstDecl(AHFLParser::ConstDeclContext *context) = 0;

    virtual std::any visitTypeAliasDecl(AHFLParser::TypeAliasDeclContext *context) = 0;

    virtual std::any visitStructDecl(AHFLParser::StructDeclContext *context) = 0;

    virtual std::any visitStructFieldDecl(AHFLParser::StructFieldDeclContext *context) = 0;

    virtual std::any visitEnumDecl(AHFLParser::EnumDeclContext *context) = 0;

    virtual std::any visitEnumVariant(AHFLParser::EnumVariantContext *context) = 0;

    virtual std::any visitCapabilityDecl(AHFLParser::CapabilityDeclContext *context) = 0;

    virtual std::any visitPredicateDecl(AHFLParser::PredicateDeclContext *context) = 0;

    virtual std::any visitParamList(AHFLParser::ParamListContext *context) = 0;

    virtual std::any visitParam(AHFLParser::ParamContext *context) = 0;

    virtual std::any visitAgentDecl(AHFLParser::AgentDeclContext *context) = 0;

    virtual std::any visitInputDecl(AHFLParser::InputDeclContext *context) = 0;

    virtual std::any visitContextDecl(AHFLParser::ContextDeclContext *context) = 0;

    virtual std::any visitOutputDecl(AHFLParser::OutputDeclContext *context) = 0;

    virtual std::any visitStatesDecl(AHFLParser::StatesDeclContext *context) = 0;

    virtual std::any visitInitialDecl(AHFLParser::InitialDeclContext *context) = 0;

    virtual std::any visitFinalDecl(AHFLParser::FinalDeclContext *context) = 0;

    virtual std::any visitCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext *context) = 0;

    virtual std::any visitQuotaDecl(AHFLParser::QuotaDeclContext *context) = 0;

    virtual std::any visitQuotaItem(AHFLParser::QuotaItemContext *context) = 0;

    virtual std::any visitTransitionDecl(AHFLParser::TransitionDeclContext *context) = 0;

    virtual std::any visitContractDecl(AHFLParser::ContractDeclContext *context) = 0;

    virtual std::any visitContractItem(AHFLParser::ContractItemContext *context) = 0;

    virtual std::any visitRequiresDecl(AHFLParser::RequiresDeclContext *context) = 0;

    virtual std::any visitEnsuresDecl(AHFLParser::EnsuresDeclContext *context) = 0;

    virtual std::any visitInvariantDecl(AHFLParser::InvariantDeclContext *context) = 0;

    virtual std::any visitForbidDecl(AHFLParser::ForbidDeclContext *context) = 0;

    virtual std::any visitFlowDecl(AHFLParser::FlowDeclContext *context) = 0;

    virtual std::any visitStateHandler(AHFLParser::StateHandlerContext *context) = 0;

    virtual std::any visitStatePolicy(AHFLParser::StatePolicyContext *context) = 0;

    virtual std::any visitStatePolicyItem(AHFLParser::StatePolicyItemContext *context) = 0;

    virtual std::any visitWorkflowDecl(AHFLParser::WorkflowDeclContext *context) = 0;

    virtual std::any visitWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext *context) = 0;

    virtual std::any visitWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext *context) = 0;

    virtual std::any visitWorkflowItem(AHFLParser::WorkflowItemContext *context) = 0;

    virtual std::any visitWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext *context) = 0;

    virtual std::any visitWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext *context) = 0;

    virtual std::any visitWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext *context) = 0;

    virtual std::any visitWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext *context) = 0;

    virtual std::any visitBlock(AHFLParser::BlockContext *context) = 0;

    virtual std::any visitStatement(AHFLParser::StatementContext *context) = 0;

    virtual std::any visitLetStmt(AHFLParser::LetStmtContext *context) = 0;

    virtual std::any visitAssignStmt(AHFLParser::AssignStmtContext *context) = 0;

    virtual std::any visitIfStmt(AHFLParser::IfStmtContext *context) = 0;

    virtual std::any visitGotoStmt(AHFLParser::GotoStmtContext *context) = 0;

    virtual std::any visitReturnStmt(AHFLParser::ReturnStmtContext *context) = 0;

    virtual std::any visitAssertStmt(AHFLParser::AssertStmtContext *context) = 0;

    virtual std::any visitExprStmt(AHFLParser::ExprStmtContext *context) = 0;

    virtual std::any visitLValue(AHFLParser::LValueContext *context) = 0;

    virtual std::any visitExpr(AHFLParser::ExprContext *context) = 0;

    virtual std::any visitImpliesExpr(AHFLParser::ImpliesExprContext *context) = 0;

    virtual std::any visitOrExpr(AHFLParser::OrExprContext *context) = 0;

    virtual std::any visitAndExpr(AHFLParser::AndExprContext *context) = 0;

    virtual std::any visitEqualityExpr(AHFLParser::EqualityExprContext *context) = 0;

    virtual std::any visitCompareExpr(AHFLParser::CompareExprContext *context) = 0;

    virtual std::any visitAddExpr(AHFLParser::AddExprContext *context) = 0;

    virtual std::any visitMulExpr(AHFLParser::MulExprContext *context) = 0;

    virtual std::any visitUnaryExpr(AHFLParser::UnaryExprContext *context) = 0;

    virtual std::any visitPostfixExpr(AHFLParser::PostfixExprContext *context) = 0;

    virtual std::any visitPrimaryExpr(AHFLParser::PrimaryExprContext *context) = 0;

    virtual std::any visitPathExpr(AHFLParser::PathExprContext *context) = 0;

    virtual std::any visitPathRoot(AHFLParser::PathRootContext *context) = 0;

    virtual std::any visitQualifiedValueExpr(AHFLParser::QualifiedValueExprContext *context) = 0;

    virtual std::any visitCallExpr(AHFLParser::CallExprContext *context) = 0;

    virtual std::any visitExprList(AHFLParser::ExprListContext *context) = 0;

    virtual std::any visitLiteral(AHFLParser::LiteralContext *context) = 0;

    virtual std::any visitIntegerLiteral(AHFLParser::IntegerLiteralContext *context) = 0;

    virtual std::any visitFloatLiteral(AHFLParser::FloatLiteralContext *context) = 0;

    virtual std::any visitDecimalLiteral(AHFLParser::DecimalLiteralContext *context) = 0;

    virtual std::any visitStringLiteral(AHFLParser::StringLiteralContext *context) = 0;

    virtual std::any visitDurationLiteral(AHFLParser::DurationLiteralContext *context) = 0;

    virtual std::any visitListLiteral(AHFLParser::ListLiteralContext *context) = 0;

    virtual std::any visitSetLiteral(AHFLParser::SetLiteralContext *context) = 0;

    virtual std::any visitMapLiteral(AHFLParser::MapLiteralContext *context) = 0;

    virtual std::any visitMapEntryList(AHFLParser::MapEntryListContext *context) = 0;

    virtual std::any visitMapEntry(AHFLParser::MapEntryContext *context) = 0;

    virtual std::any visitStructLiteral(AHFLParser::StructLiteralContext *context) = 0;

    virtual std::any visitStructInitList(AHFLParser::StructInitListContext *context) = 0;

    virtual std::any visitStructInit(AHFLParser::StructInitContext *context) = 0;

    virtual std::any visitConstExpr(AHFLParser::ConstExprContext *context) = 0;

    virtual std::any visitTemporalExpr(AHFLParser::TemporalExprContext *context) = 0;

    virtual std::any visitWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext *context) = 0;

    virtual std::any visitTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext *context) = 0;

    virtual std::any visitTemporalOrExpr(AHFLParser::TemporalOrExprContext *context) = 0;

    virtual std::any visitTemporalAndExpr(AHFLParser::TemporalAndExprContext *context) = 0;

    virtual std::any visitTemporalUntilExpr(AHFLParser::TemporalUntilExprContext *context) = 0;

    virtual std::any visitTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext *context) = 0;

    virtual std::any visitTemporalAtom(AHFLParser::TemporalAtomContext *context) = 0;


};

