#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl::ast {

// Hand-written AST nodes form the stable syntax boundary after parse-tree lowering.
// Public AST types must not retain generated ANTLR parser contexts, tokens, or
// semantic/type-check state.

class Visitor;

enum class NodeKind {
    Program,
    ModuleDecl,
    ImportDecl,
    ConstDecl,
    TypeAliasDecl,
    StructDecl,
    EnumDecl,
    CapabilityDecl,
    PredicateDecl,
    AgentDecl,
    ContractDecl,
    FlowDecl,
    WorkflowDecl,
};

[[nodiscard]] std::string_view to_string(NodeKind kind) noexcept;

enum class ContractClauseKind {
    Requires,
    Ensures,
    Invariant,
    Forbid,
};

[[nodiscard]] std::string_view to_string(ContractClauseKind kind) noexcept;

enum class TypeSyntaxKind {
    Unit,
    Bool,
    Int,
    Float,
    String,
    BoundedString,
    UUID,
    Timestamp,
    Duration,
    Decimal,
    Named,
    Optional,
    List,
    Set,
    Map,
};

[[nodiscard]] std::string_view to_string(TypeSyntaxKind kind) noexcept;

enum class AgentQuotaItemKind {
    MaxToolCalls,
    MaxExecutionTime,
};

enum class StatePolicyItemKind {
    Retry,
    RetryOn,
    Timeout,
};

enum class PathRootKind {
    Identifier,
    Input,
    Output,
};

enum class ExprSyntaxKind {
    BoolLiteral,
    IntegerLiteral,
    FloatLiteral,
    DecimalLiteral,
    StringLiteral,
    DurationLiteral,
    NoneLiteral,
    Some,
    Path,
    QualifiedValue,
    Call,
    StructLiteral,
    ListLiteral,
    SetLiteral,
    MapLiteral,
    Unary,
    Binary,
    MemberAccess,
    IndexAccess,
    Group,
};

enum class ExprUnaryOp {
    Not,
    Negate,
    Positive,
};

enum class ExprBinaryOp {
    Implies,
    Or,
    And,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
};

enum class StatementSyntaxKind {
    Let,
    Assign,
    If,
    Goto,
    Return,
    Assert,
    Expr,
};

enum class TemporalExprSyntaxKind {
    EmbeddedExpr,
    Called,
    InState,
    Running,
    Completed,
    Unary,
    Binary,
};

enum class TemporalUnaryOp {
    Always,
    Eventually,
    Next,
    Not,
};

enum class TemporalBinaryOp {
    Implies,
    Or,
    And,
    Until,
};

struct QualifiedName {
    ahfl::SourceRange range;
    std::vector<std::string> segments;

    [[nodiscard]] std::string spelling() const;
};

struct TypeSyntax {
    ahfl::SourceRange range;
    TypeSyntaxKind kind{TypeSyntaxKind::Named};
    Owned<QualifiedName> name;
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
    std::optional<std::int64_t> decimal_scale;
    Owned<TypeSyntax> first;
    Owned<TypeSyntax> second;

    [[nodiscard]] std::string spelling() const;
};

struct ExprSyntax;
struct StatementSyntax;
struct IntegerSyntax;
struct DurationSyntax;

struct PathSyntax {
    ahfl::SourceRange range;
    PathRootKind root_kind{PathRootKind::Identifier};
    std::string root_name;
    std::vector<std::string> members;

    [[nodiscard]] std::string spelling() const;
};

struct MapEntrySyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> key;
    Owned<ExprSyntax> value;
};

struct StructInitSyntax {
    ahfl::SourceRange range;
    std::string field_name;
    Owned<ExprSyntax> value;
};

