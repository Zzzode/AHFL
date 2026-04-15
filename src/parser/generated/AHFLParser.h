
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
    T__98 = 99, T__99 = 100, T__100 = 101, T__101 = 102, DURATION_LITERAL = 103,
    DECIMAL_LITERAL = 104, FLOAT_LITERAL = 105, INT_LITERAL = 106, STRING_LITERAL = 107,
    IDENT = 108, DOC_COMMENT = 109, LINE_COMMENT = 110, BLOCK_COMMENT = 111,
    WS = 112
  };

  enum {
    RuleProgram = 0, RuleTopLevelDecl = 1, RuleModuleDecl = 2, RuleImportDecl = 3,
    RuleQualifiedIdent = 4, RuleQualifiedIdentList = 5, RuleIdentList = 6,
    RuleIdentListOpt = 7, RuleQualifiedIdentListOpt = 8, RuleType_ = 9,
    RulePrimitiveType = 10, RuleConstDecl = 11, RuleTypeAliasDecl = 12,
    RuleStructDecl = 13, RuleStructFieldDecl = 14, RuleEnumDecl = 15, RuleEnumVariant = 16,
    RuleCapabilityDecl = 17, RulePredicateDecl = 18, RuleParamList = 19,
    RuleParam = 20, RuleAgentDecl = 21, RuleInputDecl = 22, RuleContextDecl = 23,
    RuleOutputDecl = 24, RuleStatesDecl = 25, RuleInitialDecl = 26, RuleFinalDecl = 27,
    RuleCapabilitiesDecl = 28, RuleQuotaDecl = 29, RuleQuotaItem = 30, RuleTransitionDecl = 31,
    RuleContractDecl = 32, RuleContractItem = 33, RuleRequiresDecl = 34,
    RuleEnsuresDecl = 35, RuleInvariantDecl = 36, RuleForbidDecl = 37, RuleFlowDecl = 38,
    RuleStateHandler = 39, RuleStatePolicy = 40, RuleStatePolicyItem = 41,
    RuleWorkflowDecl = 42, RuleWorkflowInputDecl = 43, RuleWorkflowOutputDecl = 44,
    RuleWorkflowItem = 45, RuleWorkflowNodeDecl = 46, RuleWorkflowSafetyDecl = 47,
    RuleWorkflowLivenessDecl = 48, RuleWorkflowReturnDecl = 49, RuleBlock = 50,
    RuleStatement = 51, RuleLetStmt = 52, RuleAssignStmt = 53, RuleIfStmt = 54,
    RuleGotoStmt = 55, RuleReturnStmt = 56, RuleAssertStmt = 57, RuleExprStmt = 58,
    RuleLValue = 59, RuleExpr = 60, RuleImpliesExpr = 61, RuleOrExpr = 62,
    RuleAndExpr = 63, RuleEqualityExpr = 64, RuleCompareExpr = 65, RuleAddExpr = 66,
    RuleMulExpr = 67, RuleUnaryExpr = 68, RulePostfixExpr = 69, RulePrimaryExpr = 70,
    RulePathExpr = 71, RulePathRoot = 72, RuleQualifiedValueExpr = 73, RuleCallExpr = 74,
    RuleExprList = 75, RuleLiteral = 76, RuleIntegerLiteral = 77, RuleFloatLiteral = 78,
    RuleDecimalLiteral = 79, RuleStringLiteral = 80, RuleDurationLiteral = 81,
    RuleListLiteral = 82, RuleSetLiteral = 83, RuleMapLiteral = 84, RuleMapEntryList = 85,
    RuleMapEntry = 86, RuleStructLiteral = 87, RuleStructInitList = 88,
    RuleStructInit = 89, RuleConstExpr = 90, RuleTemporalExpr = 91, RuleWorkflowTemporalExpr = 92,
    RuleTemporalImpliesExpr = 93, RuleTemporalOrExpr = 94, RuleTemporalAndExpr = 95,
    RuleTemporalUntilExpr = 96, RuleTemporalUnaryExpr = 97, RuleTemporalAtom = 98
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
  class CapabilityDeclContext;
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
  class ListLiteralContext;
  class SetLiteralContext;
  class MapLiteralContext;
  class MapEntryListContext;
  class MapEntryContext;
  class StructLiteralContext;
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
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  ImportDeclContext* importDecl();

  class  QualifiedIdentContext : public antlr4::ParserRuleContext {
  public:
    QualifiedIdentContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


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
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  TypeAliasDeclContext* typeAliasDecl();

  class  StructDeclContext : public antlr4::ParserRuleContext {
  public:
    StructDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
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
    antlr4::tree::TerminalNode *IDENT();
    std::vector<EnumVariantContext *> enumVariant();
    EnumVariantContext* enumVariant(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EnumDeclContext* enumDecl();

  class  EnumVariantContext : public antlr4::ParserRuleContext {
  public:
    EnumVariantContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  EnumVariantContext* enumVariant();

  class  CapabilityDeclContext : public antlr4::ParserRuleContext {
  public:
    CapabilityDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENT();
    Type_Context *type_();
    ParamListContext *paramList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  CapabilityDeclContext* capabilityDecl();

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
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


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
    ListLiteralContext *listLiteral();
    SetLiteralContext *setLiteral();
    MapLiteralContext *mapLiteral();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  PrimaryExprContext* primaryExpr();

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
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  QualifiedValueExprContext* qualifiedValueExpr();

  class  CallExprContext : public antlr4::ParserRuleContext {
  public:
    CallExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
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

  class  StructLiteralContext : public antlr4::ParserRuleContext {
  public:
    StructLiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QualifiedIdentContext *qualifiedIdent();
    StructInitListContext *structInitList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;

  };

  StructLiteralContext* structLiteral();

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

