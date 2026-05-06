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

// ============================================================================
// AHFL 抽象语法树 (AST) 定义
// ============================================================================
//
// 本文件定义了 AHFL 编译器前端的 AST 节点结构。AST 是 parse-tree lowering 之后的
// 稳定语法边界，后续由 Resolver → TypeChecker → IR Lowering 逐步消费。
//
// 设计约束：
//   1. AST 节点不保留 ANTLR parse-tree context 或 token 引用
//   2. AST 节点不携带语义/类型信息（这些在后续 pass 中独立构建）
//   3. 所有子节点使用 Owned<T> (unique_ptr) 管理生命周期
//
// 节点层次：
//   Node (基类)
//   ├── Program         — 顶层编译单元
//   └── Decl (抽象)     — 所有声明的基类
//       ├── ModuleDecl      — 模块声明
//       ├── ImportDecl      — 导入声明
//       ├── ConstDecl       — 常量声明
//       ├── TypeAliasDecl   — 类型别名
//       ├── StructDecl      — 结构体定义
//       ├── EnumDecl        — 枚举定义
//       ├── CapabilityDecl  — 能力声明（外部调用接口）
//       ├── PredicateDecl   — 谓词声明（形式化验证用）
//       ├── AgentDecl       — Agent 定义（状态机）
//       ├── ContractDecl    — 契约声明（前置/后置条件）
//       ├── FlowDecl        — 流程定义（Agent 的状态处理逻辑）
//       └── WorkflowDecl    — 工作流定义（多 Agent DAG 编排）

class Visitor;

// ----------------------------------------------------------------------------
// 枚举类型定义
// ----------------------------------------------------------------------------

/// AST 节点种类，用于快速类型判断
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

/// 契约子句类型
/// - Requires: 前置条件（调用前必须满足）
/// - Ensures: 后置条件（执行后保证成立）
/// - Invariant: 不变量（始终成立）
/// - Forbid: 禁止条件（永远不得发生）
enum class ContractClauseKind {
    Requires,
    Ensures,
    Invariant,
    Forbid,
};

[[nodiscard]] std::string_view to_string(ContractClauseKind kind) noexcept;

/// AHFL 类型系统中的基础类型种类
enum class TypeSyntaxKind {
    Unit,          // 空类型 ()
    Bool,          // 布尔
    Int,           // 64位整数
    Float,         // 64位浮点
    String,        // 无界字符串
    BoundedString, // 有长度约束的字符串 String(min..max)
    UUID,          // UUID 标识符
    Timestamp,     // 时间戳
    Duration,      // 时间段
    Decimal,       // 定点小数 Decimal(scale)
    Named,         // 命名类型（引用 struct/enum）
    Optional,      // 可选类型 T?
    List,          // 列表 List<T>
    Set,           // 集合 Set<T>
    Map,           // 映射 Map<K, V>
};

[[nodiscard]] std::string_view to_string(TypeSyntaxKind kind) noexcept;

/// Agent 配额项类型
enum class AgentQuotaItemKind {
    MaxToolCalls,     // 最大工具调用次数
    MaxExecutionTime, // 最大执行时间
};

/// 状态策略项类型（flow 中 state handler 的执行策略）
enum class StatePolicyItemKind {
    Retry,   // 重试次数
    RetryOn, // 指定错误类型时重试
    Timeout, // 超时限制
};

/// 路径表达式的根节点类型
/// - Identifier: 普通标识符（变量名、node 输出引用等）
/// - Input: 关键字 `input`（agent/workflow 的输入）
/// - Output: 关键字 `output`（agent 的输出上下文）
enum class PathRootKind {
    Identifier,
    Input,
    Output,
};

/// 表达式语法种类（20种）
enum class ExprSyntaxKind {
    BoolLiteral,     // true / false
    IntegerLiteral,  // 42, -1
    FloatLiteral,    // 3.14
    DecimalLiteral,  // 3.14d
    StringLiteral,   // "hello"
    DurationLiteral, // 30s, 5m
    NoneLiteral,     // none
    Some,            // some(expr)
    Path,            // input.field, ctx.field, node_name.field
    QualifiedValue,  // Priority::High（全限定枚举值）
    Call,            // capability_name(args...)
    StructLiteral,   // TypeName { field: value, ... }
    ListLiteral,     // [a, b, c]
    SetLiteral,      // {a, b, c}
    MapLiteral,      // {k1: v1, k2: v2}
    Unary,           // !expr, -expr
    Binary,          // a + b, a == b, a && b
    MemberAccess,    // expr.member
    IndexAccess,     // expr[index]
    Group,           // (expr) 括号分组
};

