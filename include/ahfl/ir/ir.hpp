#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/frontend/ast.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/support/ownership.hpp"

namespace ahfl {

struct SourceGraph;

namespace ir {

// ============================================================================
// AHFL 中间表示 (IR) 定义
// ============================================================================
//
// 本文件定义了 AHFL 编译器的中间表示。IR 由 Resolver → TypeChecker → IR Lowering
// 流水线产出，是后续 Emitter (SMV/Native/其他后端) 的输入。
//
// IR 与 AST 的关键区别：
//   1. 使用 std::variant 实现 tagged-union（而非 AST 的 tagged-struct）
//   2. 类型用字符串表示（已解析的全限定名）
//   3. 所有声明携带 DeclarationProvenance（来源追踪）
//   4. 包含推导出的分析信息（如 StateHandler::Summary、WorkflowExprSummary）
//
// IR 节点层次：
//   Program
//   ├── declarations: vector<Decl>
//   │   ├── ModuleDecl      — 模块声明
//   │   ├── ImportDecl      — 导入声明
//   │   ├── ConstDecl       — 常量声明
//   │   ├── TypeAliasDecl   — 类型别名
//   │   ├── StructDecl      — 结构体定义
//   │   ├── EnumDecl        — 枚举定义
//   │   ├── CapabilityDecl  — 能力声明
//   │   ├── PredicateDecl   — 谓词声明
//   │   ├── AgentDecl       — Agent 定义
//   │   ├── ContractDecl    — 契约声明
//   │   ├── FlowDecl        — 流程定义
//   │   └── WorkflowDecl    — 工作流定义
//   └── formal_observations: vector<FormalObservation>

inline constexpr std::string_view kFormatVersion = "ahfl.ir.v1";

// ----------------------------------------------------------------------------
// 枚举类型定义
// ----------------------------------------------------------------------------

/// 路径表达式的根节点类型
enum class PathRootKind {
    Identifier, // 普通标识符（变量名、node 输出引用等）
    Input,      // 关键字 `input`
    Output,     // 关键字 `output`
};

/// 表达式一元运算符
enum class ExprUnaryOp {
    Not,      // 逻辑非 !
    Negate,   // 取负 -
    Positive, // 取正 +
};

/// 表达式二元运算符
enum class ExprBinaryOp {
    Implies,      // => 蕴含
    Or,           // ||
    And,          // &&
    Equal,        // ==
    NotEqual,     // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=
    Add,          // +
    Subtract,     // -
    Multiply,     // *
    Divide,       // /
    Modulo,       // %
};

/// 时序逻辑一元运算符
enum class TemporalUnaryOp {
    Always,     // □ (always)
    Eventually, // ◇ (eventually)
    Next,       // ○ (next)
    Not,        // ¬ (not)
};

/// 时序逻辑二元运算符
enum class TemporalBinaryOp {
    Implies, // =>
    Or,      // ||
    And,     // &&
    Until,   // U (until)
};

/// 契约子句类型
enum class ContractClauseKind {
    Requires,  // 前置条件
    Ensures,   // 后置条件
    Invariant, // 不变量
    Forbid,    // 禁止条件
};

/// Workflow 中值引用的来源类型
enum class WorkflowValueSourceKind {
    WorkflowInput,      // 引用 workflow 的 input
    WorkflowNodeOutput, // 引用某个 node 的输出
};

// ----------------------------------------------------------------------------
// 路径 (Path)
// ----------------------------------------------------------------------------

/// 路径表达式（如 input.category, classify.confidence）
struct Path {
    PathRootKind root_kind{PathRootKind::Identifier};
    std::string root_name;            // 根名称（"input", "ctx", 或标识符名）
    std::vector<std::string> members; // 成员访问链
};

// ----------------------------------------------------------------------------
// 表达式层 (Expression Layer)
// ----------------------------------------------------------------------------

struct Expr;
using ExprPtr = Owned<Expr>;

struct TemporalExpr;
using TemporalExprPtr = Owned<TemporalExpr>;

struct Statement;
using StatementPtr = Owned<Statement>;

/// none 字面量
struct NoneLiteralExpr {};

/// 布尔字面量: true / false
struct BoolLiteralExpr {
    bool value{false};
};

/// 整数字面量: 42, -1
struct IntegerLiteralExpr {
    std::string spelling; // 原始文本（保留用于诊断）
};

/// 浮点字面量: 3.14
struct FloatLiteralExpr {
    std::string spelling;
};

/// 定点小数字面量: 3.14d
struct DecimalLiteralExpr {
    std::string spelling;
};

/// 字符串字面量: "hello"
struct StringLiteralExpr {
    std::string spelling;
};

/// 时间段字面量: 30s, 5m
struct DurationLiteralExpr {
    std::string spelling;
};

/// some 表达式: some(expr)
struct SomeExpr {
    ExprPtr value;
};

/// 路径表达式: input.field, ctx.field, node_name.field
struct PathExpr {
    Path path;
};

/// 全限定值表达式: Priority::High
struct QualifiedValueExpr {
    std::string value; // 全限定名
};

/// 函数调用表达式: capability_name(arg1, arg2, ...)
struct CallExpr {
    std::string callee;             // 被调函数名（capability 名称）
    std::vector<ExprPtr> arguments; // 实参列表
};

/// 结构体字段初始化
struct StructFieldInit {
    std::string name; // 字段名
    ExprPtr value;    // 初始化表达式
};

/// 结构体字面量: TypeName { field1: val1, field2: val2 }
struct StructLiteralExpr {
    std::string type_name;               // 类型名
    std::vector<StructFieldInit> fields; // 字段初始化列表
};

/// 列表字面量: [a, b, c]
struct ListLiteralExpr {
    std::vector<ExprPtr> items;
};

/// 集合字面量: {a, b, c}
struct SetLiteralExpr {
    std::vector<ExprPtr> items;
};

/// Map 键值对
struct MapEntryExpr {
    ExprPtr key;
    ExprPtr value;
};

/// Map 字面量: {k1: v1, k2: v2}
struct MapLiteralExpr {
    std::vector<MapEntryExpr> entries;
};

/// 一元表达式: !expr, -expr
struct UnaryExpr {
    ExprUnaryOp op{ExprUnaryOp::Not};
    ExprPtr operand;
};

/// 二元表达式: a + b, a == b
struct BinaryExpr {
    ExprBinaryOp op{ExprBinaryOp::Implies};
    ExprPtr lhs;
    ExprPtr rhs;
};

/// 成员访问表达式: expr.member
struct MemberAccessExpr {
    ExprPtr base;       // 基对象
    std::string member; // 成员名
};

/// 索引访问表达式: expr[index]
struct IndexAccessExpr {
    ExprPtr base;  // 基对象
    ExprPtr index; // 索引表达式
};

/// 括号分组表达式: (expr)
struct GroupExpr {
    ExprPtr expr;
};

/// 表达式节点（20 种 variant）
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

/// 表达式包装结构
struct Expr {
    ExprNode node;
};

// ----------------------------------------------------------------------------
// 时序逻辑表达式层 (Temporal Expression Layer)
// ----------------------------------------------------------------------------

/// 嵌入普通表达式
struct EmbeddedTemporalExpr {
    ExprPtr expr;
};

/// called(capability_name) — 某个 capability 被调用过
struct CalledTemporalExpr {
    std::string capability;
};

/// in_state(agent, state) — 某个 agent 处于某个状态
struct InStateTemporalExpr {
    std::string state;
};

/// running(agent) — 某个 agent 正在运行
struct RunningTemporalExpr {
    std::string node;
};

/// completed(agent) — 某个 agent 已完成
struct CompletedTemporalExpr {
    std::string node;
    std::optional<std::string> state_name; // 可选的终止状态
};

/// 时序一元表达式: always(expr), eventually(expr)
struct TemporalUnaryExpr {
    TemporalUnaryOp op{TemporalUnaryOp::Always};
    TemporalExprPtr operand;
};

/// 时序二元表达式: a U b (a until b)
struct TemporalBinaryExpr {
    TemporalBinaryOp op{TemporalBinaryOp::Implies};
    TemporalExprPtr lhs;
    TemporalExprPtr rhs;
};

/// 时序表达式节点（7 种 variant）
using TemporalExprNode = std::variant<EmbeddedTemporalExpr,
                                      CalledTemporalExpr,
                                      InStateTemporalExpr,
                                      RunningTemporalExpr,
                                      CompletedTemporalExpr,
                                      TemporalUnaryExpr,
                                      TemporalBinaryExpr>;

/// 时序表达式包装结构
struct TemporalExpr {
    TemporalExprNode node;
};

// ----------------------------------------------------------------------------
// 语句层 (Statement Layer)
// ----------------------------------------------------------------------------

/// 语句块: { stmt1; stmt2; ... }
struct Block {
    std::vector<StatementPtr> statements;
};

/// let 绑定语句: let name: Type = initializer;
struct LetStatement {
    std::string name;    // 变量名
    std::string type;    // 类型（全限定名）
    ExprPtr initializer; // 初始化表达式
};

/// 赋值语句: target = value;
struct AssignStatement {
    Path target;   // 赋值目标路径
    ExprPtr value; // 赋值表达式
};

/// 条件语句: if (condition) { then } else { else }
struct IfStatement {
    ExprPtr condition;       // 条件表达式
    Owned<Block> then_block; // then 分支
    Owned<Block> else_block; // else 分支（可选）
};

/// 状态跳转语句: goto StateName;
struct GotoStatement {
    std::string target_state; // 目标状态名
};

/// 返回语句: return expr;
struct ReturnStatement {
    ExprPtr value; // 返回值表达式
};

/// 断言语句: assert(condition);
struct AssertStatement {
    ExprPtr condition; // 断言条件
};

/// 表达式语句 (如 capability 调用): expr;
struct ExprStatement {
    ExprPtr expr;
};

/// 语句节点（7 种 variant）
using StatementNode = std::variant<LetStatement,
                                   AssignStatement,
                                   IfStatement,
                                   GotoStatement,
                                   ReturnStatement,
                                   AssertStatement,
                                   ExprStatement>;

/// 语句包装结构
struct Statement {
    StatementNode node;
};

// ----------------------------------------------------------------------------
// 声明来源追踪 (Declaration Provenance)
// ----------------------------------------------------------------------------

/// 声明来源信息（用于诊断和调试）
struct DeclarationProvenance {
    std::string module_name; // 所属模块名
    std::string source_path; // 源文件路径
};

// ----------------------------------------------------------------------------
// 顶层声明 (Top-Level Declarations)
// ----------------------------------------------------------------------------

/// 模块声明: module a::b::c;
struct ModuleDecl {
    DeclarationProvenance provenance;
    std::string name;
};

/// 导入声明: import a::b::c [as alias];
struct ImportDecl {
    DeclarationProvenance provenance;
    std::string path;
    std::optional<std::string> alias;
};

/// 常量声明: const NAME: Type = value;
struct ConstDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::string type; // 类型（全限定名）
    ExprPtr value;
};

/// 类型别名: type NewName = ExistingType;
struct TypeAliasDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::string aliased_type;
};

