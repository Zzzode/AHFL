
// Generated from grammar/AHFL.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "AHFLListener.h"


/**
 * This class provides an empty implementation of AHFLListener,
 * which can be extended to create a listener which only needs to handle a subset
 * of the available methods.
 */
class  AHFLBaseListener : public AHFLListener {
public:

  virtual void enterProgram(AHFLParser::ProgramContext * /*ctx*/) override { }
  virtual void exitProgram(AHFLParser::ProgramContext * /*ctx*/) override { }

  virtual void enterTopLevelDecl(AHFLParser::TopLevelDeclContext * /*ctx*/) override { }
  virtual void exitTopLevelDecl(AHFLParser::TopLevelDeclContext * /*ctx*/) override { }

  virtual void enterModuleDecl(AHFLParser::ModuleDeclContext * /*ctx*/) override { }
  virtual void exitModuleDecl(AHFLParser::ModuleDeclContext * /*ctx*/) override { }

  virtual void enterImportDecl(AHFLParser::ImportDeclContext * /*ctx*/) override { }
  virtual void exitImportDecl(AHFLParser::ImportDeclContext * /*ctx*/) override { }

  virtual void enterIdentifier(AHFLParser::IdentifierContext * /*ctx*/) override { }
  virtual void exitIdentifier(AHFLParser::IdentifierContext * /*ctx*/) override { }

  virtual void enterQualifiedIdent(AHFLParser::QualifiedIdentContext * /*ctx*/) override { }
  virtual void exitQualifiedIdent(AHFLParser::QualifiedIdentContext * /*ctx*/) override { }

  virtual void enterCallableNamePiece(AHFLParser::CallableNamePieceContext * /*ctx*/) override { }
  virtual void exitCallableNamePiece(AHFLParser::CallableNamePieceContext * /*ctx*/) override { }

  virtual void enterCallableName(AHFLParser::CallableNameContext * /*ctx*/) override { }
  virtual void exitCallableName(AHFLParser::CallableNameContext * /*ctx*/) override { }

  virtual void enterQualifiedIdentList(AHFLParser::QualifiedIdentListContext * /*ctx*/) override { }
  virtual void exitQualifiedIdentList(AHFLParser::QualifiedIdentListContext * /*ctx*/) override { }

  virtual void enterIdentList(AHFLParser::IdentListContext * /*ctx*/) override { }
  virtual void exitIdentList(AHFLParser::IdentListContext * /*ctx*/) override { }

  virtual void enterIdentListOpt(AHFLParser::IdentListOptContext * /*ctx*/) override { }
  virtual void exitIdentListOpt(AHFLParser::IdentListOptContext * /*ctx*/) override { }