/// 一元运算符
enum class ExprUnaryOp {
    Not,      // 逻辑非 !
    Negate,   // 取负 -
    Positive, // 取正 +
};

/// 二元运算符（按优先级从低到高排列）
enum class ExprBinaryOp {
    Implies,      // =>  蕴含（用于契约/时序逻辑）
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

/// 语句种类
enum class StatementSyntaxKind {
    Let,    // let x: Type = expr;
    Assign, // ctx.field = expr;
    If,     // if (cond) { ... } else { ... }
    Goto,   // goto StateName;
    Return, // return expr;
    Assert, // assert(cond);
    Expr,   // expr; （表达式语句，通常是 capability 调用）
};

/// 时序逻辑表达式种类（用于形式化验证 / SMV 生成）
enum class TemporalExprSyntaxKind {
    EmbeddedExpr, // 嵌入普通表达式
    Called,       // called(capability_name)
    InState,      // in_state(agent, state)
    Running,      // running(agent)
    Completed,    // completed(agent)
    Unary,        // always/eventually/next/not 前缀
    Binary,       // implies/or/and/until 中缀
};

/// 时序一元运算符
enum class TemporalUnaryOp {
    Always,     // □ (always) — 所有路径上始终成立
    Eventually, // ◇ (eventually) — 最终会成立
    Next,       // ○ (next) — 下一步成立
    Not,        // ¬ (not) — 否定
};

/// 时序二元运算符
enum class TemporalBinaryOp {
    Implies, // =>
    Or,      // ||
    And,     // &&
    Until,   // U (until) — 直到某条件成立
};

// ----------------------------------------------------------------------------
// 语法片段结构 (Syntax Fragments)
// ----------------------------------------------------------------------------

/// 限定名（如 module::path::Name）
struct QualifiedName {
    ahfl::SourceRange range;
    std::vector<std::string> segments; // ["module", "path", "Name"]

    [[nodiscard]] std::string spelling() const;
};

/// 类型语法节点
/// kind 决定使用哪些字段：
///   - Named: name 字段有值
///   - BoundedString: string_bounds 有值
///   - Decimal: decimal_scale 有值
///   - Optional/List/Set: first 指向内部类型
///   - Map: first=key类型, second=value类型
struct TypeSyntax {
    ahfl::SourceRange range;
    TypeSyntaxKind kind{TypeSyntaxKind::Named};
    Owned<QualifiedName> name;                                          // Named 类型的限定名
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds; // BoundedString 的长度区间
    std::optional<std::int64_t> decimal_scale;                          // Decimal 精度
    Owned<TypeSyntax> first;  // 泛型第一参数 / Optional内部类型
    Owned<TypeSyntax> second; // Map 的 value 类型

    [[nodiscard]] std::string spelling() const;
};

// 前向声明
struct ExprSyntax;
struct StatementSyntax;
struct IntegerSyntax;
struct DurationSyntax;

/// 路径表达式语法 (如 input.category, ctx.resolved, classify.confidence)
struct PathSyntax {
    ahfl::SourceRange range;
    PathRootKind root_kind{PathRootKind::Identifier}; // 根节点类型
    std::string root_name;            // 根名称（"input", "ctx", 或标识符名）
    std::vector<std::string> members; // 成员访问链

    [[nodiscard]] std::string spelling() const;
};

/// Map 字面量中的一个键值对
struct MapEntrySyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> key;
    Owned<ExprSyntax> value;
};

/// 结构体字面量中的一个字段初始化
struct StructInitSyntax {
    ahfl::SourceRange range;
    std::string field_name;  // 字段名
    Owned<ExprSyntax> value; // 初始化表达式
};

