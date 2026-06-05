#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir {

// ----------------------------------------------------------------------------
// 声明来源追踪 (Declaration Provenance)
// ----------------------------------------------------------------------------

/// 声明来源信息（用于诊断和调试）
struct DeclarationProvenance {
    std::string module_name; // 所属模块名
    std::string source_path; // 源文件路径
    SourceRangeOpt source_range;
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
    ExprPtr value;
    TypeRef type_ref;
    SymbolRef symbol_ref;
};

/// 类型别名: type NewName = ExistingType;
struct TypeAliasDecl {
    DeclarationProvenance provenance;
    std::string name;
    TypeRef aliased_type_ref;
    SymbolRef symbol_ref;
};

/// 结构体字段声明
struct FieldDecl {
    std::string name;
    ExprPtr default_value; // 可选的默认值
    TypeRef type_ref;
    SourceRangeOpt source_range;
};

/// 结构体声明: struct Name { field1: Type1; ... }
struct StructDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<FieldDecl> fields;
    SymbolRef symbol_ref;
};

/// 枚举声明: enum Name { Variant1; Variant2; }
struct EnumDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<std::string> variants;
    SymbolRef symbol_ref;
};

/// 参数声明
struct ParamDecl {
    std::string name;
    TypeRef type_ref;
    SourceRangeOpt source_range;
};

/// Capability 副作用类别。IR 中保留结构化值，backend 再映射为目标工件枚举。
enum class CapabilityEffectKind {
    Unknown,
    Read,
    ExternalSideEffect,
    DurableWrite,
    FinancialWrite,
};

/// Capability 执行回执要求
enum class CapabilityReceiptMode {
    None,
    Optional,
    Required,
};

/// Capability 重试安全性声明
enum class CapabilityRetryMode {
    Unsafe,
    SafeIfIdempotent,
    Safe,
};

/// Capability effect analysis 的输入事实，由 DSL 显式声明并随 IR 传播。
struct CapabilityEffectSpec {
    bool declared{false};
    CapabilityEffectKind kind{CapabilityEffectKind::Unknown};
    std::optional<std::string> domain;
    std::optional<std::string> idempotency_key;
    CapabilityReceiptMode receipt_mode{CapabilityReceiptMode::None};
    CapabilityRetryMode retry_mode{CapabilityRetryMode::Unsafe};
    std::optional<std::string> timeout;
    std::optional<std::string> compensation;
    std::vector<std::string> policies;
    SourceRangeOpt source_range;
};

/// 能力声明: capability Name(param1: Type1, ...) -> ReturnType;
struct CapabilityDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    TypeRef return_type_ref;
    CapabilityEffectSpec effect;
    SymbolRef symbol_ref;
};

/// 谓词声明: predicate Name(param1: Type1, ...);
struct PredicateDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    SymbolRef symbol_ref;
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
    std::vector<std::string> states;         // 状态集合
    std::string initial_state;               // 初始状态
    std::vector<std::string> final_states;   // 终止状态集合
    std::vector<QuotaItem> quota;            // 资源配额
    std::vector<TransitionDecl> transitions; // 合法状态转换
    TypeRef input_type_ref;
    TypeRef context_type_ref;
    TypeRef output_type_ref;
    std::vector<SymbolRef> capability_refs;
    SymbolRef symbol_ref;
};

/// 契约子句
struct ContractClause {
    ContractClauseKind kind{ContractClauseKind::Requires};
    std::variant<ExprPtr, TemporalExprPtr> value; // 普通表达式或时序逻辑表达式
    SourceRangeOpt source_range;
};

/// 契约声明: contract for AgentName { requires ...; ensures ...; }
struct ContractDecl {
    DeclarationProvenance provenance;
    std::vector<ContractClause> clauses; // 契约子句列表
    SymbolRef target_ref;
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
    SourceRangeOpt source_range;
};

/// 流程声明: flow for AgentName { state Init { ... } ... }
struct FlowDecl {
    DeclarationProvenance provenance;
    std::vector<StateHandler> state_handlers; // 各状态的处理器
    SymbolRef target_ref;
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
    ExprPtr input;                     // 传递给 agent 的输入表达式
    std::vector<std::string> after;    // DAG 依赖（前置节点名列表）
    SymbolRef target_ref;
    SourceRangeOpt source_range;
};

/// 工作流声明 — 多 Agent DAG 编排
struct WorkflowDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<WorkflowNode> nodes;       // DAG 节点列表
    std::vector<TemporalExprPtr> safety;   // 安全性属性
    std::vector<TemporalExprPtr> liveness; // 活性属性
    ExprPtr return_value;                  // 最终返回值表达式
    TypeRef input_type_ref;
    TypeRef output_type_ref;
    SymbolRef symbol_ref;
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

} // namespace ahfl::ir