/// 结构体字段声明
struct FieldDecl {
    std::string name;
    std::string type;      // 类型（全限定名）
    ExprPtr default_value; // 可选的默认值
};

/// 结构体声明: struct Name { field1: Type1; ... }
struct StructDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<FieldDecl> fields;
};

/// 枚举声明: enum Name { Variant1; Variant2; }
struct EnumDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<std::string> variants;
};

/// 参数声明
struct ParamDecl {
    std::string name;
    std::string type;
};

/// 能力声明: capability Name(param1: Type1, ...) -> ReturnType;
struct CapabilityDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    std::string return_type;
};

/// 谓词声明: predicate Name(param1: Type1, ...);
struct PredicateDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
};

/// 配额项
struct QuotaItem {
    std::string name;  // 如 "max_tool_calls"
    std::string value; // 如 "10"
};

/// 状态转换声明
struct TransitionDecl {
    std::string from_state;
    std::string to_state;
};

/// Agent 声明 — AHFL 的核心实体
struct AgentDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::string input_type;                  // 输入类型（全限定名）
    std::string context_type;                // 上下文类型（全限定名）
    std::string output_type;                 // 输出类型（全限定名）
    std::vector<std::string> states;         // 状态集合
    std::string initial_state;               // 初始状态
    std::vector<std::string> final_states;   // 终止状态集合
    std::vector<std::string> capabilities;   // 可用 capability 列表
    std::vector<QuotaItem> quota;            // 资源配额
    std::vector<TransitionDecl> transitions; // 合法状态转换
};