/// 表达式语法节点（tagged-union 风格，kind 决定有效字段）
///
/// 各 kind 使用的字段：
///   - BoolLiteral: bool_value
///   - IntegerLiteral: integer_literal
///   - FloatLiteral/DecimalLiteral/StringLiteral: text
///   - DurationLiteral: duration_literal
///   - Path: path
///   - QualifiedValue: qualified_name
///   - Call: name (函数名) + items (参数列表)
///   - StructLiteral: name (类型名) + struct_fields
///   - ListLiteral/SetLiteral: items
///   - MapLiteral: map_entries
///   - Unary: unary_op + first
///   - Binary: binary_op + first + second
///   - MemberAccess: first (对象) + name (成员名)
///   - IndexAccess: first (对象) + second (索引)
///   - Group: first (内部表达式)
struct ExprSyntax {
    ahfl::SourceRange range;
    std::string text; // 字面量原文
    ExprSyntaxKind kind{ExprSyntaxKind::NoneLiteral};
    bool bool_value{false};                             // BoolLiteral
    ExprUnaryOp unary_op{ExprUnaryOp::Not};             // Unary
    ExprBinaryOp binary_op{ExprBinaryOp::Implies};      // Binary
    Owned<IntegerSyntax> integer_literal;               // IntegerLiteral
    Owned<DurationSyntax> duration_literal;             // DurationLiteral
    std::string name;                                   // Call/MemberAccess/StructLiteral
    Owned<QualifiedName> qualified_name;                // QualifiedValue
    Owned<PathSyntax> path;                             // Path
    std::vector<Owned<ExprSyntax>> items;               // 列表/集合/调用参数
    std::vector<Owned<MapEntrySyntax>> map_entries;     // MapLiteral
    std::vector<Owned<StructInitSyntax>> struct_fields; // StructLiteral
    Owned<ExprSyntax> first;                            // 一元/二元操作数
    Owned<ExprSyntax> second;                           // 二元右操作数
};

// ----------------------------------------------------------------------------
// 语句语法结构
// ----------------------------------------------------------------------------

/// let 绑定语句: let name: Type = initializer;
struct LetStmtSyntax {
    ahfl::SourceRange range;
    std::string name;              // 变量名
    Owned<TypeSyntax> type;        // 类型注解（可选）
    Owned<ExprSyntax> initializer; // 初始化表达式
};

/// 赋值语句: target = value;（仅允许赋值到 ctx.field）
struct AssignStmtSyntax {
    ahfl::SourceRange range;
    Owned<PathSyntax> target; // 赋值目标路径
    Owned<ExprSyntax> value;  // 赋值表达式
};

/// 语句块: { stmt1; stmt2; ... }
struct BlockSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatementSyntax>> statements;
};

/// 条件语句: if (condition) { then } else { else }
struct IfStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition;   // 条件表达式
    Owned<BlockSyntax> then_block; // then 分支
    Owned<BlockSyntax> else_block; // else 分支（可选）
};

/// 状态跳转语句: goto StateName;
struct GotoStmtSyntax {
    ahfl::SourceRange range;
    std::string target_state; // 目标状态名
};

/// 返回语句: return expr;
struct ReturnStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> value; // 返回值表达式
};

/// 断言语句: assert(condition);
struct AssertStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition; // 断言条件
};

/// 表达式语句 (如 capability 调用): expr;
struct ExprStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> expr;
};

/// 语句节点（tagged-union，kind 决定哪个 stmt 字段有效）
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

/// 时序逻辑表达式（用于 safety/liveness 属性验证）
/// 编译到 SMV 模型检测器可消费的 CTL/LTL 公式
struct TemporalExprSyntax {
    ahfl::SourceRange range;
    std::string text;
    TemporalExprSyntaxKind kind{TemporalExprSyntaxKind::EmbeddedExpr};
    TemporalUnaryOp unary_op{TemporalUnaryOp::Always};
    TemporalBinaryOp binary_op{TemporalBinaryOp::Implies};
    std::string name;                      // Called/InState/Running/Completed 的目标名
    std::optional<std::string> state_name; // InState 的状态名
    Owned<ExprSyntax> expr;                // EmbeddedExpr 的嵌入表达式
    Owned<TemporalExprSyntax> first;       // 一元/二元第一操作数
    Owned<TemporalExprSyntax> second;      // 二元第二操作数
};

// ----------------------------------------------------------------------------
// 辅助语法片段
// ----------------------------------------------------------------------------

