# AHFL AST Model Architecture

本文说明 AHFL Core hand-written AST 的 flat declaration store、variant 节点分层、所有权规则和扩展方式，面向需要新增语法节点、阅读 `include/ahfl/compiler/frontend/ast.hpp` 或调整 lowering 输出形状的工程实现者。

关联文档：

- [compiler-phase-boundaries.zh.md](./compiler-phase-boundaries.zh.md)
- [frontend-lowering-architecture.zh.md](./frontend-lowering-architecture.zh.md)
- [semantics-architecture.zh.md](./semantics-architecture.zh.md)
- [ir-backend-architecture.zh.md](./ir-backend-architecture.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 AST 为什么按 declaration / type / expr / statement / temporal 五层组织。
2. `ast.hpp` 中哪些字段是稳定语法事实，哪些只是调试辅助。
3. 顶层 value store、递归子节点所有权和 variant visitor 为什么这样设计。
4. 新增语法能力时，AST 节点应该如何落位。

## 总体定位

AHFL 的 AST 是 parse tree lowering 之后的稳定语法边界。

它的职责是：

1. 保留后续阶段需要消费的结构化语法事实。
2. 与 generated ANTLR context 解耦。
3. 为 resolver/typecheck/validate/IR 提供统一输入。

它不负责：

- 名称解析
- 类型检查
- 领域级 validate
- backend lowering

## 为什么需要 hand-written AST

当前仓库并没有把 ANTLR parse tree 直接暴露给后续阶段，而是使用 hand-written AST，原因有三：

1. grammar rule 形状不等于后续阶段最适合消费的语义形状。
2. parse tree 生命周期不应穿透 frontend。
3. 后续阶段需要稳定公共头，而不是 generated 类型。

因此 `include/ahfl/compiler/frontend/ast.hpp` 的意义是：

- 作为编译器内部的稳定语法边界
- 让顶层声明使用闭合 `std::variant` 集合，新增 kind 时由编译器强制所有 visitor 更新
- 让 `Program` 通过连续 `std::vector<Decl>` 保存声明，避免基类指针、虚分发和 `dynamic_cast`

## 五层对象模型

当前 AST 大致可以分成五层：

1. 顶层 declaration
2. 类型语法
3. 值表达式
4. 语句与 block
5. temporal 表达式

这五层不是按源码文件拆开的，而是按后续消费方式拆开的。

## 顶层 declaration 层

顶层 declaration 以 `Program` 和 `Decl` 为入口：

```cpp
using Decl = std::variant<ModuleDecl,
                          ImportDecl,
                          UseDecl,
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
                          FnDecl,
                          TraitDecl,
                          ImplDecl>;

struct Program {
    SourceRange range;
    std::string source_name;
    std::vector<Decl> declarations;
};
```

每个 declaration payload 是普通 value struct，自带：

- `SourceRange range`
- `Visibility visibility`
- `duplicate_visibility_modifier`
- 该声明独有的语法字段

`NodeKind` 不是与 variant 并存的第二个 tag。消费者需要兼容旧的 kind 分类语言时，通过 `decl_kind(const Decl&)` 从 active alternative 确定性派生，禁止存储可能与 variant 漂移的平行 tag。

当前 `NodeKind` 已覆盖：

- `ModuleDecl`
- `ImportDecl`
- `ConstDecl`
- `TypeAliasDecl`
- `StructDecl`
- `EnumDecl`
- `CapabilityDecl`
- `PredicateDecl`
- `AgentDecl`
- `ContractDecl`
- `FlowDecl`
- `WorkflowDecl`

这层的设计重点是：

1. 每种顶层能力对应唯一 variant alternative。
2. `Program::declarations` 是 cache-friendly 的连续 flat store。
3. resolver 只理解 hand-written declaration，不关心 parser rule 名。
4. `decl_range`、`decl_visibility`、`decl_headline` 等 helper 从 active payload 派生公共元数据。
5. 声明地址只允许作为 `Program` 生命周期内的临时借用；跨阶段 canonical identity 使用 `SymbolId`、source identity 或 Typed HIR/IR index，不能依赖 vector 元素地址。

## 类型语法层

`TypeSyntax` 当前负责承载 surface type 的结构化形状。

核心种类包括：

- primitive kinds
- `Named`
- `Optional`
- `List`
- `Set`
- `Map`

以及参数化原语：

- bounded `String`
- `Decimal(scale)`

这层之所以独立存在，是因为 typecheck 需要处理的是：

- 类型树

而不是：

- 原始字符串拼写

## 值表达式层

`ExprSyntax` 当前承载：

- literal
- path
- qualified value
- call
- struct literal
- collection literal
- unary / binary
- member access / index access
- group / `some` / `none`

这一层是后续 typecheck 最密集消费的 AST 层。

重要约束：

1. 运算符优先级已经在 lowering 中固化为树形结构。
2. `text` 只是 source slice，不是后续语义推断依据。

## 语句与 block 层

flow handler 中的可执行语法被组织为：

- `BlockSyntax`
- `StatementSyntax`

语句种类包括：

- `Let`
- `Assign`
- `If`
- `Goto`
- `Return`
- `Assert`
- `Expr`

这样设计的原因是：

1. typecheck 需要逐条检查语句。
2. validate 需要理解控制流结构。
3. IR lowering 需要稳定 statement tree。

## temporal 表达式层

`TemporalExprSyntax` 与普通表达式分开建模，当前支持：

- `EmbeddedExpr`
- `Called`
- `InState`
- `Running`
- `Completed`
- `Unary`
- `Binary`

分开的原因不是“语法糖不同”，而是后续语义规则不同：

1. temporal atom 有专门的 resolve/validate 规则。
2. embedded expr 需要单独检查 Bool/purity 约束。
3. backend 对 temporal tree 有专门 lowering。

## SourceRange 与 text

几乎所有 AST 节点都携带：

- `SourceRange`

部分节点还携带：

- `text`
- `spelling`

这套设计的用法应区分清楚：

1. `SourceRange`
   - 是稳定定位信息，供 diagnostics 和后续查找使用。
2. `text` / `spelling`
   - 是可读性与调试辅助。

不要把 `text` 当作“低配 AST”，再在后续阶段做二次解析。

## 所有权模型

顶层 declaration 不使用 `Owned<Decl>`；`Program` 直接拥有 `Decl` values。

声明 payload 内的递归或可选语法子节点仍统一使用 `Owned<T>`，本质上是 `std::unique_ptr<T>`。

其设计意图是：

1. 顶层声明连续存储，不发生 polymorphic allocation。
2. 递归子节点生命周期明确。
3. frontend 构造 concrete payload 后，在 append boundary move 进 `Decl` variant。
4. 后续阶段只借用 AST，不重新拥有它。

可选子节点通过空 `Owned<T>` 表达，例如：

- `else_block`
- `default_value`
- `expr` / `temporal_expr`

这比使用 parser-specific nullable context 更稳定。

## 共享辅助枚举

AST 里当前还有一组共享枚举：

- `ContractClauseKind`
- `TypeSyntaxKind`
- `PathRootKind`
- `ExprSyntaxKind`
- `ExprUnaryOp`
- `ExprBinaryOp`
- `StatementSyntaxKind`
- `TemporalExprSyntaxKind`
- `TemporalUnaryOp`
- `TemporalBinaryOp`

`NodeKind` 仅作为 declaration kind 的兼容分类结果，由 `decl_kind()` 派生。其他共享枚举的作用是：

1. 冻结语法形状分类。
2. 让 typecheck / validate / printer / IR lowering 共享同一组分类语言。

如果新增语法能力需要后续多个阶段都识别，通常应先扩展这些枚举之一。

## Variant Visitor 边界

顶层 declaration 通过 `std::visit` 或 `visit_decl()` 分发：

```cpp
visit_decl(declaration, Overloaded{
    [&](const StructDecl &value) { /* ... */ },
    [&](const EnumDecl &value) { /* ... */ },
    [&](const auto &) { /* irrelevant declarations */ },
});
```

该边界具有以下约束：

1. AST data model 没有 inheritance、virtual method、`accept()` 或 `dynamic_cast`。
2. 需要 exhaustive handling 的消费者使用 `Overloaded`，新增 alternative 会产生编译错误。
3. 只关心一个 payload 的消费者使用 `std::get_if<T>()`。
4. 需要保留已有 kind-oriented算法时，可使用 `decl_kind()` + `std::get<T>()`，但不能重新引入平行 tag。
5. expression、type、pattern 和 temporal 节点继续使用各自的 `std::variant` visitor；statement 的剩余 tagged-struct 迁移由独立架构工作跟踪，不能成为恢复 declaration inheritance 的理由。

## AST 中“应该有”和“不应该有”的信息

AST 中应该有：

- 结构化语法事实
- source range
- 后续普遍要用的字段

AST 中不应该有：

- generated parser context
- token 指针
- resolve 结果
- typecheck 结果
- backend 私有字段

如果某个信息只对某一阶段有意义，而且可以在该阶段稳定重建，它通常不该进 AST 公共头。

## 新增节点时的落点指南

### 新增顶层 declaration

通常需要：

1. 新增普通 declaration payload struct。
2. 将 payload 加入 `Decl` variant，并同步 `NodeKind`/`decl_kind()` 兼容映射。
3. 更新 `ProgramBuilder`，在完成 concrete payload 构造后 move 进 flat store。
4. 修复编译器报告的 exhaustive visitor 缺口。
5. 更新 AST invariant validator、AST printer、resolver、sema、LSP 和相关测试。
6. 更新 `scripts/check-architecture.py` 的 exact variant shape；该门禁必须与 C++ 定义原子变化。

### 新增表达式

通常需要：

1. 扩展 `ExprSyntaxKind` 或相关 operator enum
2. 决定使用现有字段组合，还是新增专用字段
3. 更新 lowering、printer、typecheck、IR

### 新增 temporal atom

通常需要：

1. 扩展 `TemporalExprSyntaxKind`
2. 在 frontend 明确其语法形状
3. 在 semantics 再定义其语义约束

## 推荐阅读顺序

建议按下面顺序读：

1. `include/ahfl/compiler/frontend/ast.hpp`
2. `src/compiler/syntax/frontend/frontend.cpp` 中的 `ProgramBuilder`
3. `src/compiler/syntax/frontend/ast.cpp` 中的 invariant validator 和 declaration helpers
4. `src/compiler/syntax/frontend/ast_printer.cpp`
5. `scripts/check-architecture.py`
6. `src/compiler/semantics/resolver.cpp`
7. `src/compiler/semantics/typecheck_decls.cpp`

阅读重点：

1. 先看 node kind 和 enum 分类。
2. 再看每类语法节点持有哪些字段。
3. 最后看各阶段如何消费这些字段。

## 对后续实现的约束

后续继续扩展 AST 时，应保持以下原则：

1. AST 继续作为稳定 hand-written 语法边界，不引入 generated parser 类型。
2. AST 只表达语法事实，不承载 resolve/typecheck/backend 状态。
3. 顶层 declaration 只能进入 `vector<Decl>` flat store，不得恢复 `Owned<Decl>`、基类、虚 visitor 或 RTTI 分发。
4. 新节点应优先服务后续多个阶段的共享需求，而不是某个局部实现的临时方便。
5. 若一个字段只用于 dump/debug，应明确把它视为辅助字段，而不是公共语义契约。