struct ExprSyntax {
    ahfl::SourceRange range;
    std::string text;
    ExprSyntaxKind kind{ExprSyntaxKind::NoneLiteral};
    bool bool_value{false};
    ExprUnaryOp unary_op{ExprUnaryOp::Not};
    ExprBinaryOp binary_op{ExprBinaryOp::Implies};
    Owned<IntegerSyntax> integer_literal;
    Owned<DurationSyntax> duration_literal;
    std::string name;
    Owned<QualifiedName> qualified_name;
    Owned<PathSyntax> path;
    std::vector<Owned<ExprSyntax>> items;
    std::vector<Owned<MapEntrySyntax>> map_entries;
    std::vector<Owned<StructInitSyntax>> struct_fields;
    Owned<ExprSyntax> first;
    Owned<ExprSyntax> second;
};

struct LetStmtSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> initializer;
};

struct AssignStmtSyntax {
    ahfl::SourceRange range;
    Owned<PathSyntax> target;
    Owned<ExprSyntax> value;
};

struct BlockSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatementSyntax>> statements;
};

struct IfStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition;
    Owned<BlockSyntax> then_block;
    Owned<BlockSyntax> else_block;
};

struct GotoStmtSyntax {
    ahfl::SourceRange range;
    std::string target_state;
};

struct ReturnStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> value;
};

struct AssertStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition;
};

struct ExprStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> expr;
};

struct StatementSyntax {
    ahfl::SourceRange range;
    StatementSyntaxKind kind{StatementSyntaxKind::Expr};
    Owned<LetStmtSyntax> let_stmt;
    Owned<AssignStmtSyntax> assign_stmt;
    Owned<IfStmtSyntax> if_stmt;
    Owned<GotoStmtSyntax> goto_stmt;
    Owned<ReturnStmtSyntax> return_stmt;
    Owned<AssertStmtSyntax> assert_stmt;
    Owned<ExprStmtSyntax> expr_stmt;
};

struct TemporalExprSyntax {
    ahfl::SourceRange range;
    std::string text;
    TemporalExprSyntaxKind kind{TemporalExprSyntaxKind::EmbeddedExpr};
    TemporalUnaryOp unary_op{TemporalUnaryOp::Always};
    TemporalBinaryOp binary_op{TemporalBinaryOp::Implies};
    std::string name;
    std::optional<std::string> state_name;
    Owned<ExprSyntax> expr;
    Owned<TemporalExprSyntax> first;
    Owned<TemporalExprSyntax> second;
};

struct IntegerSyntax {
    ahfl::SourceRange range;
    std::string spelling;
    std::int64_t value{0};
};

struct DurationSyntax {
    ahfl::SourceRange range;
    std::string spelling;
};

struct ParamDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
};

struct StructFieldDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> default_value;
};

struct EnumVariantDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
};

struct TransitionSyntax {
    ahfl::SourceRange range;
    std::string from_state;
    std::string to_state;
};

struct AgentQuotaItemSyntax {
    ahfl::SourceRange range;
    AgentQuotaItemKind kind{AgentQuotaItemKind::MaxToolCalls};
    Owned<IntegerSyntax> integer_value;
    Owned<DurationSyntax> duration_value;
};

struct AgentQuotaSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<AgentQuotaItemSyntax>> items;
};

struct ContractClauseSyntax {
    ahfl::SourceRange range;
    ContractClauseKind kind{ContractClauseKind::Requires};
    Owned<ExprSyntax> expr;
    Owned<TemporalExprSyntax> temporal_expr;
};

struct StatePolicyItemSyntax {
    ahfl::SourceRange range;
    StatePolicyItemKind kind{StatePolicyItemKind::Retry};
    Owned<IntegerSyntax> retry_limit;
    std::vector<Owned<QualifiedName>> retry_on;
    Owned<DurationSyntax> timeout;
};

struct StatePolicySyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatePolicyItemSyntax>> items;
};

struct StateHandlerSyntax {
    ahfl::SourceRange range;
    std::string state_name;
    Owned<StatePolicySyntax> policy;
    Owned<BlockSyntax> body;
};

struct WorkflowNodeDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<QualifiedName> target;
    Owned<ExprSyntax> input;
    std::vector<std::string> after;
};