/// 整数字面量
struct IntegerSyntax {
    ahfl::SourceRange range;
    std::string spelling; // 原始文本
    std::int64_t value{0};
};

/// 时间段字面量 (如 "30s", "5m", "1h")
struct DurationSyntax {
    ahfl::SourceRange range;
    std::string spelling;
};

/// 参数声明 (用于 capability 参数)
struct ParamDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
};

/// 结构体字段声明
struct StructFieldDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> default_value; // 可选的默认值
};

/// 枚举变体声明
struct EnumVariantDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
};

/// 状态转换声明: from_state -> to_state
struct TransitionSyntax {
    ahfl::SourceRange range;
    std::string from_state;
    std::string to_state;
};

/// Agent 配额项（资源限制）
struct AgentQuotaItemSyntax {
    ahfl::SourceRange range;
    AgentQuotaItemKind kind{AgentQuotaItemKind::MaxToolCalls};
    Owned<IntegerSyntax> integer_value;   // MaxToolCalls 的值
    Owned<DurationSyntax> duration_value; // MaxExecutionTime 的值
};

/// Agent 配额声明: quota { max_tool_calls: 10; max_execution_time: 30s; }
struct AgentQuotaSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<AgentQuotaItemSyntax>> items;
};

/// 契约子句: requires/ensures/invariant/forbid expr
struct ContractClauseSyntax {
    ahfl::SourceRange range;
    ContractClauseKind kind{ContractClauseKind::Requires};
    Owned<ExprSyntax> expr;                  // 普通表达式条件
    Owned<TemporalExprSyntax> temporal_expr; // 时序逻辑条件
};

/// 状态策略项（flow handler 中的执行策略）
struct StatePolicyItemSyntax {
    ahfl::SourceRange range;
    StatePolicyItemKind kind{StatePolicyItemKind::Retry};
    Owned<IntegerSyntax> retry_limit;           // Retry 的次数
    std::vector<Owned<QualifiedName>> retry_on; // RetryOn 的错误类型列表
    Owned<DurationSyntax> timeout;              // Timeout 的时间限制
};

/// 状态策略声明: policy { retry: 3; timeout: 30s; }
struct StatePolicySyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatePolicyItemSyntax>> items;
};

/// 状态处理器: state StateName [policy {...}] { statements... }
struct StateHandlerSyntax {
    ahfl::SourceRange range;
    std::string state_name;          // 处理哪个状态
    Owned<StatePolicySyntax> policy; // 可选的执行策略
    Owned<BlockSyntax> body;         // 处理逻辑（语句块）
};

/// Workflow 节点声明: node name: AgentType(input_expr) [after [dep1, dep2]];
struct WorkflowNodeDeclSyntax {
    ahfl::SourceRange range;
    std::string name;               // 节点名称
    Owned<QualifiedName> target;    // 绑定的 Agent 类型
    Owned<ExprSyntax> input;        // 传递给 agent 的输入表达式
    std::vector<std::string> after; // DAG 依赖（前置节点名列表）
};

// ----------------------------------------------------------------------------
// 核心 AST 节点类层次
// ----------------------------------------------------------------------------

/// AST 节点基类（所有节点的公共接口）
struct Node {
    NodeKind kind;
    ahfl::SourceRange range;

    explicit Node(NodeKind kind, ahfl::SourceRange range = {});
    virtual ~Node() = default;

    virtual void accept(Visitor &visitor) = 0;
};

/// 声明基类（所有顶层声明的公共基类）
struct Decl : Node {
    // 过渡性调试载荷。Resolver/Checker 不得依赖 reparsing raw_text；
    // 语义 pass 必须消费结构化 AST 数据。
    std::string raw_text;

    Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range = {});
    ~Decl() override = default;

    /// 返回声明的单行摘要（用于诊断输出）
    [[nodiscard]] virtual std::string headline() const = 0;
};

/// 编译单元（一个 .ahfl 文件对应一个 Program）
struct Program final : Node {
    std::string source_name;               // 源文件名
    std::vector<Owned<Decl>> declarations; // 所有顶层声明

    explicit Program(std::string source_name, ahfl::SourceRange range = {});

    void accept(Visitor &visitor) override;
};

/// 模块声明: module a::b::c;
struct ModuleDecl final : Decl {
    Owned<QualifiedName> name;

