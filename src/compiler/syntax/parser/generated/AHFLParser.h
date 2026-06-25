
// Generated from grammar/AHFL.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  AHFLParser : public antlr4::Parser {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7,
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14,
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, T__19 = 20,
    T__20 = 21, T__21 = 22, T__22 = 23, T__23 = 24, T__24 = 25, T__25 = 26,
    T__26 = 27, T__27 = 28, T__28 = 29, T__29 = 30, T__30 = 31, T__31 = 32,
    T__32 = 33, T__33 = 34, T__34 = 35, T__35 = 36, T__36 = 37, T__37 = 38,
    T__38 = 39, T__39 = 40, T__40 = 41, T__41 = 42, T__42 = 43, T__43 = 44,
    T__44 = 45, T__45 = 46, T__46 = 47, T__47 = 48, T__48 = 49, T__49 = 50,
    T__50 = 51, T__51 = 52, T__52 = 53, T__53 = 54, T__54 = 55, T__55 = 56,
    T__56 = 57, T__57 = 58, T__58 = 59, T__59 = 60, T__60 = 61, T__61 = 62,
    T__62 = 63, T__63 = 64, T__64 = 65, T__65 = 66, T__66 = 67, T__67 = 68,
    T__68 = 69, T__69 = 70, T__70 = 71, T__71 = 72, T__72 = 73, T__73 = 74,
    T__74 = 75, T__75 = 76, T__76 = 77, T__77 = 78, T__78 = 79, T__79 = 80,
    T__80 = 81, T__81 = 82, T__82 = 83, T__83 = 84, T__84 = 85, T__85 = 86,
    T__86 = 87, T__87 = 88, T__88 = 89, T__89 = 90, T__90 = 91, T__91 = 92,
    T__92 = 93, T__93 = 94, T__94 = 95, T__95 = 96, T__96 = 97, T__97 = 98,
    T__98 = 99, T__99 = 100, T__100 = 101, T__101 = 102, T__102 = 103, T__103 = 104,
    T__104 = 105, T__105 = 106, T__106 = 107, T__107 = 108, T__108 = 109,
    T__109 = 110, T__110 = 111, T__111 = 112, T__112 = 113, T__113 = 114,
    T__114 = 115, T__115 = 116, T__116 = 117, T__117 = 118, T__118 = 119,
    T__119 = 120, T__120 = 121, T__121 = 122, T__122 = 123, T__123 = 124,
    T__124 = 125, T__125 = 126, T__126 = 127, T__127 = 128, T__128 = 129,
    T__129 = 130, T__130 = 131, T__131 = 132, T__132 = 133, T__133 = 134,
    DURATION_LITERAL = 135, DECIMAL_LITERAL = 136, FLOAT_LITERAL = 137,
    INT_LITERAL = 138, BACKSLASH = 139, STRING_LITERAL = 140, IDENT = 141,
    DOC_COMMENT = 142, LINE_COMMENT = 143, BLOCK_COMMENT = 144, WS = 145
  };

  enum {
    RuleProgram = 0, RuleTopLevelDecl = 1, RuleModuleDecl = 2, RuleImportDecl = 3,
    RuleIdentifier = 4, RuleQualifiedIdent = 5, RuleQualifiedIdentList = 6,
    RuleIdentList = 7, RuleIdentListOpt = 8, RuleQualifiedIdentListOpt = 9,
    RuleType_ = 10, RulePrimitiveType = 11, RuleConstDecl = 12, RuleTypeAliasDecl = 13,
    RuleStructDecl = 14, RuleStructFieldDecl = 15, RuleEnumDecl = 16, RuleEnumVariant = 17,
    RuleTypeList = 18, RuleFnType = 19, RuleCapabilityDecl = 20, RuleCapabilityEffectBlock = 21,
    RuleCapabilityEffectItem = 22, RuleCapabilityEffectKind = 23, RuleCapabilityReceiptMode = 24,
    RuleCapabilityRetryMode = 25, RulePredicateDecl = 26, RuleParamList = 27,
    RuleParam = 28, RuleAgentDecl = 29, RuleInputDecl = 30, RuleContextDecl = 31,
    RuleOutputDecl = 32, RuleStatesDecl = 33, RuleInitialDecl = 34, RuleFinalDecl = 35,
    RuleCapabilitiesDecl = 36, RuleQuotaDecl = 37, RuleQuotaItem = 38, RuleTransitionDecl = 39,
    RuleContractDecl = 40, RuleContractItem = 41, RuleRequiresDecl = 42,
    RuleEnsuresDecl = 43, RuleInvariantDecl = 44, RuleForbidDecl = 45, RuleDecreasesDecl = 46,
    RuleFlowDecl = 47, RuleStateHandler = 48, RuleStatePolicy = 49, RuleStatePolicyItem = 50,
    RuleWorkflowDecl = 51, RuleWorkflowInputDecl = 52, RuleWorkflowOutputDecl = 53,
    RuleWorkflowItem = 54, RuleWorkflowNodeDecl = 55, RuleWorkflowSafetyDecl = 56,
    RuleWorkflowLivenessDecl = 57, RuleWorkflowReturnDecl = 58, RuleBuiltinAttr = 59,
    RuleFnDecl = 60, RuleTypeParams = 61, RuleTypeParam = 62, RuleTypeBoundList = 63,
    RuleFnBody = 64, RuleEffectClause = 65, RuleEffectSpec = 66, RuleCapabilityRef = 67,
    RuleDecreasesClause = 68, RuleWhereClause = 69, RuleWhereConstraint = 70,
    RuleLambdaExpr = 71, RuleLambdaParamList = 72, RuleLambdaParam = 73,
    RuleTraitDecl = 74, RuleTraitItem = 75, RuleTraitFnItem = 76, RuleAssocTypeItem = 77,
    RuleAssocConstItem = 78, RuleImplDecl = 79, RuleTraitRef = 80, RuleImplItem = 81,
    RuleImplFnItem = 82, RuleAssocTypeDef = 83, RuleAssocConstDef = 84,
    RuleBlock = 85, RuleStatement = 86, RuleLetStmt = 87, RuleAssignStmt = 88,
    RuleIfStmt = 89, RuleGotoStmt = 90, RuleReturnStmt = 91, RuleAssertStmt = 92,
    RuleExprStmt = 93, RuleLValue = 94, RuleExpr = 95, RuleImpliesExpr = 96,
    RuleOrExpr = 97, RuleAndExpr = 98, RuleEqualityExpr = 99, RuleCompareExpr = 100,
    RuleAddExpr = 101, RuleMulExpr = 102, RuleUnaryExpr = 103, RulePostfixExpr = 104,
    RulePrimaryExpr = 105, RuleMatchExpr = 106, RuleMatchArm = 107, RulePattern = 108,
    RuleOrPattern = 109, RuleConcatPattern = 110, RuleLiteralPattern = 111,
    RuleVariantPattern = 112, RuleWildcardPattern = 113, RuleBindingPattern = 114,
    RuleTuplePattern = 115, RulePatternList = 116, RulePathExpr = 117, RulePathRoot = 118,
    RuleQualifiedValueExpr = 119, RuleCallExpr = 120, RuleExprList = 121,
    RuleLiteral = 122, RuleIntegerLiteral = 123, RuleFloatLiteral = 124,
    RuleDecimalLiteral = 125, RuleStringLiteral = 126, RuleDurationLiteral = 127,
    RuleStructLiteral = 128, RuleListLiteral = 129, RuleSetLiteral = 130,
    RuleMapLiteral = 131, RuleMapEntryList = 132, RuleMapEntry = 133, RuleStructInitList = 134,
    RuleStructInit = 135, RuleConstExpr = 136, RuleTemporalExpr = 137, RuleWorkflowTemporalExpr = 138,
    RuleTemporalImpliesExpr = 139, RuleTemporalOrExpr = 140, RuleTemporalAndExpr = 141,
    RuleTemporalUntilExpr = 142, RuleTemporalUnaryExpr = 143, RuleTemporalAtom = 144
  };

  explicit AHFLParser(antlr4::TokenStream *input);

  AHFLParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~AHFLParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class ProgramContext;
  class TopLevelDeclContext;
  class ModuleDeclContext;
  class ImportDeclContext;
  class IdentifierContext;
  class QualifiedIdentContext;
  class QualifiedIdentListContext;
  class IdentListContext;
  class IdentListOptContext;
  class QualifiedIdentListOptContext;
  class Type_Context;
  class PrimitiveTypeContext;
  class ConstDeclContext;
  class TypeAliasDeclContext;
  class StructDeclContext;
  class StructFieldDeclContext;
  class EnumDeclContext;
  class EnumVariantContext;
  class TypeListContext;
  class FnTypeContext;
  class CapabilityDeclContext;
  class CapabilityEffectBlockContext;
  class CapabilityEffectItemContext;
  class CapabilityEffectKindContext;
  class CapabilityReceiptModeContext;
  class CapabilityRetryModeContext;
  class PredicateDeclContext;
  class ParamListContext;
  class ParamContext;
  class AgentDeclContext;
  class InputDeclContext;
  class ContextDeclContext;
  class OutputDeclContext;
  class StatesDeclContext;
  class InitialDeclContext;
  class FinalDeclContext;
  class CapabilitiesDeclContext;
  class QuotaDeclContext;
  class QuotaItemContext;
  class TransitionDeclContext;
  class ContractDeclContext;
  class ContractItemContext;
  class RequiresDeclContext;
  class EnsuresDeclContext;
  class InvariantDeclContext;
  class ForbidDeclContext;
  class DecreasesDeclContext;
  class FlowDeclContext;
  class StateHandlerContext;
  class StatePolicyContext;
  class StatePolicyItemContext;
  class WorkflowDeclContext;
  class WorkflowInputDeclContext;
  class WorkflowOutputDeclContext;
  class WorkflowItemContext;
  class WorkflowNodeDeclContext;
  class WorkflowSafetyDeclContext;
  class WorkflowLivenessDeclContext;
  class WorkflowReturnDeclContext;
  class BuiltinAttrContext;
  class FnDeclContext;
  class TypeParamsContext;
  class TypeParamContext;
  class TypeBoundListContext;
  class FnBodyContext;
  class EffectClauseContext;
  class EffectSpecContext;
  class CapabilityRefContext;
  class DecreasesClauseContext;
  class WhereClauseContext;
  class WhereConstraintContext;
  class LambdaExprContext;
  class LambdaParamListContext;
  class LambdaParamContext;
  class TraitDeclContext;
  class TraitItemContext;
  class TraitFnItemContext;
  class AssocTypeItemContext;
  class AssocConstItemContext;
  class ImplDeclContext;
  class TraitRefContext;
  class ImplItemContext;
  class ImplFnItemContext;
  class AssocTypeDefContext;
  class AssocConstDefContext;
  class BlockContext;
  class StatementContext;
  class LetStmtContext;
  class AssignStmtContext;
  class IfStmtContext;
  class GotoStmtContext;
  class ReturnStmtContext;
  class AssertStmtContext;
  class ExprStmtContext;
  class LValueContext;
  class ExprContext;
  class ImpliesExprContext;
  class OrExprContext;
  class AndExprContext;
  class EqualityExprContext;
  class CompareExprContext;
  class AddExprContext;
  class MulExprContext;
  class UnaryExprContext;
  class PostfixExprContext;
  class PrimaryExprContext;
  class MatchExprContext;
  class MatchArmContext;
  class PatternContext;
  class OrPatternContext;
  class ConcatPatternContext;
  class LiteralPatternContext;
  class VariantPatternContext;
  class WildcardPatternContext;
  class BindingPatternContext;
  class TuplePatternContext;
  class PatternListContext;
  class PathExprContext;
  class PathRootContext;
  class QualifiedValueExprContext;
  class CallExprContext;
  class ExprListContext;
  class LiteralContext;
  class IntegerLiteralContext;
  class FloatLiteralContext;
  class DecimalLiteralContext;
  class StringLiteralContext;
  class DurationLiteralContext;
  class StructLiteralContext;
  class ListLiteralContext;
  class SetLiteralContext;
  class MapLiteralContext;
  class MapEntryListContext;
  class MapEntryContext;
  class StructInitListContext;
  class StructInitContext;
  class ConstExprContext;
  class TemporalExprContext;
  class WorkflowTemporalExprContext;
  class TemporalImpliesExprContext;
  class TemporalOrExprContext;
  class TemporalAndExprContext;
  class TemporalUntilExprContext;
  class TemporalUnaryExprContext;
  class TemporalAtomContext;

  class  ProgramContext : public antlr4::ParserRuleContext {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<ModuleDeclContext *> moduleDecl();
    ModuleDeclContext* moduleDecl(size_t i);
    std::vector<ImportDeclContext *> importDecl();
    ImportDeclContext* importDecl(size_t i);
    std::vector<TopLevelDeclContext *> topLevelDecl();
    TopLevelDeclContext* topLevelDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ProgramContext* program();

  class  TopLevelDeclContext : public antlr4::ParserRuleContext {
  public:
    TopLevelDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ConstDeclContext *constDecl();
    TypeAliasDeclContext *typeAliasDecl();
    StructDeclContext *structDecl();
    EnumDeclContext *enumDecl();
    CapabilityDeclContext *capabilityDecl();
    PredicateDeclContext *predicateDecl();
    AgentDeclContext *agentDecl();
    ContractDeclContext *contractDecl();
    FlowDeclContext *flowDecl();
    WorkflowDeclContext *workflowDecl();
    FnDeclContext *fnDecl();
    TraitDeclContext *traitDecl();
    ImplDeclContext *implDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TopLevelDeclContext* topLevelDecl();

  class  ModuleDeclContext : public antlr4::ParserRuleContext {
  public:
    ModuleDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ModuleDeclContext* moduleDecl();

  class  ImportDeclContext : public antlr4::ParserRuleContext {
  public:
    ImportDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    IdentifierContext *identifier();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImportDeclContext* importDecl();

  class  IdentifierContext : public antlr4::ParserRuleContext {
  public:
    IdentifierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  IdentifierContext* identifier();

  class  QualifiedIdentContext : public antlr4::ParserRuleContext {
  public:
    QualifiedIdentContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentifierContext *> identifier();
    IdentifierContext* identifier(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QualifiedIdentContext* qualifiedIdent();

  class  QualifiedIdentListContext : public antlr4::ParserRuleContext {
  public:
    QualifiedIdentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<QualifiedIdentContext *> qualifiedIdent();
    QualifiedIdentContext* qualifiedIdent(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QualifiedIdentListContext* qualifiedIdentList();

  class  IdentListContext : public antlr4::ParserRuleContext {
  public:
    IdentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  IdentListContext* identList();

  class  IdentListOptContext : public antlr4::ParserRuleContext {
  public:
    IdentListOptContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  IdentListOptContext* identListOpt();

  class  QualifiedIdentListOptContext : public antlr4::ParserRuleContext {
  public:
    QualifiedIdentListOptContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentListContext *qualifiedIdentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QualifiedIdentListOptContext* qualifiedIdentListOpt();

  class  Type_Context : public antlr4::ParserRuleContext {
  public:
    Type_Context(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimitiveTypeContext *primitiveType();
    FnTypeContext *fnType();
    QualifiedIdentContext *qualifiedIdent();
    std::vector<Type_Context *> type_();
    Type_Context* type_(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  Type_Context* type_();

  class  PrimitiveTypeContext : public antlr4::ParserRuleContext {
  public:
    PrimitiveTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> INT_LITERAL();
    antlr4::tree::TerminalNode* INT_LITERAL(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PrimitiveTypeContext* primitiveType();

  class  ConstDeclContext : public antlr4::ParserRuleContext {
  public:
    ConstDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();
    ConstExprContext *constExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ConstDeclContext* constDecl();

  class  TypeAliasDeclContext : public antlr4::ParserRuleContext {
  public:
    TypeAliasDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    Type_Context *type_();
    TypeParamsContext *typeParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeAliasDeclContext* typeAliasDecl();

  class  StructDeclContext : public antlr4::ParserRuleContext {
  public:
    StructDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    TypeParamsContext *typeParams();
    std::vector<StructFieldDeclContext *> structFieldDecl();
    StructFieldDeclContext* structFieldDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructDeclContext* structDecl();

  class  StructFieldDeclContext : public antlr4::ParserRuleContext {
  public:
    StructFieldDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();
    ConstExprContext *constExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructFieldDeclContext* structFieldDecl();

  class  EnumDeclContext : public antlr4::ParserRuleContext {
  public:
    EnumDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    std::vector<EnumVariantContext *> enumVariant();
    EnumVariantContext* enumVariant(size_t i);
    TypeParamsContext *typeParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EnumDeclContext* enumDecl();

  class  EnumVariantContext : public antlr4::ParserRuleContext {
  public:
    EnumVariantContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    TypeListContext *typeList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EnumVariantContext* enumVariant();

  class  TypeListContext : public antlr4::ParserRuleContext {
  public:
    TypeListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<Type_Context *> type_();
    Type_Context* type_(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeListContext* typeList();

  class  FnTypeContext : public antlr4::ParserRuleContext {
  public:
    FnTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeListContext *typeList();
    Type_Context *type_();
    EffectSpecContext *effectSpec();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FnTypeContext* fnType();

  class  CapabilityDeclContext : public antlr4::ParserRuleContext {
  public:
    CapabilityDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();
    CapabilityEffectBlockContext *capabilityEffectBlock();
    ParamListContext *paramList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityDeclContext* capabilityDecl();

  class  CapabilityEffectBlockContext : public antlr4::ParserRuleContext {
  public:
    CapabilityEffectBlockContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<CapabilityEffectItemContext *> capabilityEffectItem();
    CapabilityEffectItemContext* capabilityEffectItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityEffectBlockContext* capabilityEffectBlock();

  class  CapabilityEffectItemContext : public antlr4::ParserRuleContext {
  public:
    CapabilityEffectItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CapabilityEffectKindContext *capabilityEffectKind();
    QualifiedIdentContext *qualifiedIdent();
    PathExprContext *pathExpr();
    CapabilityReceiptModeContext *capabilityReceiptMode();
    CapabilityRetryModeContext *capabilityRetryMode();
    DurationLiteralContext *durationLiteral();
    QualifiedIdentListOptContext *qualifiedIdentListOpt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityEffectItemContext* capabilityEffectItem();

  class  CapabilityEffectKindContext : public antlr4::ParserRuleContext {
  public:
    CapabilityEffectKindContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityEffectKindContext* capabilityEffectKind();

  class  CapabilityReceiptModeContext : public antlr4::ParserRuleContext {
  public:
    CapabilityReceiptModeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityReceiptModeContext* capabilityReceiptMode();

  class  CapabilityRetryModeContext : public antlr4::ParserRuleContext {
  public:
    CapabilityRetryModeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityRetryModeContext* capabilityRetryMode();

  class  PredicateDeclContext : public antlr4::ParserRuleContext {
  public:
    PredicateDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    ParamListContext *paramList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PredicateDeclContext* predicateDecl();

  class  ParamListContext : public antlr4::ParserRuleContext {
  public:
    ParamListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ParamContext *> param();
    ParamContext* param(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ParamListContext* paramList();

  class  ParamContext : public antlr4::ParserRuleContext {
  public:
    ParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ParamContext* param();

  class  AgentDeclContext : public antlr4::ParserRuleContext {
  public:
    AgentDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    InputDeclContext *inputDecl();
    ContextDeclContext *contextDecl();
    OutputDeclContext *outputDecl();
    StatesDeclContext *statesDecl();
    InitialDeclContext *initialDecl();
    FinalDeclContext *finalDecl();
    CapabilitiesDeclContext *capabilitiesDecl();
    QuotaDeclContext *quotaDecl();
    std::vector<TransitionDeclContext *> transitionDecl();
    TransitionDeclContext* transitionDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AgentDeclContext* agentDecl();

  class  InputDeclContext : public antlr4::ParserRuleContext {
  public:
    InputDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  InputDeclContext* inputDecl();

  class  ContextDeclContext : public antlr4::ParserRuleContext {
  public:
    ContextDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ContextDeclContext* contextDecl();

  class  OutputDeclContext : public antlr4::ParserRuleContext {
  public:
    OutputDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  OutputDeclContext* outputDecl();

  class  StatesDeclContext : public antlr4::ParserRuleContext {
  public:
    StatesDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StatesDeclContext* statesDecl();

  class  InitialDeclContext : public antlr4::ParserRuleContext {
  public:
    InitialDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  InitialDeclContext* initialDecl();

  class  FinalDeclContext : public antlr4::ParserRuleContext {
  public:
    FinalDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FinalDeclContext* finalDecl();

  class  CapabilitiesDeclContext : public antlr4::ParserRuleContext {
  public:
    CapabilitiesDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListOptContext *identListOpt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilitiesDeclContext* capabilitiesDecl();

  class  QuotaDeclContext : public antlr4::ParserRuleContext {
  public:
    QuotaDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<QuotaItemContext *> quotaItem();
    QuotaItemContext* quotaItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QuotaDeclContext* quotaDecl();

  class  QuotaItemContext : public antlr4::ParserRuleContext {
  public:
    QuotaItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IntegerLiteralContext *integerLiteral();
    DurationLiteralContext *durationLiteral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QuotaItemContext* quotaItem();

  class  TransitionDeclContext : public antlr4::ParserRuleContext {
  public:
    TransitionDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TransitionDeclContext* transitionDecl();

  class  ContractDeclContext : public antlr4::ParserRuleContext {
  public:
    ContractDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    std::vector<ContractItemContext *> contractItem();
    ContractItemContext* contractItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ContractDeclContext* contractDecl();

  class  ContractItemContext : public antlr4::ParserRuleContext {
  public:
    ContractItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RequiresDeclContext *requiresDecl();
    EnsuresDeclContext *ensuresDecl();
    InvariantDeclContext *invariantDecl();
    ForbidDeclContext *forbidDecl();
    DecreasesDeclContext *decreasesDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ContractItemContext* contractItem();

  class  RequiresDeclContext : public antlr4::ParserRuleContext {
  public:
    RequiresDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  RequiresDeclContext* requiresDecl();

  class  EnsuresDeclContext : public antlr4::ParserRuleContext {
  public:
    EnsuresDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EnsuresDeclContext* ensuresDecl();

  class  InvariantDeclContext : public antlr4::ParserRuleContext {
  public:
    InvariantDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalExprContext *temporalExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  InvariantDeclContext* invariantDecl();

  class  ForbidDeclContext : public antlr4::ParserRuleContext {
  public:
    ForbidDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalExprContext *temporalExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ForbidDeclContext* forbidDecl();

  class  DecreasesDeclContext : public antlr4::ParserRuleContext {
  public:
    DecreasesDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  DecreasesDeclContext* decreasesDecl();

  class  FlowDeclContext : public antlr4::ParserRuleContext {
  public:
    FlowDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    std::vector<StateHandlerContext *> stateHandler();
    StateHandlerContext* stateHandler(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FlowDeclContext* flowDecl();

  class  StateHandlerContext : public antlr4::ParserRuleContext {
  public:
    StateHandlerContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    BlockContext *block();
    StatePolicyContext *statePolicy();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StateHandlerContext* stateHandler();

  class  StatePolicyContext : public antlr4::ParserRuleContext {
  public:
    StatePolicyContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StatePolicyItemContext *> statePolicyItem();
    StatePolicyItemContext* statePolicyItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StatePolicyContext* statePolicy();

  class  StatePolicyItemContext : public antlr4::ParserRuleContext {
  public:
    StatePolicyItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IntegerLiteralContext *integerLiteral();
    QualifiedIdentListOptContext *qualifiedIdentListOpt();
    DurationLiteralContext *durationLiteral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StatePolicyItemContext* statePolicyItem();

  class  WorkflowDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    WorkflowInputDeclContext *workflowInputDecl();
    WorkflowOutputDeclContext *workflowOutputDecl();
    WorkflowReturnDeclContext *workflowReturnDecl();
    std::vector<WorkflowItemContext *> workflowItem();
    WorkflowItemContext* workflowItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowDeclContext* workflowDecl();

  class  WorkflowInputDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowInputDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowInputDeclContext* workflowInputDecl();

  class  WorkflowOutputDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowOutputDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowOutputDeclContext* workflowOutputDecl();

  class  WorkflowItemContext : public antlr4::ParserRuleContext {
  public:
    WorkflowItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    WorkflowNodeDeclContext *workflowNodeDecl();
    WorkflowSafetyDeclContext *workflowSafetyDecl();
    WorkflowLivenessDeclContext *workflowLivenessDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowItemContext* workflowItem();

  class  WorkflowNodeDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowNodeDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    QualifiedIdentContext *qualifiedIdent();
    ExprContext *expr();
    IdentListOptContext *identListOpt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowNodeDeclContext* workflowNodeDecl();

  class  WorkflowSafetyDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowSafetyDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    WorkflowTemporalExprContext *workflowTemporalExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowSafetyDeclContext* workflowSafetyDecl();

  class  WorkflowLivenessDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowLivenessDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    WorkflowTemporalExprContext *workflowTemporalExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowLivenessDeclContext* workflowLivenessDecl();

  class  WorkflowReturnDeclContext : public antlr4::ParserRuleContext {
  public:
    WorkflowReturnDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowReturnDeclContext* workflowReturnDecl();

  class  BuiltinAttrContext : public antlr4::ParserRuleContext {
  public:
    BuiltinAttrContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *STRING_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  BuiltinAttrContext* builtinAttr();

  class  FnDeclContext : public antlr4::ParserRuleContext {
  public:
    FnDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    FnBodyContext *fnBody();
    antlr4::tree::TerminalNode *DOC_COMMENT();
    BuiltinAttrContext *builtinAttr();
    TypeParamsContext *typeParams();
    ParamListContext *paramList();
    Type_Context *type_();
    EffectClauseContext *effectClause();
    WhereClauseContext *whereClause();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FnDeclContext* fnDecl();

  class  TypeParamsContext : public antlr4::ParserRuleContext {
  public:
    TypeParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TypeParamContext *> typeParam();
    TypeParamContext* typeParam(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeParamsContext* typeParams();

  class  TypeParamContext : public antlr4::ParserRuleContext {
  public:
    TypeParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    TypeBoundListContext *typeBoundList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeParamContext* typeParam();

  class  TypeBoundListContext : public antlr4::ParserRuleContext {
  public:
    TypeBoundListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<Type_Context *> type_();
    Type_Context* type_(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeBoundListContext* typeBoundList();

  class  FnBodyContext : public antlr4::ParserRuleContext {
  public:
    FnBodyContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BlockContext *block();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FnBodyContext* fnBody();

  class  EffectClauseContext : public antlr4::ParserRuleContext {
  public:
    EffectClauseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EffectSpecContext *effectSpec();
    DecreasesClauseContext *decreasesClause();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EffectClauseContext* effectClause();

  class  EffectSpecContext : public antlr4::ParserRuleContext {
  public:
    EffectSpecContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<CapabilityRefContext *> capabilityRef();
    CapabilityRefContext* capabilityRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EffectSpecContext* effectSpec();

  class  CapabilityRefContext : public antlr4::ParserRuleContext {
  public:
    CapabilityRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityRefContext* capabilityRef();

  class  DecreasesClauseContext : public antlr4::ParserRuleContext {
  public:
    DecreasesClauseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  DecreasesClauseContext* decreasesClause();

  class  WhereClauseContext : public antlr4::ParserRuleContext {
  public:
    WhereClauseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<WhereConstraintContext *> whereConstraint();
    WhereConstraintContext* whereConstraint(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WhereClauseContext* whereClause();

  class  WhereConstraintContext : public antlr4::ParserRuleContext {
  public:
    WhereConstraintContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();
    IdentifierContext *identifier();
    TypeListContext *typeList();
    TypeBoundListContext *typeBoundList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WhereConstraintContext* whereConstraint();

  class  LambdaExprContext : public antlr4::ParserRuleContext {
  public:
    LambdaExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *BACKSLASH();
    ExprContext *expr();
    LambdaParamListContext *lambdaParamList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LambdaExprContext* lambdaExpr();

  class  LambdaParamListContext : public antlr4::ParserRuleContext {
  public:
    LambdaParamListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<LambdaParamContext *> lambdaParam();
    LambdaParamContext* lambdaParam(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LambdaParamListContext* lambdaParamList();

  class  LambdaParamContext : public antlr4::ParserRuleContext {
  public:
    LambdaParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LambdaParamContext* lambdaParam();

  class  TraitDeclContext : public antlr4::ParserRuleContext {
  public:
    TraitDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *DOC_COMMENT();
    TypeParamsContext *typeParams();
    TypeBoundListContext *typeBoundList();
    std::vector<TraitItemContext *> traitItem();
    TraitItemContext* traitItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TraitDeclContext* traitDecl();

  class  TraitItemContext : public antlr4::ParserRuleContext {
  public:
    TraitItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TraitFnItemContext *traitFnItem();
    AssocTypeItemContext *assocTypeItem();
    AssocConstItemContext *assocConstItem();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TraitItemContext* traitItem();

  class  TraitFnItemContext : public antlr4::ParserRuleContext {
  public:
    TraitFnItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    TypeParamsContext *typeParams();
    ParamListContext *paramList();
    Type_Context *type_();
    EffectClauseContext *effectClause();
    WhereClauseContext *whereClause();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TraitFnItemContext* traitFnItem();

  class  AssocTypeItemContext : public antlr4::ParserRuleContext {
  public:
    AssocTypeItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    TypeParamsContext *typeParams();
    TypeBoundListContext *typeBoundList();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssocTypeItemContext* assocTypeItem();

  class  AssocConstItemContext : public antlr4::ParserRuleContext {
  public:
    AssocConstItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    Type_Context *type_();
    ConstExprContext *constExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssocConstItemContext* assocConstItem();

  class  ImplDeclContext : public antlr4::ParserRuleContext {
  public:
    ImplDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();
    antlr4::tree::TerminalNode *DOC_COMMENT();
    TypeParamsContext *typeParams();
    TraitRefContext *traitRef();
    WhereClauseContext *whereClause();
    std::vector<ImplItemContext *> implItem();
    ImplItemContext* implItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImplDeclContext* implDecl();

  class  TraitRefContext : public antlr4::ParserRuleContext {
  public:
    TraitRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TraitRefContext* traitRef();

  class  ImplItemContext : public antlr4::ParserRuleContext {
  public:
    ImplItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ImplFnItemContext *implFnItem();
    AssocTypeDefContext *assocTypeDef();
    AssocConstDefContext *assocConstDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImplItemContext* implItem();

  class  ImplFnItemContext : public antlr4::ParserRuleContext {
  public:
    ImplFnItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    FnBodyContext *fnBody();
    BuiltinAttrContext *builtinAttr();
    TypeParamsContext *typeParams();
    ParamListContext *paramList();
    Type_Context *type_();
    EffectClauseContext *effectClause();
    WhereClauseContext *whereClause();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImplFnItemContext* implFnItem();

  class  AssocTypeDefContext : public antlr4::ParserRuleContext {
  public:
    AssocTypeDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssocTypeDefContext* assocTypeDef();

  class  AssocConstDefContext : public antlr4::ParserRuleContext {
  public:
    AssocConstDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    Type_Context *type_();
    ConstExprContext *constExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssocConstDefContext* assocConstDef();

  class  BlockContext : public antlr4::ParserRuleContext {
  public:
    BlockContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  BlockContext* block();

  class  StatementContext : public antlr4::ParserRuleContext {
  public:
    StatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetStmtContext *letStmt();
    AssignStmtContext *assignStmt();
    IfStmtContext *ifStmt();
    GotoStmtContext *gotoStmt();
    ReturnStmtContext *returnStmt();
    AssertStmtContext *assertStmt();
    ExprStmtContext *exprStmt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StatementContext* statement();

  class  LetStmtContext : public antlr4::ParserRuleContext {
  public:
    LetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    ExprContext *expr();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LetStmtContext* letStmt();

  class  AssignStmtContext : public antlr4::ParserRuleContext {
  public:
    AssignStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LValueContext *lValue();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssignStmtContext* assignStmt();

  class  IfStmtContext : public antlr4::ParserRuleContext {
  public:
    IfStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    std::vector<BlockContext *> block();
    BlockContext* block(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  IfStmtContext* ifStmt();

  class  GotoStmtContext : public antlr4::ParserRuleContext {
  public:
    GotoStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  GotoStmtContext* gotoStmt();

  class  ReturnStmtContext : public antlr4::ParserRuleContext {
  public:
    ReturnStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ReturnStmtContext* returnStmt();

  class  AssertStmtContext : public antlr4::ParserRuleContext {
  public:
    AssertStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AssertStmtContext* assertStmt();

  class  ExprStmtContext : public antlr4::ParserRuleContext {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ExprStmtContext* exprStmt();

  class  LValueContext : public antlr4::ParserRuleContext {
  public:
    LValueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PathExprContext *pathExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LValueContext* lValue();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ImpliesExprContext *impliesExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ExprContext* expr();

  class  ImpliesExprContext : public antlr4::ParserRuleContext {
  public:
    ImpliesExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<OrExprContext *> orExpr();
    OrExprContext* orExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImpliesExprContext* impliesExpr();

  class  OrExprContext : public antlr4::ParserRuleContext {
  public:
    OrExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AndExprContext *> andExpr();
    AndExprContext* andExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  OrExprContext* orExpr();

  class  AndExprContext : public antlr4::ParserRuleContext {
  public:
    AndExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EqualityExprContext *> equalityExpr();
    EqualityExprContext* equalityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AndExprContext* andExpr();

  class  EqualityExprContext : public antlr4::ParserRuleContext {
  public:
    EqualityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<CompareExprContext *> compareExpr();
    CompareExprContext* compareExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EqualityExprContext* equalityExpr();

  class  CompareExprContext : public antlr4::ParserRuleContext {
  public:
    CompareExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AddExprContext *> addExpr();
    AddExprContext* addExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CompareExprContext* compareExpr();

  class  AddExprContext : public antlr4::ParserRuleContext {
  public:
    AddExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MulExprContext *> mulExpr();
    MulExprContext* mulExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  AddExprContext* addExpr();

  class  MulExprContext : public antlr4::ParserRuleContext {
  public:
    MulExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<UnaryExprContext *> unaryExpr();
    UnaryExprContext* unaryExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MulExprContext* mulExpr();

  class  UnaryExprContext : public antlr4::ParserRuleContext {
  public:
    UnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnaryExprContext *unaryExpr();
    PostfixExprContext *postfixExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  UnaryExprContext* unaryExpr();

  class  PostfixExprContext : public antlr4::ParserRuleContext {
  public:
    PostfixExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryExprContext *primaryExpr();
    std::vector<IdentifierContext *> identifier();
    IdentifierContext* identifier(size_t i);
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<TypeListContext *> typeList();
    TypeListContext* typeList(size_t i);
    std::vector<ExprListContext *> exprList();
    ExprListContext* exprList(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PostfixExprContext* postfixExpr();

  class  PrimaryExprContext : public antlr4::ParserRuleContext {
  public:
    PrimaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LiteralContext *literal();
    CallExprContext *callExpr();
    StructLiteralContext *structLiteral();
    QualifiedValueExprContext *qualifiedValueExpr();
    PathExprContext *pathExpr();
    MatchExprContext *matchExpr();
    LambdaExprContext *lambdaExpr();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PrimaryExprContext* primaryExpr();

  class  MatchExprContext : public antlr4::ParserRuleContext {
  public:
    MatchExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    std::vector<MatchArmContext *> matchArm();
    MatchArmContext* matchArm(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MatchExprContext* matchExpr();

  class  MatchArmContext : public antlr4::ParserRuleContext {
  public:
    MatchArmContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PatternContext *pattern();
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MatchArmContext* matchArm();

  class  PatternContext : public antlr4::ParserRuleContext {
  public:
    PatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    OrPatternContext *orPattern();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PatternContext* pattern();

  class  OrPatternContext : public antlr4::ParserRuleContext {
  public:
    OrPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ConcatPatternContext *> concatPattern();
    ConcatPatternContext* concatPattern(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  OrPatternContext* orPattern();

  class  ConcatPatternContext : public antlr4::ParserRuleContext {
  public:
    ConcatPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LiteralPatternContext *literalPattern();
    VariantPatternContext *variantPattern();
    WildcardPatternContext *wildcardPattern();
    BindingPatternContext *bindingPattern();
    TuplePatternContext *tuplePattern();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ConcatPatternContext* concatPattern();

  class  LiteralPatternContext : public antlr4::ParserRuleContext {
  public:
    LiteralPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IntegerLiteralContext *integerLiteral();
    FloatLiteralContext *floatLiteral();
    StringLiteralContext *stringLiteral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LiteralPatternContext* literalPattern();

  class  VariantPatternContext : public antlr4::ParserRuleContext {
  public:
    VariantPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    PatternListContext *patternList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  VariantPatternContext* variantPattern();

  class  WildcardPatternContext : public antlr4::ParserRuleContext {
  public:
    WildcardPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WildcardPatternContext* wildcardPattern();

  class  BindingPatternContext : public antlr4::ParserRuleContext {
  public:
    BindingPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    ConcatPatternContext *concatPattern();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  BindingPatternContext* bindingPattern();

  class  TuplePatternContext : public antlr4::ParserRuleContext {
  public:
    TuplePatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PatternListContext *patternList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TuplePatternContext* tuplePattern();

  class  PatternListContext : public antlr4::ParserRuleContext {
  public:
    PatternListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<PatternContext *> pattern();
    PatternContext* pattern(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PatternListContext* patternList();

  class  PathExprContext : public antlr4::ParserRuleContext {
  public:
    PathExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PathRootContext *pathRoot();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PathExprContext* pathExpr();

  class  PathRootContext : public antlr4::ParserRuleContext {
  public:
    PathRootContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PathRootContext* pathRoot();

  class  QualifiedValueExprContext : public antlr4::ParserRuleContext {
  public:
    QualifiedValueExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentifierContext *> identifier();
    IdentifierContext* identifier(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QualifiedValueExprContext* qualifiedValueExpr();

  class  CallExprContext : public antlr4::ParserRuleContext {
  public:
    CallExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    TypeListContext *typeList();
    ExprListContext *exprList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CallExprContext* callExpr();

  class  ExprListContext : public antlr4::ParserRuleContext {
  public:
    ExprListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ExprListContext* exprList();

  class  LiteralContext : public antlr4::ParserRuleContext {
  public:
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IntegerLiteralContext *integerLiteral();
    FloatLiteralContext *floatLiteral();
    DecimalLiteralContext *decimalLiteral();
    StringLiteralContext *stringLiteral();
    DurationLiteralContext *durationLiteral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  LiteralContext* literal();

  class  IntegerLiteralContext : public antlr4::ParserRuleContext {
  public:
    IntegerLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INT_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  IntegerLiteralContext* integerLiteral();

  class  FloatLiteralContext : public antlr4::ParserRuleContext {
  public:
    FloatLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FLOAT_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  FloatLiteralContext* floatLiteral();

  class  DecimalLiteralContext : public antlr4::ParserRuleContext {
  public:
    DecimalLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DECIMAL_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  DecimalLiteralContext* decimalLiteral();

  class  StringLiteralContext : public antlr4::ParserRuleContext {
  public:
    StringLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *STRING_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StringLiteralContext* stringLiteral();

  class  DurationLiteralContext : public antlr4::ParserRuleContext {
  public:
    DurationLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DURATION_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  DurationLiteralContext* durationLiteral();

  class  StructLiteralContext : public antlr4::ParserRuleContext {
  public:
    StructLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    StructInitListContext *structInitList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructLiteralContext* structLiteral();

  class  ListLiteralContext : public antlr4::ParserRuleContext {
  public:
    ListLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprListContext *exprList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ListLiteralContext* listLiteral();

  class  SetLiteralContext : public antlr4::ParserRuleContext {
  public:
    SetLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprListContext *exprList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  SetLiteralContext* setLiteral();

  class  MapLiteralContext : public antlr4::ParserRuleContext {
  public:
    MapLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MapEntryListContext *mapEntryList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MapLiteralContext* mapLiteral();

  class  MapEntryListContext : public antlr4::ParserRuleContext {
  public:
    MapEntryListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MapEntryContext *> mapEntry();
    MapEntryContext* mapEntry(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MapEntryListContext* mapEntryList();

  class  MapEntryContext : public antlr4::ParserRuleContext {
  public:
    MapEntryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  MapEntryContext* mapEntry();

  class  StructInitListContext : public antlr4::ParserRuleContext {
  public:
    StructInitListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StructInitContext *> structInit();
    StructInitContext* structInit(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructInitListContext* structInitList();

  class  StructInitContext : public antlr4::ParserRuleContext {
  public:
    StructInitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructInitContext* structInit();

  class  ConstExprContext : public antlr4::ParserRuleContext {
  public:
    ConstExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ConstExprContext* constExpr();

  class  TemporalExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalImpliesExprContext *temporalImpliesExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalExprContext* temporalExpr();

  class  WorkflowTemporalExprContext : public antlr4::ParserRuleContext {
  public:
    WorkflowTemporalExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalExprContext *temporalExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  WorkflowTemporalExprContext* workflowTemporalExpr();

  class  TemporalImpliesExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalImpliesExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TemporalOrExprContext *> temporalOrExpr();
    TemporalOrExprContext* temporalOrExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalImpliesExprContext* temporalImpliesExpr();

  class  TemporalOrExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalOrExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TemporalAndExprContext *> temporalAndExpr();
    TemporalAndExprContext* temporalAndExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalOrExprContext* temporalOrExpr();

  class  TemporalAndExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalAndExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TemporalUntilExprContext *> temporalUntilExpr();
    TemporalUntilExprContext* temporalUntilExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalAndExprContext* temporalAndExpr();

  class  TemporalUntilExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalUntilExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TemporalUnaryExprContext *> temporalUnaryExpr();
    TemporalUnaryExprContext* temporalUnaryExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalUntilExprContext* temporalUntilExpr();

  class  TemporalUnaryExprContext : public antlr4::ParserRuleContext {
  public:
    TemporalUnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalUnaryExprContext *temporalUnaryExpr();
    TemporalAtomContext *temporalAtom();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalUnaryExprContext* temporalUnaryExpr();

  class  TemporalAtomContext : public antlr4::ParserRuleContext {
  public:
    TemporalAtomContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TemporalExprContext *temporalExpr();
    ExprContext *expr();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TemporalAtomContext* temporalAtom();


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