/// 契约子句
struct ContractClause {
    ContractClauseKind kind{ContractClauseKind::Requires};
    std::variant<ExprPtr, TemporalExprPtr> value; // 普通表达式或时序逻辑表达式
};

/// 契约声明: contract for AgentName { requires ...; ensures ...; }
struct ContractDecl {
    DeclarationProvenance provenance;
    std::string target;                  // 目标 Agent
    std::vector<ContractClause> clauses; // 契约子句列表
};

// ----------------------------------------------------------------------------
// Flow 相关结构 (Flow-Related Structures)
// ----------------------------------------------------------------------------

/// 重试策略: retry: 3;
struct RetryPolicy {
    std::string limit;
};

/// 指定错误类型重试策略: retry_on: [Error1, Error2];
struct RetryOnPolicy {
    std::vector<std::string> targets;
};

/// 超时策略: timeout: 30s;
struct TimeoutPolicy {
    std::string duration;
};

/// 状态策略项（variant）
using StatePolicyItem = std::variant<RetryPolicy, RetryOnPolicy, TimeoutPolicy>;

/// 状态处理器: state StateName [policy {...}] { statements... }
struct StateHandler {
    /// 状态处理器摘要（由 IR lowering 阶段计算）
    struct Summary {
        std::vector<std::string> goto_targets;   // 可能跳转到的目标状态
        bool may_return{false};                  // 是否可能执行 return
        bool may_fallthrough{true};              // 是否可能顺序执行到底
        std::vector<Path> assigned_paths;        // 被赋值的路径
        std::vector<std::string> called_targets; // 调用的 capability
        std::size_t assert_count{0};             // assert 语句数量
    };