    ModuleDecl(Owned<QualifiedName> name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 导入声明: import a::b::c [as alias];
struct ImportDecl final : Decl {
    Owned<QualifiedName> path; // 导入路径
    std::string alias;         // 可选别名

    ImportDecl(Owned<QualifiedName> path,
               std::string alias,
               std::string raw_text,
               ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 常量声明: const NAME: Type = value;
struct ConstDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> value;

    ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 类型别名: type NewName = ExistingType;
struct TypeAliasDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> aliased_type;

    TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 结构体声明: struct Name { field1: Type1; field2: Type2 = default; }
struct StructDecl final : Decl {
    std::string name;
    std::vector<Owned<StructFieldDeclSyntax>> fields;

    StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 枚举声明: enum Name { Variant1; Variant2; Variant3; }
struct EnumDecl final : Decl {
    std::string name;
    std::vector<Owned<EnumVariantDeclSyntax>> variants;

    EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 能力声明: capability Name(param1: Type1, ...) -> ReturnType;
/// 代表 Agent 可调用的外部接口（如 LLM API、数据库查询等）
struct CapabilityDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<TypeSyntax> return_type;

    CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 谓词声明: predicate Name(param1: Type1, ...);
/// 用于形式化验证中的逻辑断言
struct PredicateDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;

    PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Agent 声明 — AHFL 的核心实体
///
/// 定义一个有限状态机 Agent：
///   agent Name {
///       input: InputType;
///       context: CtxType;
///       output: OutputType;
///       states: [Init, Processing, Done];
///       initial: Init;
///       final: [Done];
///       capabilities: [Cap1, Cap2];
///       transitions { Init -> Processing; Processing -> Done; }
///       quota { max_tool_calls: 10; }
///   }
struct AgentDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;                     // 输入类型
    Owned<TypeSyntax> context_type;                   // 上下文类型（可变状态）
    Owned<TypeSyntax> output_type;                    // 输出类型
    std::vector<std::string> states;                  // 状态集合
    std::string initial_state;                        // 初始状态
    std::vector<std::string> final_states;            // 终止状态集合
    std::vector<std::string> capabilities;            // 可用 capability 列表
    Owned<AgentQuotaSyntax> quota;                    // 资源配额
    std::vector<Owned<TransitionSyntax>> transitions; // 合法状态转换

    AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 契约声明: contract for AgentName { requires ...; ensures ...; }
/// 为 Agent 附加形式化约束，编译到 SMV 模型进行验证
struct ContractDecl final : Decl {
    Owned<QualifiedName> target;                      // 目标 Agent
    std::vector<Owned<ContractClauseSyntax>> clauses; // 契约子句列表

    ContractDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 流程声明: flow for AgentName { state Init { ... } state Done { ... } }
/// 定义 Agent 每个状态下的具体执行逻辑
struct FlowDecl final : Decl {
    Owned<QualifiedName> target;                           // 目标 Agent
    std::vector<Owned<StateHandlerSyntax>> state_handlers; // 各状态的处理器

    FlowDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// 工作流声明 — 多 Agent DAG 编排
///
///   workflow Name {
///       input: InputType;
///       output: OutputType;
///       node a: AgentA(input);
///       node b: AgentB(a.result) after [a];
///       safety { always(...); }
///       liveness { eventually(...); }
///       return OutputType { field: b.result };
///   }
struct WorkflowDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;                     // Workflow 输入类型
    Owned<TypeSyntax> output_type;                    // Workflow 输出类型
    std::vector<Owned<WorkflowNodeDeclSyntax>> nodes; // DAG 节点列表
    std::vector<Owned<TemporalExprSyntax>> safety;    // 安全性属性（□ always）
    std::vector<Owned<TemporalExprSyntax>> liveness;  // 活性属性（◇ eventually）
    Owned<ExprSyntax> return_value;                   // 最终返回值表达式

    WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

// ----------------------------------------------------------------------------
// Visitor 模式
// ----------------------------------------------------------------------------

/// AST 访问者接口（双分派）
/// 所有需要遍历 AST 的 pass（Resolver、TypeChecker、Emitter）实现此接口
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

/// 递归访问者（提供默认空实现，子类只需覆盖感兴趣的节点）
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
