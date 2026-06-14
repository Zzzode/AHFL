#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/types.hpp"

namespace ahfl::ir {

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

/// Stable handle for an arena-owned expression node.
///
/// The index is the durable identity used by Program::expr_arena; the cached
/// pointer keeps existing recursive IR algorithms cheap and avoids threading
/// arena lookups through every visitor/printer/evaluator call site.
struct ExprRef {
    using Index = std::uint32_t;
    static constexpr Index kInvalid = UINT32_MAX;

    Expr *ptr{nullptr};
    Index index{kInvalid};

    ExprRef() = default;
    ExprRef(Expr &expr, Index expr_index) noexcept : ptr(&expr), index(expr_index) {}
    ExprRef(std::nullptr_t) noexcept {}
    ExprRef &operator=(std::nullptr_t) noexcept {
        ptr = nullptr;
        index = kInvalid;
        return *this;
    }

    [[nodiscard]] bool has_value() const noexcept { return ptr != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] Expr *get() const noexcept { return ptr; }
    [[nodiscard]] Expr &operator*() const noexcept {
        assert(ptr != nullptr);
        return *ptr;
    }
    [[nodiscard]] Expr *operator->() const noexcept {
        assert(ptr != nullptr);
        return ptr;
    }

    [[nodiscard]] friend bool operator==(const ExprRef &ref, std::nullptr_t) noexcept {
        return ref.ptr == nullptr;
    }
    [[nodiscard]] friend bool operator==(std::nullptr_t, const ExprRef &ref) noexcept {
        return ref.ptr == nullptr;
    }
    [[nodiscard]] friend bool operator!=(const ExprRef &ref, std::nullptr_t) noexcept {
        return ref.ptr != nullptr;
    }
    [[nodiscard]] friend bool operator!=(std::nullptr_t, const ExprRef &ref) noexcept {
        return ref.ptr != nullptr;
    }
};

struct TemporalExpr;
using TemporalExprPtr = Owned<TemporalExpr>;

struct Statement;
using StatementPtr = Owned<Statement>;

using SourceRangeOpt = std::optional<SourceRange>;

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
    ExprRef value;
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
    std::vector<ExprRef> arguments; // 实参列表
};

/// 结构体字段初始化
struct StructFieldInit {
    std::string name; // 字段名
    ExprRef value;    // 初始化表达式
};

/// 结构体字面量: TypeName { field1: val1, field2: val2 }
struct StructLiteralExpr {
    std::string type_name;               // 类型名
    std::vector<StructFieldInit> fields; // 字段初始化列表
};

/// 列表字面量: [a, b, c]
struct ListLiteralExpr {
    std::vector<ExprRef> items;
};

/// 集合字面量: {a, b, c}
struct SetLiteralExpr {
    std::vector<ExprRef> items;
};

/// Map 键值对
struct MapEntryExpr {
    ExprRef key;
    ExprRef value;
};

/// Map 字面量: {k1: v1, k2: v2}
struct MapLiteralExpr {
    std::vector<MapEntryExpr> entries;
};

/// 一元表达式: !expr, -expr
struct UnaryExpr {
    ExprUnaryOp op{ExprUnaryOp::Not};
    ExprRef operand;
};

/// 二元表达式: a + b, a == b
struct BinaryExpr {
    ExprBinaryOp op{ExprBinaryOp::Implies};
    ExprRef lhs;
    ExprRef rhs;
};

/// 成员访问表达式: expr.member
struct MemberAccessExpr {
    ExprRef base;       // 基对象
    std::string member; // 成员名
};

/// 索引访问表达式: expr[index]
struct IndexAccessExpr {
    ExprRef base;  // 基对象
    ExprRef index; // 索引表达式
};

/// 表达式节点（19 种 variant）
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
                              IndexAccessExpr>;

/// 表达式包装结构
struct Expr {
    ExprNode node;
    SourceRangeOpt source_range;
    TypeRef resolved_type; // Populated during lowering; kind=Unresolved if unavailable
    std::uint32_t id{0};  // Monotonic node ID assigned during lowering (E-2)
};

// ----------------------------------------------------------------------------
// 时序逻辑表达式层 (Temporal Expression Layer)
// ----------------------------------------------------------------------------

/// 嵌入普通表达式
struct EmbeddedTemporalExpr {
    ExprRef expr;
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
    SourceRangeOpt source_range;
};

// ----------------------------------------------------------------------------
// 语句层 (Statement Layer)
// ----------------------------------------------------------------------------

/// 语句块: { stmt1; stmt2; ... }
struct Block {
    std::vector<StatementPtr> statements;
    SourceRangeOpt source_range;
};

/// let 绑定语句: let name: Type = initializer;
struct LetStatement {
    std::string name;    // 变量名
    TypeRef type_ref;    // 绑定类型
    ExprRef initializer; // 初始化表达式
};

/// 赋值语句: target = value;
struct AssignStatement {
    Path target;   // 赋值目标路径
    ExprRef value; // 赋值表达式
};

/// 条件语句: if (condition) { then } else { else }
struct IfStatement {
    ExprRef condition;       // 条件表达式
    Owned<Block> then_block; // then 分支
    Owned<Block> else_block; // else 分支（可选）
};

/// 状态跳转语句: goto StateName;
struct GotoStatement {
    std::string target_state; // 目标状态名
};

/// 返回语句: return expr;
struct ReturnStatement {
    ExprRef value; // 返回值表达式
};

/// 断言语句: assert(condition);
struct AssertStatement {
    ExprRef condition; // 断言条件
};

/// 表达式语句 (如 capability 调用): expr;
struct ExprStatement {
    ExprRef expr;
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
    SourceRangeOpt source_range;
    std::uint32_t id{0}; // Monotonic statement ID assigned during lowering
};

} // namespace ahfl::ir