    std::string state_name;              // 处理哪个状态
    std::vector<StatePolicyItem> policy; // 执行策略
    Block body;                          // 处理逻辑（语句块）
    Summary summary;                     // 推导出的摘要信息
};

/// 流程声明: flow for AgentName { state Init { ... } ... }
struct FlowDecl {
    DeclarationProvenance provenance;
    std::string target;                       // 目标 Agent
    std::vector<StateHandler> state_handlers; // 各状态的处理器
};

// ----------------------------------------------------------------------------
// Workflow 相关结构 (Workflow-Related Structures)
// ----------------------------------------------------------------------------

/// Workflow 中值引用的来源
struct WorkflowValueRead {
    WorkflowValueSourceKind kind{WorkflowValueSourceKind::WorkflowInput};
    std::string root_name;            // input 或 node 名称
    std::vector<std::string> members; // 成员访问链
};

/// Workflow 表达式摘要（分析其依赖的值来源）
struct WorkflowExprSummary {
    std::vector<WorkflowValueRead> reads; // 所有值引用
};

/// Workflow 节点: node name: AgentType(input_expr) [after [dep1, dep2]];
struct WorkflowNode {
    std::string name;                  // 节点名称
    std::string target;                // 绑定的 Agent 类型（全限定名）
    ExprPtr input;                     // 传递给 agent 的输入表达式
    WorkflowExprSummary input_summary; // 输入表达式的摘要
    std::vector<std::string> after;    // DAG 依赖（前置节点名列表）
};

/// 工作流声明 — 多 Agent DAG 编排
struct WorkflowDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::string input_type;                // Workflow 输入类型
    std::string output_type;               // Workflow 输出类型
    std::vector<WorkflowNode> nodes;       // DAG 节点列表
    std::vector<TemporalExprPtr> safety;   // 安全性属性
    std::vector<TemporalExprPtr> liveness; // 活性属性
    ExprPtr return_value;                  // 最终返回值表达式
    WorkflowExprSummary return_summary;    // 返回值表达式的摘要
};

// ----------------------------------------------------------------------------
// 形式化观察 (Formal Observations)
// ----------------------------------------------------------------------------

/// 形式化观察的范围类型
enum class FormalObservationScopeKind {
    ContractClause,         // 契约子句
    WorkflowSafetyClause,   // Workflow safety 属性
    WorkflowLivenessClause, // Workflow liveness 属性
};

/// 形式化观察的范围
struct FormalObservationScope {
    FormalObservationScopeKind kind{FormalObservationScopeKind::ContractClause};
    std::string owner;           // 所属 Agent 或 Workflow
    std::size_t clause_index{0}; // 子句索引
    std::size_t atom_index{0};   // 原子索引
};

/// called(capability) 观察
struct CalledCapabilityObservation {
    std::string agent;
    std::string capability;
};

/// 嵌入布尔表达式观察
struct EmbeddedBoolObservation {
    FormalObservationScope scope;
};

/// 形式化观察节点
using FormalObservationNode = std::variant<CalledCapabilityObservation, EmbeddedBoolObservation>;

/// 形式化观察（用于 SMV 模型生成）
struct FormalObservation {
    std::string symbol;         // 观察符号
    FormalObservationNode node; // 观察内容
};

// ----------------------------------------------------------------------------
// 顶层 IR 结构 (Top-Level IR Structures)
// ----------------------------------------------------------------------------

/// 声明（variant，包含所有顶层声明类型）
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

/// IR 程序 — 编译单元的完整 IR 表示
struct Program {
    std::string format_version{std::string(kFormatVersion)};
    std::vector<Decl> declarations;                     // 所有顶层声明
    std::vector<FormalObservation> formal_observations; // 形式化观察列表
};

} // namespace ir

// ----------------------------------------------------------------------------
// IR 生成 API
// ----------------------------------------------------------------------------

/// 从 AST 生成 IR（单文件）
[[nodiscard]] ir::Program lower_program_ir(const ast::Program &program,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// 从 SourceGraph 生成 IR（多文件）
[[nodiscard]] ir::Program lower_program_ir(const SourceGraph &graph,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// 收集形式化观察
[[nodiscard]] std::vector<ir::FormalObservation>
collect_formal_observations(const ir::Program &program);

/// 打印 IR（可读格式）
void print_program_ir(const ir::Program &program, std::ostream &out);

/// 打印 IR（JSON 格式）
void print_program_ir_json(const ir::Program &program, std::ostream &out);

/// 发射 IR 到输出流（可读格式）
void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

void emit_program_ir(const SourceGraph &graph,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

/// 发射 IR 到输出流（JSON 格式）
void emit_program_ir_json(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

void emit_program_ir_json(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

} // namespace ahfl
