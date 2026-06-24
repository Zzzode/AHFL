
// Generated from AHFL.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "AHFLParser.h"


/**
 * This interface defines an abstract listener for a parse tree produced by AHFLParser.
 */
class  AHFLListener : public antlr4::tree::ParseTreeListener {
public:

  virtual void enterProgram(AHFLParser::ProgramContext *ctx) = 0;
  virtual void exitProgram(AHFLParser::ProgramContext *ctx) = 0;

  virtual void enterTopLevelDecl(AHFLParser::TopLevelDeclContext *ctx) = 0;
  virtual void exitTopLevelDecl(AHFLParser::TopLevelDeclContext *ctx) = 0;

  virtual void enterModuleDecl(AHFLParser::ModuleDeclContext *ctx) = 0;
  virtual void exitModuleDecl(AHFLParser::ModuleDeclContext *ctx) = 0;

  virtual void enterImportDecl(AHFLParser::ImportDeclContext *ctx) = 0;
  virtual void exitImportDecl(AHFLParser::ImportDeclContext *ctx) = 0;

  virtual void enterQualifiedIdent(AHFLParser::QualifiedIdentContext *ctx) = 0;
  virtual void exitQualifiedIdent(AHFLParser::QualifiedIdentContext *ctx) = 0;

  virtual void enterQualifiedIdentList(AHFLParser::QualifiedIdentListContext *ctx) = 0;
  virtual void exitQualifiedIdentList(AHFLParser::QualifiedIdentListContext *ctx) = 0;

  virtual void enterIdentList(AHFLParser::IdentListContext *ctx) = 0;
  virtual void exitIdentList(AHFLParser::IdentListContext *ctx) = 0;

  virtual void enterIdentListOpt(AHFLParser::IdentListOptContext *ctx) = 0;
  virtual void exitIdentListOpt(AHFLParser::IdentListOptContext *ctx) = 0;

