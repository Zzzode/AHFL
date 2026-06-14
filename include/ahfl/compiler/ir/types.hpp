#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl::ir {

// ============================================================================
// AHFL 中间表示 (IR) 定义
// ============================================================================
//
// 本文件定义了 AHFL 编译器的中间表示。IR 由 Resolver → TypeChecker → IR Lowering
// 流水线产出，是后续 Emitter (SMV/Native/其他后端) 的输入。
//
// IR 与 AST 的关键区别：
//   1. 使用 std::variant 实现 tagged-union（而非 AST 的 tagged-struct）
//   2. 类型/跨声明符号以 TypeRef/SymbolRef 作为唯一结构化事实源
//   3. 声明和主要可执行节点携带结构化 source range（来源追踪）
//   4. 推导出的分析信息集中保存在 Program::analyses
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
//   └── analyses: AnalysisBundle

inline constexpr std::string_view kFormatVersion = "ahfl.ir.v1";

// ----------------------------------------------------------------------------
// 枚举类型定义
// ----------------------------------------------------------------------------

/// 路径表达式的根节点类型
enum class PathRootKind {
    Identifier, // 普通标识符（变量名、node 输出引用等）
    Input,      // 关键字 `input`
    Context,    // 关键字/约定根 `ctx`
    Output,     // 关键字 `output`
    State,      // 约定根 `state`
    Local,      // Flow-local let binding
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

/// 符号引用种类
enum class SymbolRefKind {
    Unknown,
    Type,
    Const,
    Capability,
    Predicate,
    Agent,
    Workflow,
};

/// 类型引用种类
enum class TypeRefKind {
    Unresolved,
    Any,
    Never,
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
    Struct,
    Enum,
    Optional,
    List,
    Set,
    Map,
};

/// 已解析符号引用。canonical_name 是后端稳定身份，local/module 用于诊断和展示。
struct SymbolRef {
    SymbolRefKind kind{SymbolRefKind::Unknown};
    std::string canonical_name;
    std::string local_name;
    std::string module_name;
    std::optional<std::size_t> id; // Numeric symbol ID for O(1) cross-declaration lookup
};

/// 已解析或结构化的类型引用。
struct TypeRef;
using TypeRefPtr = Owned<TypeRef>;

struct TypeRef {
    TypeRefKind kind{TypeRefKind::Unresolved};
    std::string display_name;
    std::string canonical_name;
    std::string variant_name;
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
    std::optional<std::int64_t> decimal_scale;
    std::optional<SourceRange> source_range;
    TypeRefPtr first;
    TypeRefPtr second;
};

} // namespace ahfl::ir