struct Node {
    NodeKind kind;
    ahfl::SourceRange range;

    explicit Node(NodeKind kind, ahfl::SourceRange range = {});
    virtual ~Node() = default;

    virtual void accept(Visitor &visitor) = 0;
};

struct Decl : Node {
    // Transitional debug payload. Resolver/checker code must not depend on
    // reparsing raw_text; semantic passes must consume structured AST data.
    std::string raw_text;

    Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range = {});
    ~Decl() override = default;

    [[nodiscard]] virtual std::string headline() const = 0;
};

struct Program final : Node {
    std::string source_name;
    std::vector<Owned<Decl>> declarations;

    explicit Program(std::string source_name, ahfl::SourceRange range = {});

    void accept(Visitor &visitor) override;
};

struct ModuleDecl final : Decl {
    Owned<QualifiedName> name;

    ModuleDecl(Owned<QualifiedName> name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ImportDecl final : Decl {
    Owned<QualifiedName> path;
    std::string alias;

    ImportDecl(Owned<QualifiedName> path,
               std::string alias,
               std::string raw_text,
               ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ConstDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> value;

    ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct TypeAliasDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> aliased_type;

    TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct StructDecl final : Decl {
    std::string name;
    std::vector<Owned<StructFieldDeclSyntax>> fields;

    StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct EnumDecl final : Decl {
    std::string name;
    std::vector<Owned<EnumVariantDeclSyntax>> variants;

    EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct CapabilityDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<TypeSyntax> return_type;

    CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct PredicateDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;

    PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct AgentDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;
    Owned<TypeSyntax> context_type;
    Owned<TypeSyntax> output_type;
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<std::string> capabilities;
    Owned<AgentQuotaSyntax> quota;
    std::vector<Owned<TransitionSyntax>> transitions;

    AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct ContractDecl final : Decl {
    Owned<QualifiedName> target;
    std::vector<Owned<ContractClauseSyntax>> clauses;

    ContractDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct FlowDecl final : Decl {
    Owned<QualifiedName> target;
    std::vector<Owned<StateHandlerSyntax>> state_handlers;

    FlowDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

struct WorkflowDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;
    Owned<TypeSyntax> output_type;
    std::vector<Owned<WorkflowNodeDeclSyntax>> nodes;
    std::vector<Owned<TemporalExprSyntax>> safety;
    std::vector<Owned<TemporalExprSyntax>> liveness;
    Owned<ExprSyntax> return_value;

    WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

class Visitor {
  public:
    virtual ~Visitor() = default;

    virtual void visit(Program &node) = 0;
    virtual void visit(ModuleDecl &node) = 0;
    virtual void visit(ImportDecl &node) = 0;
    virtual void visit(ConstDecl &node) = 0;
    virtual void visit(TypeAliasDecl &node) = 0;
    virtual void visit(StructDecl &node) = 0;
    virtual void visit(EnumDecl &node) = 0;
    virtual void visit(CapabilityDecl &node) = 0;
    virtual void visit(PredicateDecl &node) = 0;
    virtual void visit(AgentDecl &node) = 0;
    virtual void visit(ContractDecl &node) = 0;
    virtual void visit(FlowDecl &node) = 0;
    virtual void visit(WorkflowDecl &node) = 0;
};

class RecursiveVisitor : public Visitor {
  public:
    void visit(Program &node) override;
    void visit(ModuleDecl &node) override;
    void visit(ImportDecl &node) override;
    void visit(ConstDecl &node) override;
    void visit(TypeAliasDecl &node) override;
    void visit(StructDecl &node) override;
    void visit(EnumDecl &node) override;
    void visit(CapabilityDecl &node) override;
    void visit(PredicateDecl &node) override;
    void visit(AgentDecl &node) override;
    void visit(ContractDecl &node) override;
    void visit(FlowDecl &node) override;
    void visit(WorkflowDecl &node) override;
};

} // namespace ahfl::ast