  virtual void enterQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext *ctx) = 0;
  virtual void exitQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext *ctx) = 0;

  virtual void enterType_(AHFLParser::Type_Context *ctx) = 0;
  virtual void exitType_(AHFLParser::Type_Context *ctx) = 0;

  virtual void enterPrimitiveType(AHFLParser::PrimitiveTypeContext *ctx) = 0;
  virtual void exitPrimitiveType(AHFLParser::PrimitiveTypeContext *ctx) = 0;

  virtual void enterConstDecl(AHFLParser::ConstDeclContext *ctx) = 0;
  virtual void exitConstDecl(AHFLParser::ConstDeclContext *ctx) = 0;

  virtual void enterTypeAliasDecl(AHFLParser::TypeAliasDeclContext *ctx) = 0;
  virtual void exitTypeAliasDecl(AHFLParser::TypeAliasDeclContext *ctx) = 0;

  virtual void enterStructDecl(AHFLParser::StructDeclContext *ctx) = 0;
  virtual void exitStructDecl(AHFLParser::StructDeclContext *ctx) = 0;

  virtual void enterStructFieldDecl(AHFLParser::StructFieldDeclContext *ctx) = 0;
  virtual void exitStructFieldDecl(AHFLParser::StructFieldDeclContext *ctx) = 0;

  virtual void enterEnumDecl(AHFLParser::EnumDeclContext *ctx) = 0;
  virtual void exitEnumDecl(AHFLParser::EnumDeclContext *ctx) = 0;

  virtual void enterEnumVariant(AHFLParser::EnumVariantContext *ctx) = 0;
  virtual void exitEnumVariant(AHFLParser::EnumVariantContext *ctx) = 0;

  virtual void enterCapabilityDecl(AHFLParser::CapabilityDeclContext *ctx) = 0;
  virtual void exitCapabilityDecl(AHFLParser::CapabilityDeclContext *ctx) = 0;

  virtual void enterCapabilityEffectBlock(AHFLParser::CapabilityEffectBlockContext *ctx) = 0;
  virtual void exitCapabilityEffectBlock(AHFLParser::CapabilityEffectBlockContext *ctx) = 0;

  virtual void enterCapabilityEffectItem(AHFLParser::CapabilityEffectItemContext *ctx) = 0;
  virtual void exitCapabilityEffectItem(AHFLParser::CapabilityEffectItemContext *ctx) = 0;

  virtual void enterCapabilityEffectKind(AHFLParser::CapabilityEffectKindContext *ctx) = 0;
  virtual void exitCapabilityEffectKind(AHFLParser::CapabilityEffectKindContext *ctx) = 0;

  virtual void enterCapabilityReceiptMode(AHFLParser::CapabilityReceiptModeContext *ctx) = 0;
  virtual void exitCapabilityReceiptMode(AHFLParser::CapabilityReceiptModeContext *ctx) = 0;

  virtual void enterCapabilityRetryMode(AHFLParser::CapabilityRetryModeContext *ctx) = 0;
  virtual void exitCapabilityRetryMode(AHFLParser::CapabilityRetryModeContext *ctx) = 0;

  virtual void enterPredicateDecl(AHFLParser::PredicateDeclContext *ctx) = 0;
  virtual void exitPredicateDecl(AHFLParser::PredicateDeclContext *ctx) = 0;

  virtual void enterParamList(AHFLParser::ParamListContext *ctx) = 0;
  virtual void exitParamList(AHFLParser::ParamListContext *ctx) = 0;

  virtual void enterParam(AHFLParser::ParamContext *ctx) = 0;
  virtual void exitParam(AHFLParser::ParamContext *ctx) = 0;

  virtual void enterAgentDecl(AHFLParser::AgentDeclContext *ctx) = 0;
  virtual void exitAgentDecl(AHFLParser::AgentDeclContext *ctx) = 0;

  virtual void enterInputDecl(AHFLParser::InputDeclContext *ctx) = 0;
  virtual void exitInputDecl(AHFLParser::InputDeclContext *ctx) = 0;

  virtual void enterContextDecl(AHFLParser::ContextDeclContext *ctx) = 0;
  virtual void exitContextDecl(AHFLParser::ContextDeclContext *ctx) = 0;

  virtual void enterOutputDecl(AHFLParser::OutputDeclContext *ctx) = 0;
  virtual void exitOutputDecl(AHFLParser::OutputDeclContext *ctx) = 0;

  virtual void enterStatesDecl(AHFLParser::StatesDeclContext *ctx) = 0;
  virtual void exitStatesDecl(AHFLParser::StatesDeclContext *ctx) = 0;

  virtual void enterInitialDecl(AHFLParser::InitialDeclContext *ctx) = 0;
  virtual void exitInitialDecl(AHFLParser::InitialDeclContext *ctx) = 0;

  virtual void enterFinalDecl(AHFLParser::FinalDeclContext *ctx) = 0;
  virtual void exitFinalDecl(AHFLParser::FinalDeclContext *ctx) = 0;

  virtual void enterCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext *ctx) = 0;
  virtual void exitCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext *ctx) = 0;

  virtual void enterQuotaDecl(AHFLParser::QuotaDeclContext *ctx) = 0;
  virtual void exitQuotaDecl(AHFLParser::QuotaDeclContext *ctx) = 0;

  virtual void enterQuotaItem(AHFLParser::QuotaItemContext *ctx) = 0;
  virtual void exitQuotaItem(AHFLParser::QuotaItemContext *ctx) = 0;

  virtual void enterTransitionDecl(AHFLParser::TransitionDeclContext *ctx) = 0;
  virtual void exitTransitionDecl(AHFLParser::TransitionDeclContext *ctx) = 0;

  virtual void enterContractDecl(AHFLParser::ContractDeclContext *ctx) = 0;
  virtual void exitContractDecl(AHFLParser::ContractDeclContext *ctx) = 0;

  virtual void enterContractItem(AHFLParser::ContractItemContext *ctx) = 0;
  virtual void exitContractItem(AHFLParser::ContractItemContext *ctx) = 0;

  virtual void enterRequiresDecl(AHFLParser::RequiresDeclContext *ctx) = 0;
  virtual void exitRequiresDecl(AHFLParser::RequiresDeclContext *ctx) = 0;

  virtual void enterEnsuresDecl(AHFLParser::EnsuresDeclContext *ctx) = 0;
  virtual void exitEnsuresDecl(AHFLParser::EnsuresDeclContext *ctx) = 0;

  virtual void enterInvariantDecl(AHFLParser::InvariantDeclContext *ctx) = 0;
  virtual void exitInvariantDecl(AHFLParser::InvariantDeclContext *ctx) = 0;

  virtual void enterForbidDecl(AHFLParser::ForbidDeclContext *ctx) = 0;
  virtual void exitForbidDecl(AHFLParser::ForbidDeclContext *ctx) = 0;

  virtual void enterFlowDecl(AHFLParser::FlowDeclContext *ctx) = 0;
  virtual void exitFlowDecl(AHFLParser::FlowDeclContext *ctx) = 0;

  virtual void enterStateHandler(AHFLParser::StateHandlerContext *ctx) = 0;
  virtual void exitStateHandler(AHFLParser::StateHandlerContext *ctx) = 0;

  virtual void enterStatePolicy(AHFLParser::StatePolicyContext *ctx) = 0;
  virtual void exitStatePolicy(AHFLParser::StatePolicyContext *ctx) = 0;

  virtual void enterStatePolicyItem(AHFLParser::StatePolicyItemContext *ctx) = 0;
  virtual void exitStatePolicyItem(AHFLParser::StatePolicyItemContext *ctx) = 0;

  virtual void enterWorkflowDecl(AHFLParser::WorkflowDeclContext *ctx) = 0;
  virtual void exitWorkflowDecl(AHFLParser::WorkflowDeclContext *ctx) = 0;

  virtual void enterWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext *ctx) = 0;
  virtual void exitWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext *ctx) = 0;

  virtual void enterWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext *ctx) = 0;
  virtual void exitWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext *ctx) = 0;

  virtual void enterWorkflowItem(AHFLParser::WorkflowItemContext *ctx) = 0;
  virtual void exitWorkflowItem(AHFLParser::WorkflowItemContext *ctx) = 0;

  virtual void enterWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext *ctx) = 0;
  virtual void exitWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext *ctx) = 0;

  virtual void enterWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext *ctx) = 0;
  virtual void exitWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext *ctx) = 0;

  virtual void enterWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext *ctx) = 0;
  virtual void exitWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext *ctx) = 0;

  virtual void enterWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext *ctx) = 0;
  virtual void exitWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext *ctx) = 0;

  virtual void enterBlock(AHFLParser::BlockContext *ctx) = 0;
  virtual void exitBlock(AHFLParser::BlockContext *ctx) = 0;

  virtual void enterStatement(AHFLParser::StatementContext *ctx) = 0;
  virtual void exitStatement(AHFLParser::StatementContext *ctx) = 0;

  virtual void enterLetStmt(AHFLParser::LetStmtContext *ctx) = 0;
  virtual void exitLetStmt(AHFLParser::LetStmtContext *ctx) = 0;

  virtual void enterAssignStmt(AHFLParser::AssignStmtContext *ctx) = 0;
  virtual void exitAssignStmt(AHFLParser::AssignStmtContext *ctx) = 0;

  virtual void enterIfStmt(AHFLParser::IfStmtContext *ctx) = 0;
  virtual void exitIfStmt(AHFLParser::IfStmtContext *ctx) = 0;

  virtual void enterGotoStmt(AHFLParser::GotoStmtContext *ctx) = 0;
  virtual void exitGotoStmt(AHFLParser::GotoStmtContext *ctx) = 0;

  virtual void enterReturnStmt(AHFLParser::ReturnStmtContext *ctx) = 0;
  virtual void exitReturnStmt(AHFLParser::ReturnStmtContext *ctx) = 0;

  virtual void enterAssertStmt(AHFLParser::AssertStmtContext *ctx) = 0;
  virtual void exitAssertStmt(AHFLParser::AssertStmtContext *ctx) = 0;

  virtual void enterExprStmt(AHFLParser::ExprStmtContext *ctx) = 0;
  virtual void exitExprStmt(AHFLParser::ExprStmtContext *ctx) = 0;

  virtual void enterLValue(AHFLParser::LValueContext *ctx) = 0;
  virtual void exitLValue(AHFLParser::LValueContext *ctx) = 0;

  virtual void enterExpr(AHFLParser::ExprContext *ctx) = 0;
  virtual void exitExpr(AHFLParser::ExprContext *ctx) = 0;

  virtual void enterImpliesExpr(AHFLParser::ImpliesExprContext *ctx) = 0;
  virtual void exitImpliesExpr(AHFLParser::ImpliesExprContext *ctx) = 0;

  virtual void enterOrExpr(AHFLParser::OrExprContext *ctx) = 0;
  virtual void exitOrExpr(AHFLParser::OrExprContext *ctx) = 0;

  virtual void enterAndExpr(AHFLParser::AndExprContext *ctx) = 0;
  virtual void exitAndExpr(AHFLParser::AndExprContext *ctx) = 0;

  virtual void enterEqualityExpr(AHFLParser::EqualityExprContext *ctx) = 0;
  virtual void exitEqualityExpr(AHFLParser::EqualityExprContext *ctx) = 0;

  virtual void enterCompareExpr(AHFLParser::CompareExprContext *ctx) = 0;
  virtual void exitCompareExpr(AHFLParser::CompareExprContext *ctx) = 0;

  virtual void enterAddExpr(AHFLParser::AddExprContext *ctx) = 0;
  virtual void exitAddExpr(AHFLParser::AddExprContext *ctx) = 0;

  virtual void enterMulExpr(AHFLParser::MulExprContext *ctx) = 0;
  virtual void exitMulExpr(AHFLParser::MulExprContext *ctx) = 0;

  virtual void enterUnaryExpr(AHFLParser::UnaryExprContext *ctx) = 0;
  virtual void exitUnaryExpr(AHFLParser::UnaryExprContext *ctx) = 0;

  virtual void enterPostfixExpr(AHFLParser::PostfixExprContext *ctx) = 0;
  virtual void exitPostfixExpr(AHFLParser::PostfixExprContext *ctx) = 0;

  virtual void enterPrimaryExpr(AHFLParser::PrimaryExprContext *ctx) = 0;
  virtual void exitPrimaryExpr(AHFLParser::PrimaryExprContext *ctx) = 0;

  virtual void enterPathExpr(AHFLParser::PathExprContext *ctx) = 0;
  virtual void exitPathExpr(AHFLParser::PathExprContext *ctx) = 0;

  virtual void enterPathRoot(AHFLParser::PathRootContext *ctx) = 0;
  virtual void exitPathRoot(AHFLParser::PathRootContext *ctx) = 0;

  virtual void enterQualifiedValueExpr(AHFLParser::QualifiedValueExprContext *ctx) = 0;
  virtual void exitQualifiedValueExpr(AHFLParser::QualifiedValueExprContext *ctx) = 0;

  virtual void enterCallExpr(AHFLParser::CallExprContext *ctx) = 0;
  virtual void exitCallExpr(AHFLParser::CallExprContext *ctx) = 0;

  virtual void enterExprList(AHFLParser::ExprListContext *ctx) = 0;
  virtual void exitExprList(AHFLParser::ExprListContext *ctx) = 0;

  virtual void enterLiteral(AHFLParser::LiteralContext *ctx) = 0;
  virtual void exitLiteral(AHFLParser::LiteralContext *ctx) = 0;

  virtual void enterIntegerLiteral(AHFLParser::IntegerLiteralContext *ctx) = 0;
  virtual void exitIntegerLiteral(AHFLParser::IntegerLiteralContext *ctx) = 0;

  virtual void enterFloatLiteral(AHFLParser::FloatLiteralContext *ctx) = 0;
  virtual void exitFloatLiteral(AHFLParser::FloatLiteralContext *ctx) = 0;

  virtual void enterDecimalLiteral(AHFLParser::DecimalLiteralContext *ctx) = 0;
  virtual void exitDecimalLiteral(AHFLParser::DecimalLiteralContext *ctx) = 0;

  virtual void enterStringLiteral(AHFLParser::StringLiteralContext *ctx) = 0;
  virtual void exitStringLiteral(AHFLParser::StringLiteralContext *ctx) = 0;

  virtual void enterDurationLiteral(AHFLParser::DurationLiteralContext *ctx) = 0;
  virtual void exitDurationLiteral(AHFLParser::DurationLiteralContext *ctx) = 0;

  virtual void enterListLiteral(AHFLParser::ListLiteralContext *ctx) = 0;
  virtual void exitListLiteral(AHFLParser::ListLiteralContext *ctx) = 0;

  virtual void enterSetLiteral(AHFLParser::SetLiteralContext *ctx) = 0;
  virtual void exitSetLiteral(AHFLParser::SetLiteralContext *ctx) = 0;

  virtual void enterMapLiteral(AHFLParser::MapLiteralContext *ctx) = 0;
  virtual void exitMapLiteral(AHFLParser::MapLiteralContext *ctx) = 0;

  virtual void enterMapEntryList(AHFLParser::MapEntryListContext *ctx) = 0;
  virtual void exitMapEntryList(AHFLParser::MapEntryListContext *ctx) = 0;

  virtual void enterMapEntry(AHFLParser::MapEntryContext *ctx) = 0;
  virtual void exitMapEntry(AHFLParser::MapEntryContext *ctx) = 0;

  virtual void enterStructLiteral(AHFLParser::StructLiteralContext *ctx) = 0;
  virtual void exitStructLiteral(AHFLParser::StructLiteralContext *ctx) = 0;

  virtual void enterStructInitList(AHFLParser::StructInitListContext *ctx) = 0;
  virtual void exitStructInitList(AHFLParser::StructInitListContext *ctx) = 0;

  virtual void enterStructInit(AHFLParser::StructInitContext *ctx) = 0;
  virtual void exitStructInit(AHFLParser::StructInitContext *ctx) = 0;

  virtual void enterConstExpr(AHFLParser::ConstExprContext *ctx) = 0;
  virtual void exitConstExpr(AHFLParser::ConstExprContext *ctx) = 0;

  virtual void enterTemporalExpr(AHFLParser::TemporalExprContext *ctx) = 0;
  virtual void exitTemporalExpr(AHFLParser::TemporalExprContext *ctx) = 0;

  virtual void enterWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext *ctx) = 0;
  virtual void exitWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext *ctx) = 0;

  virtual void enterTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext *ctx) = 0;
  virtual void exitTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext *ctx) = 0;

  virtual void enterTemporalOrExpr(AHFLParser::TemporalOrExprContext *ctx) = 0;
  virtual void exitTemporalOrExpr(AHFLParser::TemporalOrExprContext *ctx) = 0;

  virtual void enterTemporalAndExpr(AHFLParser::TemporalAndExprContext *ctx) = 0;
  virtual void exitTemporalAndExpr(AHFLParser::TemporalAndExprContext *ctx) = 0;

  virtual void enterTemporalUntilExpr(AHFLParser::TemporalUntilExprContext *ctx) = 0;
  virtual void exitTemporalUntilExpr(AHFLParser::TemporalUntilExprContext *ctx) = 0;

  virtual void enterTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext *ctx) = 0;
  virtual void exitTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext *ctx) = 0;

  virtual void enterTemporalAtom(AHFLParser::TemporalAtomContext *ctx) = 0;
  virtual void exitTemporalAtom(AHFLParser::TemporalAtomContext *ctx) = 0;


};

