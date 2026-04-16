#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/ast.hpp"
#include "ahfl/ownership.hpp"
#include "ahfl/resolver.hpp"
#include "ahfl/typecheck.hpp"

namespace ahfl {

namespace ir {

enum class PathRootKind {
    Identifier,
    Input,
    Output,
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

enum class ContractClauseKind {
    Requires,
    Ensures,
    Invariant,
    Forbid,
};

struct Path {
    PathRootKind root_kind{PathRootKind::Identifier};
    std::string root_name;
    std::vector<std::string> members;
};

struct Expr;
using ExprPtr = Owned<Expr>;

struct TemporalExpr;
using TemporalExprPtr = Owned<TemporalExpr>;

struct Statement;
using StatementPtr = Owned<Statement>;

struct NoneLiteralExpr {};

struct BoolLiteralExpr {
    bool value{false};
};

struct IntegerLiteralExpr {
    std::string spelling;
};

struct FloatLiteralExpr {
    std::string spelling;
};

struct DecimalLiteralExpr {
    std::string spelling;
};

struct StringLiteralExpr {
    std::string spelling;
};

struct DurationLiteralExpr {
    std::string spelling;
};

struct SomeExpr {
    ExprPtr value;
};

struct PathExpr {
    Path path;
};

struct QualifiedValueExpr {
    std::string value;
};

struct CallExpr {
    std::string callee;
    std::vector<ExprPtr> arguments;
};

struct StructFieldInit {
    std::string name;
    ExprPtr value;
};

struct StructLiteralExpr {
    std::string type_name;
    std::vector<StructFieldInit> fields;
};

struct ListLiteralExpr {
    std::vector<ExprPtr> items;
};

struct SetLiteralExpr {
    std::vector<ExprPtr> items;
};

struct MapEntryExpr {
    ExprPtr key;
    ExprPtr value;
};

struct MapLiteralExpr {
    std::vector<MapEntryExpr> entries;
};

struct UnaryExpr {
    ExprUnaryOp op{ExprUnaryOp::Not};
    ExprPtr operand;
};

struct BinaryExpr {
    ExprBinaryOp op{ExprBinaryOp::Implies};
    ExprPtr lhs;
    ExprPtr rhs;
};

struct MemberAccessExpr {
    ExprPtr base;
    std::string member;
};

struct IndexAccessExpr {
    ExprPtr base;
    ExprPtr index;
};

struct GroupExpr {
    ExprPtr expr;
};

using ExprNode = std::variant<NoneLiteralExpr,
                              BoolLiteralExpr,
                              IntegerLiteralExpr,
                              FloatLiteralExpr,
                              DecimalLiteralExpr,
                              StringLiteralExpr,
                              DurationLiteralExpr,
                              SomeExpr,
                              PathExpr,
                              QualifiedValueExpr,
                              CallExpr,
                              StructLiteralExpr,
                              ListLiteralExpr,
                              SetLiteralExpr,
                              MapLiteralExpr,
                              UnaryExpr,
                              BinaryExpr,
                              MemberAccessExpr,
                              IndexAccessExpr,
                              GroupExpr>;

struct Expr {
    ExprNode node;
};

struct EmbeddedTemporalExpr {
    ExprPtr expr;
};

struct CalledTemporalExpr {
    std::string capability;
};

struct InStateTemporalExpr {
    std::string state;
};

struct RunningTemporalExpr {
    std::string node;
};

struct CompletedTemporalExpr {
    std::string node;
    std::optional<std::string> state_name;
};

struct TemporalUnaryExpr {
    TemporalUnaryOp op{TemporalUnaryOp::Always};
    TemporalExprPtr operand;
};

struct TemporalBinaryExpr {
    TemporalBinaryOp op{TemporalBinaryOp::Implies};
    TemporalExprPtr lhs;
    TemporalExprPtr rhs;
};

using TemporalExprNode = std::variant<EmbeddedTemporalExpr,
                                      CalledTemporalExpr,
                                      InStateTemporalExpr,
                                      RunningTemporalExpr,
                                      CompletedTemporalExpr,
                                      TemporalUnaryExpr,
                                      TemporalBinaryExpr>;

struct TemporalExpr {
    TemporalExprNode node;
};

struct Block {
    std::vector<StatementPtr> statements;
};

struct LetStatement {
    std::string name;
    std::string type;
    ExprPtr initializer;
};

struct AssignStatement {
    Path target;
    ExprPtr value;
};

struct IfStatement {
    ExprPtr condition;
    Owned<Block> then_block;
    Owned<Block> else_block;
};

struct GotoStatement {
    std::string target_state;
};

struct ReturnStatement {
    ExprPtr value;
};

struct AssertStatement {
    ExprPtr condition;
};

struct ExprStatement {
    ExprPtr expr;
};

using StatementNode = std::variant<LetStatement,
                                   AssignStatement,
                                   IfStatement,
                                   GotoStatement,
                                   ReturnStatement,
                                   AssertStatement,
                                   ExprStatement>;

struct Statement {
    StatementNode node;
};

struct ModuleDecl {
    std::string name;
};

struct ImportDecl {
    std::string path;
    std::optional<std::string> alias;
};

struct ConstDecl {
    std::string name;
    std::string type;
    ExprPtr value;
};

struct TypeAliasDecl {
    std::string name;
    std::string aliased_type;
};

struct FieldDecl {
    std::string name;
    std::string type;
    ExprPtr default_value;
};

struct StructDecl {
    std::string name;
    std::vector<FieldDecl> fields;
};

struct EnumDecl {
    std::string name;
    std::vector<std::string> variants;
};

struct ParamDecl {
    std::string name;
    std::string type;
};

struct CapabilityDecl {
    std::string name;
    std::vector<ParamDecl> params;
    std::string return_type;
};

struct PredicateDecl {
    std::string name;
    std::vector<ParamDecl> params;
};

struct QuotaItem {
    std::string name;
    std::string value;
};

struct TransitionDecl {
    std::string from_state;
    std::string to_state;
};

struct AgentDecl {
    std::string name;
    std::string input_type;
    std::string context_type;
    std::string output_type;
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<std::string> capabilities;
    std::vector<QuotaItem> quota;
    std::vector<TransitionDecl> transitions;
};

struct ContractClause {
    ContractClauseKind kind{ContractClauseKind::Requires};
    std::variant<ExprPtr, TemporalExprPtr> value;
};

struct ContractDecl {
    std::string target;
    std::vector<ContractClause> clauses;
};

struct RetryPolicy {
    std::string limit;
};

struct RetryOnPolicy {
    std::vector<std::string> targets;
};

struct TimeoutPolicy {
    std::string duration;
};

using StatePolicyItem = std::variant<RetryPolicy, RetryOnPolicy, TimeoutPolicy>;

struct StateHandler {
    std::string state_name;
    std::vector<StatePolicyItem> policy;
    Block body;
};

struct FlowDecl {
    std::string target;
    std::vector<StateHandler> state_handlers;
};

struct WorkflowNode {
    std::string name;
    std::string target;
    ExprPtr input;
    std::vector<std::string> after;
};

struct WorkflowDecl {
    std::string name;
    std::string input_type;
    std::string output_type;
    std::vector<WorkflowNode> nodes;
    std::vector<TemporalExprPtr> safety;
    std::vector<TemporalExprPtr> liveness;
    ExprPtr return_value;
};

using Decl = std::variant<ModuleDecl,
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
                          WorkflowDecl>;

struct Program {
    std::string format_version{"ahfl.ir.v1"};
    std::vector<Decl> declarations;
};

} // namespace ir

[[nodiscard]] ir::Program lower_program_ir(const ast::Program &program,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

void print_program_ir(const ir::Program &program, std::ostream &out);

void print_program_ir_json(const ir::Program &program, std::ostream &out);

void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

void emit_program_ir_json(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

} // namespace ahfl