  virtual void enterQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext * /*ctx*/) override { }
  virtual void exitQualifiedIdentListOpt(AHFLParser::QualifiedIdentListOptContext * /*ctx*/) override { }

  virtual void enterType_(AHFLParser::Type_Context * /*ctx*/) override { }
  virtual void exitType_(AHFLParser::Type_Context * /*ctx*/) override { }

  virtual void enterPrimitiveType(AHFLParser::PrimitiveTypeContext * /*ctx*/) override { }
  virtual void exitPrimitiveType(AHFLParser::PrimitiveTypeContext * /*ctx*/) override { }

  virtual void enterConstDecl(AHFLParser::ConstDeclContext * /*ctx*/) override { }
  virtual void exitConstDecl(AHFLParser::ConstDeclContext * /*ctx*/) override { }

  virtual void enterTypeAliasDecl(AHFLParser::TypeAliasDeclContext * /*ctx*/) override { }
  virtual void exitTypeAliasDecl(AHFLParser::TypeAliasDeclContext * /*ctx*/) override { }

  virtual void enterStructDecl(AHFLParser::StructDeclContext * /*ctx*/) override { }
  virtual void exitStructDecl(AHFLParser::StructDeclContext * /*ctx*/) override { }

  virtual void enterStructFieldDecl(AHFLParser::StructFieldDeclContext * /*ctx*/) override { }
  virtual void exitStructFieldDecl(AHFLParser::StructFieldDeclContext * /*ctx*/) override { }

  virtual void enterEnumDecl(AHFLParser::EnumDeclContext * /*ctx*/) override { }
  virtual void exitEnumDecl(AHFLParser::EnumDeclContext * /*ctx*/) override { }

  virtual void enterEnumVariant(AHFLParser::EnumVariantContext * /*ctx*/) override { }
  virtual void exitEnumVariant(AHFLParser::EnumVariantContext * /*ctx*/) override { }

  virtual void enterTypeList(AHFLParser::TypeListContext * /*ctx*/) override { }
  virtual void exitTypeList(AHFLParser::TypeListContext * /*ctx*/) override { }

  virtual void enterFnType(AHFLParser::FnTypeContext * /*ctx*/) override { }
  virtual void exitFnType(AHFLParser::FnTypeContext * /*ctx*/) override { }

  virtual void enterCapabilityDecl(AHFLParser::CapabilityDeclContext * /*ctx*/) override { }
  virtual void exitCapabilityDecl(AHFLParser::CapabilityDeclContext * /*ctx*/) override { }

  virtual void enterCapabilityEffectBlock(AHFLParser::CapabilityEffectBlockContext * /*ctx*/) override { }
  virtual void exitCapabilityEffectBlock(AHFLParser::CapabilityEffectBlockContext * /*ctx*/) override { }

  virtual void enterCapabilityEffectItem(AHFLParser::CapabilityEffectItemContext * /*ctx*/) override { }
  virtual void exitCapabilityEffectItem(AHFLParser::CapabilityEffectItemContext * /*ctx*/) override { }

  virtual void enterCapabilityEffectKind(AHFLParser::CapabilityEffectKindContext * /*ctx*/) override { }
  virtual void exitCapabilityEffectKind(AHFLParser::CapabilityEffectKindContext * /*ctx*/) override { }

  virtual void enterCapabilityReceiptMode(AHFLParser::CapabilityReceiptModeContext * /*ctx*/) override { }
  virtual void exitCapabilityReceiptMode(AHFLParser::CapabilityReceiptModeContext * /*ctx*/) override { }

  virtual void enterCapabilityRetryMode(AHFLParser::CapabilityRetryModeContext * /*ctx*/) override { }
  virtual void exitCapabilityRetryMode(AHFLParser::CapabilityRetryModeContext * /*ctx*/) override { }

  virtual void enterPredicateDecl(AHFLParser::PredicateDeclContext * /*ctx*/) override { }
  virtual void exitPredicateDecl(AHFLParser::PredicateDeclContext * /*ctx*/) override { }

  virtual void enterParamList(AHFLParser::ParamListContext * /*ctx*/) override { }
  virtual void exitParamList(AHFLParser::ParamListContext * /*ctx*/) override { }

  virtual void enterParam(AHFLParser::ParamContext * /*ctx*/) override { }
  virtual void exitParam(AHFLParser::ParamContext * /*ctx*/) override { }

  virtual void enterAgentDecl(AHFLParser::AgentDeclContext * /*ctx*/) override { }
  virtual void exitAgentDecl(AHFLParser::AgentDeclContext * /*ctx*/) override { }

  virtual void enterInputDecl(AHFLParser::InputDeclContext * /*ctx*/) override { }
  virtual void exitInputDecl(AHFLParser::InputDeclContext * /*ctx*/) override { }

  virtual void enterContextDecl(AHFLParser::ContextDeclContext * /*ctx*/) override { }
  virtual void exitContextDecl(AHFLParser::ContextDeclContext * /*ctx*/) override { }

  virtual void enterOutputDecl(AHFLParser::OutputDeclContext * /*ctx*/) override { }
  virtual void exitOutputDecl(AHFLParser::OutputDeclContext * /*ctx*/) override { }

  virtual void enterStatesDecl(AHFLParser::StatesDeclContext * /*ctx*/) override { }
  virtual void exitStatesDecl(AHFLParser::StatesDeclContext * /*ctx*/) override { }

  virtual void enterInitialDecl(AHFLParser::InitialDeclContext * /*ctx*/) override { }
  virtual void exitInitialDecl(AHFLParser::InitialDeclContext * /*ctx*/) override { }

  virtual void enterFinalDecl(AHFLParser::FinalDeclContext * /*ctx*/) override { }
  virtual void exitFinalDecl(AHFLParser::FinalDeclContext * /*ctx*/) override { }

  virtual void enterCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext * /*ctx*/) override { }
  virtual void exitCapabilitiesDecl(AHFLParser::CapabilitiesDeclContext * /*ctx*/) override { }

  virtual void enterQuotaDecl(AHFLParser::QuotaDeclContext * /*ctx*/) override { }
  virtual void exitQuotaDecl(AHFLParser::QuotaDeclContext * /*ctx*/) override { }

  virtual void enterQuotaItem(AHFLParser::QuotaItemContext * /*ctx*/) override { }
  virtual void exitQuotaItem(AHFLParser::QuotaItemContext * /*ctx*/) override { }

  virtual void enterTransitionDecl(AHFLParser::TransitionDeclContext * /*ctx*/) override { }
  virtual void exitTransitionDecl(AHFLParser::TransitionDeclContext * /*ctx*/) override { }

  virtual void enterContractDecl(AHFLParser::ContractDeclContext * /*ctx*/) override { }
  virtual void exitContractDecl(AHFLParser::ContractDeclContext * /*ctx*/) override { }

  virtual void enterContractItem(AHFLParser::ContractItemContext * /*ctx*/) override { }
  virtual void exitContractItem(AHFLParser::ContractItemContext * /*ctx*/) override { }

  virtual void enterRequiresDecl(AHFLParser::RequiresDeclContext * /*ctx*/) override { }
  virtual void exitRequiresDecl(AHFLParser::RequiresDeclContext * /*ctx*/) override { }

  virtual void enterEnsuresDecl(AHFLParser::EnsuresDeclContext * /*ctx*/) override { }
  virtual void exitEnsuresDecl(AHFLParser::EnsuresDeclContext * /*ctx*/) override { }

  virtual void enterInvariantDecl(AHFLParser::InvariantDeclContext * /*ctx*/) override { }
  virtual void exitInvariantDecl(AHFLParser::InvariantDeclContext * /*ctx*/) override { }

  virtual void enterForbidDecl(AHFLParser::ForbidDeclContext * /*ctx*/) override { }
  virtual void exitForbidDecl(AHFLParser::ForbidDeclContext * /*ctx*/) override { }

  virtual void enterDecreasesDecl(AHFLParser::DecreasesDeclContext * /*ctx*/) override { }
  virtual void exitDecreasesDecl(AHFLParser::DecreasesDeclContext * /*ctx*/) override { }

  virtual void enterFlowDecl(AHFLParser::FlowDeclContext * /*ctx*/) override { }
  virtual void exitFlowDecl(AHFLParser::FlowDeclContext * /*ctx*/) override { }

  virtual void enterStateHandler(AHFLParser::StateHandlerContext * /*ctx*/) override { }
  virtual void exitStateHandler(AHFLParser::StateHandlerContext * /*ctx*/) override { }

  virtual void enterStatePolicy(AHFLParser::StatePolicyContext * /*ctx*/) override { }
  virtual void exitStatePolicy(AHFLParser::StatePolicyContext * /*ctx*/) override { }

  virtual void enterStatePolicyItem(AHFLParser::StatePolicyItemContext * /*ctx*/) override { }
  virtual void exitStatePolicyItem(AHFLParser::StatePolicyItemContext * /*ctx*/) override { }

  virtual void enterWorkflowDecl(AHFLParser::WorkflowDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowDecl(AHFLParser::WorkflowDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowInputDecl(AHFLParser::WorkflowInputDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowOutputDecl(AHFLParser::WorkflowOutputDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowItem(AHFLParser::WorkflowItemContext * /*ctx*/) override { }
  virtual void exitWorkflowItem(AHFLParser::WorkflowItemContext * /*ctx*/) override { }

  virtual void enterWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowNodeDecl(AHFLParser::WorkflowNodeDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowSafetyDecl(AHFLParser::WorkflowSafetyDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowLivenessDecl(AHFLParser::WorkflowLivenessDeclContext * /*ctx*/) override { }

  virtual void enterWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext * /*ctx*/) override { }
  virtual void exitWorkflowReturnDecl(AHFLParser::WorkflowReturnDeclContext * /*ctx*/) override { }

  virtual void enterBuiltinAttr(AHFLParser::BuiltinAttrContext * /*ctx*/) override { }
  virtual void exitBuiltinAttr(AHFLParser::BuiltinAttrContext * /*ctx*/) override { }

  virtual void enterFnDecl(AHFLParser::FnDeclContext * /*ctx*/) override { }
  virtual void exitFnDecl(AHFLParser::FnDeclContext * /*ctx*/) override { }

  virtual void enterTypeParams(AHFLParser::TypeParamsContext * /*ctx*/) override { }
  virtual void exitTypeParams(AHFLParser::TypeParamsContext * /*ctx*/) override { }

  virtual void enterTypeParam(AHFLParser::TypeParamContext * /*ctx*/) override { }
  virtual void exitTypeParam(AHFLParser::TypeParamContext * /*ctx*/) override { }

  virtual void enterTypeBoundList(AHFLParser::TypeBoundListContext * /*ctx*/) override { }
  virtual void exitTypeBoundList(AHFLParser::TypeBoundListContext * /*ctx*/) override { }

  virtual void enterFnBody(AHFLParser::FnBodyContext * /*ctx*/) override { }
  virtual void exitFnBody(AHFLParser::FnBodyContext * /*ctx*/) override { }

  virtual void enterEffectClause(AHFLParser::EffectClauseContext * /*ctx*/) override { }
  virtual void exitEffectClause(AHFLParser::EffectClauseContext * /*ctx*/) override { }

  virtual void enterEffectSpec(AHFLParser::EffectSpecContext * /*ctx*/) override { }
  virtual void exitEffectSpec(AHFLParser::EffectSpecContext * /*ctx*/) override { }

  virtual void enterCapabilityRef(AHFLParser::CapabilityRefContext * /*ctx*/) override { }
  virtual void exitCapabilityRef(AHFLParser::CapabilityRefContext * /*ctx*/) override { }

  virtual void enterDecreasesClause(AHFLParser::DecreasesClauseContext * /*ctx*/) override { }
  virtual void exitDecreasesClause(AHFLParser::DecreasesClauseContext * /*ctx*/) override { }

  virtual void enterWhereClause(AHFLParser::WhereClauseContext * /*ctx*/) override { }
  virtual void exitWhereClause(AHFLParser::WhereClauseContext * /*ctx*/) override { }

  virtual void enterWhereConstraint(AHFLParser::WhereConstraintContext * /*ctx*/) override { }
  virtual void exitWhereConstraint(AHFLParser::WhereConstraintContext * /*ctx*/) override { }

  virtual void enterLambdaExpr(AHFLParser::LambdaExprContext * /*ctx*/) override { }
  virtual void exitLambdaExpr(AHFLParser::LambdaExprContext * /*ctx*/) override { }

  virtual void enterLambdaParamList(AHFLParser::LambdaParamListContext * /*ctx*/) override { }
  virtual void exitLambdaParamList(AHFLParser::LambdaParamListContext * /*ctx*/) override { }

  virtual void enterLambdaParam(AHFLParser::LambdaParamContext * /*ctx*/) override { }
  virtual void exitLambdaParam(AHFLParser::LambdaParamContext * /*ctx*/) override { }

  virtual void enterTraitDecl(AHFLParser::TraitDeclContext * /*ctx*/) override { }
  virtual void exitTraitDecl(AHFLParser::TraitDeclContext * /*ctx*/) override { }

  virtual void enterTraitItem(AHFLParser::TraitItemContext * /*ctx*/) override { }
  virtual void exitTraitItem(AHFLParser::TraitItemContext * /*ctx*/) override { }

  virtual void enterTraitFnItem(AHFLParser::TraitFnItemContext * /*ctx*/) override { }
  virtual void exitTraitFnItem(AHFLParser::TraitFnItemContext * /*ctx*/) override { }

  virtual void enterAssocTypeItem(AHFLParser::AssocTypeItemContext * /*ctx*/) override { }
  virtual void exitAssocTypeItem(AHFLParser::AssocTypeItemContext * /*ctx*/) override { }

  virtual void enterAssocConstItem(AHFLParser::AssocConstItemContext * /*ctx*/) override { }
  virtual void exitAssocConstItem(AHFLParser::AssocConstItemContext * /*ctx*/) override { }

  virtual void enterImplDecl(AHFLParser::ImplDeclContext * /*ctx*/) override { }
  virtual void exitImplDecl(AHFLParser::ImplDeclContext * /*ctx*/) override { }

  virtual void enterTraitRef(AHFLParser::TraitRefContext * /*ctx*/) override { }
  virtual void exitTraitRef(AHFLParser::TraitRefContext * /*ctx*/) override { }

  virtual void enterImplItem(AHFLParser::ImplItemContext * /*ctx*/) override { }
  virtual void exitImplItem(AHFLParser::ImplItemContext * /*ctx*/) override { }

  virtual void enterImplFnItem(AHFLParser::ImplFnItemContext * /*ctx*/) override { }
  virtual void exitImplFnItem(AHFLParser::ImplFnItemContext * /*ctx*/) override { }

  virtual void enterAssocTypeDef(AHFLParser::AssocTypeDefContext * /*ctx*/) override { }
  virtual void exitAssocTypeDef(AHFLParser::AssocTypeDefContext * /*ctx*/) override { }

  virtual void enterAssocConstDef(AHFLParser::AssocConstDefContext * /*ctx*/) override { }
  virtual void exitAssocConstDef(AHFLParser::AssocConstDefContext * /*ctx*/) override { }

  virtual void enterBlock(AHFLParser::BlockContext * /*ctx*/) override { }
  virtual void exitBlock(AHFLParser::BlockContext * /*ctx*/) override { }

  virtual void enterStatement(AHFLParser::StatementContext * /*ctx*/) override { }
  virtual void exitStatement(AHFLParser::StatementContext * /*ctx*/) override { }

  virtual void enterLetStmt(AHFLParser::LetStmtContext * /*ctx*/) override { }
  virtual void exitLetStmt(AHFLParser::LetStmtContext * /*ctx*/) override { }

  virtual void enterAssignStmt(AHFLParser::AssignStmtContext * /*ctx*/) override { }
  virtual void exitAssignStmt(AHFLParser::AssignStmtContext * /*ctx*/) override { }

  virtual void enterIfStmt(AHFLParser::IfStmtContext * /*ctx*/) override { }
  virtual void exitIfStmt(AHFLParser::IfStmtContext * /*ctx*/) override { }

  virtual void enterGotoStmt(AHFLParser::GotoStmtContext * /*ctx*/) override { }
  virtual void exitGotoStmt(AHFLParser::GotoStmtContext * /*ctx*/) override { }

  virtual void enterReturnStmt(AHFLParser::ReturnStmtContext * /*ctx*/) override { }
  virtual void exitReturnStmt(AHFLParser::ReturnStmtContext * /*ctx*/) override { }

  virtual void enterAssertStmt(AHFLParser::AssertStmtContext * /*ctx*/) override { }
  virtual void exitAssertStmt(AHFLParser::AssertStmtContext * /*ctx*/) override { }

  virtual void enterUnwrapStmt(AHFLParser::UnwrapStmtContext * /*ctx*/) override { }
  virtual void exitUnwrapStmt(AHFLParser::UnwrapStmtContext * /*ctx*/) override { }

  virtual void enterRequiresStmt(AHFLParser::RequiresStmtContext * /*ctx*/) override { }
  virtual void exitRequiresStmt(AHFLParser::RequiresStmtContext * /*ctx*/) override { }

  virtual void enterUnreachableStmt(AHFLParser::UnreachableStmtContext * /*ctx*/) override { }
  virtual void exitUnreachableStmt(AHFLParser::UnreachableStmtContext * /*ctx*/) override { }

  virtual void enterExprStmt(AHFLParser::ExprStmtContext * /*ctx*/) override { }
  virtual void exitExprStmt(AHFLParser::ExprStmtContext * /*ctx*/) override { }

  virtual void enterLValue(AHFLParser::LValueContext * /*ctx*/) override { }
  virtual void exitLValue(AHFLParser::LValueContext * /*ctx*/) override { }

  virtual void enterExpr(AHFLParser::ExprContext * /*ctx*/) override { }
  virtual void exitExpr(AHFLParser::ExprContext * /*ctx*/) override { }

  virtual void enterImpliesExpr(AHFLParser::ImpliesExprContext * /*ctx*/) override { }
  virtual void exitImpliesExpr(AHFLParser::ImpliesExprContext * /*ctx*/) override { }

  virtual void enterOrExpr(AHFLParser::OrExprContext * /*ctx*/) override { }
  virtual void exitOrExpr(AHFLParser::OrExprContext * /*ctx*/) override { }

  virtual void enterAndExpr(AHFLParser::AndExprContext * /*ctx*/) override { }
  virtual void exitAndExpr(AHFLParser::AndExprContext * /*ctx*/) override { }

  virtual void enterEqualityExpr(AHFLParser::EqualityExprContext * /*ctx*/) override { }
  virtual void exitEqualityExpr(AHFLParser::EqualityExprContext * /*ctx*/) override { }

  virtual void enterCompareExpr(AHFLParser::CompareExprContext * /*ctx*/) override { }
  virtual void exitCompareExpr(AHFLParser::CompareExprContext * /*ctx*/) override { }

  virtual void enterAddExpr(AHFLParser::AddExprContext * /*ctx*/) override { }
  virtual void exitAddExpr(AHFLParser::AddExprContext * /*ctx*/) override { }

  virtual void enterMulExpr(AHFLParser::MulExprContext * /*ctx*/) override { }
  virtual void exitMulExpr(AHFLParser::MulExprContext * /*ctx*/) override { }

  virtual void enterUnaryExpr(AHFLParser::UnaryExprContext * /*ctx*/) override { }
  virtual void exitUnaryExpr(AHFLParser::UnaryExprContext * /*ctx*/) override { }

  virtual void enterPostfixExpr(AHFLParser::PostfixExprContext * /*ctx*/) override { }
  virtual void exitPostfixExpr(AHFLParser::PostfixExprContext * /*ctx*/) override { }

  virtual void enterPrimaryExpr(AHFLParser::PrimaryExprContext * /*ctx*/) override { }
  virtual void exitPrimaryExpr(AHFLParser::PrimaryExprContext * /*ctx*/) override { }

  virtual void enterMatchExpr(AHFLParser::MatchExprContext * /*ctx*/) override { }
  virtual void exitMatchExpr(AHFLParser::MatchExprContext * /*ctx*/) override { }

  virtual void enterMatchArm(AHFLParser::MatchArmContext * /*ctx*/) override { }
  virtual void exitMatchArm(AHFLParser::MatchArmContext * /*ctx*/) override { }

  virtual void enterPattern(AHFLParser::PatternContext * /*ctx*/) override { }
  virtual void exitPattern(AHFLParser::PatternContext * /*ctx*/) override { }

  virtual void enterOrPattern(AHFLParser::OrPatternContext * /*ctx*/) override { }
  virtual void exitOrPattern(AHFLParser::OrPatternContext * /*ctx*/) override { }

  virtual void enterConcatPattern(AHFLParser::ConcatPatternContext * /*ctx*/) override { }
  virtual void exitConcatPattern(AHFLParser::ConcatPatternContext * /*ctx*/) override { }

  virtual void enterLiteralPattern(AHFLParser::LiteralPatternContext * /*ctx*/) override { }
  virtual void exitLiteralPattern(AHFLParser::LiteralPatternContext * /*ctx*/) override { }

  virtual void enterVariantPattern(AHFLParser::VariantPatternContext * /*ctx*/) override { }
  virtual void exitVariantPattern(AHFLParser::VariantPatternContext * /*ctx*/) override { }

  virtual void enterWildcardPattern(AHFLParser::WildcardPatternContext * /*ctx*/) override { }
  virtual void exitWildcardPattern(AHFLParser::WildcardPatternContext * /*ctx*/) override { }

  virtual void enterBindingPattern(AHFLParser::BindingPatternContext * /*ctx*/) override { }
  virtual void exitBindingPattern(AHFLParser::BindingPatternContext * /*ctx*/) override { }

  virtual void enterTuplePattern(AHFLParser::TuplePatternContext * /*ctx*/) override { }
  virtual void exitTuplePattern(AHFLParser::TuplePatternContext * /*ctx*/) override { }

  virtual void enterPatternList(AHFLParser::PatternListContext * /*ctx*/) override { }
  virtual void exitPatternList(AHFLParser::PatternListContext * /*ctx*/) override { }

  virtual void enterPathExpr(AHFLParser::PathExprContext * /*ctx*/) override { }
  virtual void exitPathExpr(AHFLParser::PathExprContext * /*ctx*/) override { }

  virtual void enterPathRoot(AHFLParser::PathRootContext * /*ctx*/) override { }
  virtual void exitPathRoot(AHFLParser::PathRootContext * /*ctx*/) override { }

  virtual void enterQualifiedValueExpr(AHFLParser::QualifiedValueExprContext * /*ctx*/) override { }
  virtual void exitQualifiedValueExpr(AHFLParser::QualifiedValueExprContext * /*ctx*/) override { }

  virtual void enterCallExpr(AHFLParser::CallExprContext * /*ctx*/) override { }
  virtual void exitCallExpr(AHFLParser::CallExprContext * /*ctx*/) override { }

  virtual void enterExprList(AHFLParser::ExprListContext * /*ctx*/) override { }
  virtual void exitExprList(AHFLParser::ExprListContext * /*ctx*/) override { }

  virtual void enterLiteral(AHFLParser::LiteralContext * /*ctx*/) override { }
  virtual void exitLiteral(AHFLParser::LiteralContext * /*ctx*/) override { }

  virtual void enterIntegerLiteral(AHFLParser::IntegerLiteralContext * /*ctx*/) override { }
  virtual void exitIntegerLiteral(AHFLParser::IntegerLiteralContext * /*ctx*/) override { }

  virtual void enterFloatLiteral(AHFLParser::FloatLiteralContext * /*ctx*/) override { }
  virtual void exitFloatLiteral(AHFLParser::FloatLiteralContext * /*ctx*/) override { }

  virtual void enterDecimalLiteral(AHFLParser::DecimalLiteralContext * /*ctx*/) override { }
  virtual void exitDecimalLiteral(AHFLParser::DecimalLiteralContext * /*ctx*/) override { }

  virtual void enterStringLiteral(AHFLParser::StringLiteralContext * /*ctx*/) override { }
  virtual void exitStringLiteral(AHFLParser::StringLiteralContext * /*ctx*/) override { }

  virtual void enterDurationLiteral(AHFLParser::DurationLiteralContext * /*ctx*/) override { }
  virtual void exitDurationLiteral(AHFLParser::DurationLiteralContext * /*ctx*/) override { }

  virtual void enterStructLiteral(AHFLParser::StructLiteralContext * /*ctx*/) override { }
  virtual void exitStructLiteral(AHFLParser::StructLiteralContext * /*ctx*/) override { }

  virtual void enterListLiteral(AHFLParser::ListLiteralContext * /*ctx*/) override { }
  virtual void exitListLiteral(AHFLParser::ListLiteralContext * /*ctx*/) override { }

  virtual void enterSetLiteral(AHFLParser::SetLiteralContext * /*ctx*/) override { }
  virtual void exitSetLiteral(AHFLParser::SetLiteralContext * /*ctx*/) override { }

  virtual void enterMapLiteral(AHFLParser::MapLiteralContext * /*ctx*/) override { }
  virtual void exitMapLiteral(AHFLParser::MapLiteralContext * /*ctx*/) override { }

  virtual void enterMapEntryList(AHFLParser::MapEntryListContext * /*ctx*/) override { }
  virtual void exitMapEntryList(AHFLParser::MapEntryListContext * /*ctx*/) override { }

  virtual void enterMapEntry(AHFLParser::MapEntryContext * /*ctx*/) override { }
  virtual void exitMapEntry(AHFLParser::MapEntryContext * /*ctx*/) override { }

  virtual void enterStructInitList(AHFLParser::StructInitListContext * /*ctx*/) override { }
  virtual void exitStructInitList(AHFLParser::StructInitListContext * /*ctx*/) override { }

  virtual void enterStructInit(AHFLParser::StructInitContext * /*ctx*/) override { }
  virtual void exitStructInit(AHFLParser::StructInitContext * /*ctx*/) override { }

  virtual void enterConstExpr(AHFLParser::ConstExprContext * /*ctx*/) override { }
  virtual void exitConstExpr(AHFLParser::ConstExprContext * /*ctx*/) override { }

  virtual void enterTemporalExpr(AHFLParser::TemporalExprContext * /*ctx*/) override { }
  virtual void exitTemporalExpr(AHFLParser::TemporalExprContext * /*ctx*/) override { }

  virtual void enterWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext * /*ctx*/) override { }
  virtual void exitWorkflowTemporalExpr(AHFLParser::WorkflowTemporalExprContext * /*ctx*/) override { }

  virtual void enterTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext * /*ctx*/) override { }
  virtual void exitTemporalImpliesExpr(AHFLParser::TemporalImpliesExprContext * /*ctx*/) override { }

  virtual void enterTemporalOrExpr(AHFLParser::TemporalOrExprContext * /*ctx*/) override { }
  virtual void exitTemporalOrExpr(AHFLParser::TemporalOrExprContext * /*ctx*/) override { }

  virtual void enterTemporalAndExpr(AHFLParser::TemporalAndExprContext * /*ctx*/) override { }
  virtual void exitTemporalAndExpr(AHFLParser::TemporalAndExprContext * /*ctx*/) override { }

  virtual void enterTemporalUntilExpr(AHFLParser::TemporalUntilExprContext * /*ctx*/) override { }
  virtual void exitTemporalUntilExpr(AHFLParser::TemporalUntilExprContext * /*ctx*/) override { }

  virtual void enterTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext * /*ctx*/) override { }
  virtual void exitTemporalUnaryExpr(AHFLParser::TemporalUnaryExprContext * /*ctx*/) override { }

  virtual void enterTemporalAtom(AHFLParser::TemporalAtomContext * /*ctx*/) override { }
  virtual void exitTemporalAtom(AHFLParser::TemporalAtomContext * /*ctx*/) override { }


  virtual void enterEveryRule(antlr4::ParserRuleContext * /*ctx*/) override { }
  virtual void exitEveryRule(antlr4::ParserRuleContext * /*ctx*/) override { }
  virtual void visitTerminal(antlr4::tree::TerminalNode * /*node*/) override { }
  virtual void visitErrorNode(antlr4::tree::ErrorNode * /*node*/) override { }

};

