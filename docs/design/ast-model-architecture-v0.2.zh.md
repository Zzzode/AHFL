# AHFL Core V0.2 AST Model Architecture

本文说明 AHFL Core V0.2 hand-written AST 的对象模型、节点分层、所有权规则和扩展方式，面向需要新增语法节点、阅读 `include/ahfl/frontend/ast.hpp` 或调整 lowering 输出形状的工程实现者。

关联文档：

- [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
- [frontend-lowering-architecture-v0.2.zh.md](./frontend-lowering-architecture-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 AST 为什么按 declaration / type / expr / statement / temporal 五层组织。
2. `ast.hpp` 中哪些字段是稳定语法事实，哪些只是调试辅助。
3. 所有权、可选子节点和 visitor 为什么这样设计。
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

因此 `include/ahfl/frontend/ast.hpp` 的意义是：

- 作为编译器内部的“稳定语法 ABI”

## 五层对象模型

当前 AST 大致可以分成五层：

1. 顶层 declaration
2. 类型语法
3. 值表达式
4. 语句与 block
5. temporal 表达式

这五层不是按源码文件拆开的，而是按后续消费方式拆开的。

## 顶层 declaration 层

顶层 declaration 以：

- `NodeKind`
- `Decl`
- `Program`

为入口。

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

1. 每种顶层能力都有稳定 node kind。
2. resolver 只需理解这些 hand-written declaration，而不关心 parser rule 名。
3. declaration headline 信息在 AST 上就已可读。

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

当前 AST 统一使用：

- `Owned<T>`

来表达树状拥有关系，本质上是 `std::unique_ptr<T>`。

其设计意图是：

1. AST 节点生命周期明确。
2. 前端可安全构造递归语法树。
3. 后续阶段只借用 AST，不重新拥有它。

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

这些枚举的作用是：

1. 冻结语法形状分类。
2. 让 typecheck / validate / printer / IR lowering 共享同一组分类语言。

如果新增语法能力需要后续多个阶段都识别，通常应先扩展这些枚举之一。

## Visitor 边界

顶层 declaration 当前通过 `Visitor` 提供统一遍历接口。

这样做的原因是：

1. resolver / printer / 其他 pass 可以按 declaration kind 分发。
2. 不需要在每个消费者里手写一层 `switch` + `static_cast`。

当前 visitor 边界主要覆盖 declaration 层，而不是把所有 expr/type/statement 都做成 visitor-heavy 模型。这是刻意的：

1. declaration 层适合 pass-oriented 分发。
2. expr/type/statement 目前更多通过显式递归函数消费。

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

1. 扩展 `NodeKind`
2. 新增 declaration struct
3. 接入 `Visitor`
4. 更新 `ProgramBuilder`
5. 更新 AST printer

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

1. `include/ahfl/frontend/ast.hpp`
2. `src/frontend/frontend.cpp` 中的 `ProgramBuilder`
3. `src/frontend/frontend.cpp` 中的 `AstPrinter`
4. `src/semantics/resolver.cpp`
5. `src/semantics/typecheck.cpp`

阅读重点：

1. 先看 node kind 和 enum 分类。
2. 再看每类语法节点持有哪些字段。
3. 最后看各阶段如何消费这些字段。

## 对后续实现的约束

后续继续扩展 AST 时，应保持以下原则：

1. AST 继续作为稳定 hand-written 语法边界，不引入 generated parser 类型。
2. AST 只表达语法事实，不承载 resolve/typecheck/backend 状态。
3. 新节点应优先服务后续多个阶段的共享需求，而不是某个局部实现的临时方便。
4. 若一个字段只用于 dump/debug，应明确把它视为辅助字段，而不是公共语义契约。
