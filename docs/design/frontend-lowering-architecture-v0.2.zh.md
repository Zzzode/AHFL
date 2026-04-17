# AHFL Core V0.2 Frontend Lowering Architecture

本文说明 AHFL Core V0.2 `frontend` 层中 hand-written AST lowering 的实现组织，重点解释 `ProgramBuilder`、表达式/时序表达式链式 lowering、source range 绑定，以及 project-aware 模式如何在 frontend 边界内完成 source graph 构建。

关联文档：

- [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
- [ast-model-architecture-v0.2.zh.md](./ast-model-architecture-v0.2.zh.md)
- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [diagnostics-architecture-v0.2.zh.md](./diagnostics-architecture-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)

## 目标

本文主要回答五个问题：

1. `src/frontend/frontend.cpp` 里的 hand-written lowering 是怎么组织的。
2. 为什么 `ProgramBuilder` 被视为 parse tree 和 AST 之间的唯一稳定边界。
3. 表达式、temporal formula、statement、type syntax 为什么要在 frontend 就 lower 成结构化树。
4. `SourceRange` 和原始 `text` 是如何在 lowering 时绑定的。
5. project-aware 模式为什么仍然属于 frontend，而不是 CLI 或 semantics。

## 总体定位

当前 `frontend` 层负责两件事：

1. `parse`
   - 调用 ANTLR 生成的 lexer/parser。
2. `ast lowering`
   - 把 parse tree lower 成 hand-written `ast::*` 节点。

project-aware 模式下，frontend 还额外负责：

3. `source graph construction`
   - 递归装载 source，并建立 `SourceGraph`。

但 frontend 仍然不负责：

- 名称解析
- 类型检查
- backend lowering

## 公共边界

当前公共头位于：

- `include/ahfl/frontend/frontend.hpp`
- `include/ahfl/frontend/ast.hpp`

对外稳定入口包括：

- `Frontend::parse_file(...)`
- `Frontend::parse_text(...)`
- `Frontend::parse_project(...)`
- `dump_program_outline(...)`
- `dump_project_outline(...)`

其中最重要的约束是：

1. 公共头只暴露 hand-written AST 和 source graph。
2. ANTLR parse tree、token stream、generated parser context 只停留在 `src/frontend/frontend.cpp`。

## ProgramBuilder 的角色

`ProgramBuilder` 是当前最关键的 lowering 对象。

它的角色不是“方便写点 helper”，而是：

- parse tree -> AST 的唯一手写边界

当前源码中已经明确写出这个意图：

1. 只有它应该常规遍历 generated ANTLR contexts。
2. resolver / checker / IR 都不能绕过它重新读 parse tree。

### 为什么要集中在一个 builder

把 lowering 集中到一个 `ProgramBuilder` 有三个好处：

1. grammar 变化的影响面集中。
2. AST 形状在一个模块里统一冻结。
3. diagnostics、`SourceRange`、`text` 绑定规则不会散落到后续阶段。

如果未来在 resolver/typecheck 里又开始访问 generated context，frontend 边界就会被重新打穿。

## 顶层 declaration lowering

`ProgramBuilder::build_program(...)` 当前按三类顶层元素装配 `ast::Program`：

1. `module`
2. `import`
3. 其他 top-level declaration

然后再按 `begin_offset` 排序后写入 `program->declarations`。

这条规则很重要，因为：

1. parse tree 的分组方式并不一定等于源码中的稳定展示顺序。
2. AST 需要保持源文件内的声明顺序，供 dump、resolver、IR 和 golden 使用。

### 顶层 declaration 分发

`build_top_level_decl(...)` 当前负责把 generated grammar 分支映射为 hand-written AST 节点，例如：

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

这说明 frontend 阶段已经做了一次很重要的“语法归一化”：

1. grammar 里的不同子规则被折叠为稳定 AST node kind。
2. 后续阶段只需理解 `ast::NodeKind`，而不必理解 parser rule 名字。

## 表达式 lowering 模式

### 1. precedence 在 frontend 冻结

值表达式当前采用分层函数链：

```text
build_implies_expr
  -> build_or_expr
  -> build_and_expr
  -> build_equality_expr
  -> build_compare_expr
  -> build_add_expr
  -> build_mul_expr
  -> build_unary_expr
  -> build_postfix_expr
  -> build_primary_expr
```

这意味着：

1. 运算符优先级和结合性在 frontend 就已经稳定。
2. 后续阶段拿到的是已经结构化的 AST，而不是等待再解释的 token 序列。

### 2. 通用 chain helper

`build_expr_chain(...)` 是当前的核心实现模式。

它负责：

- 收集中缀运算符文本
- 根据 left/right associativity 组装 AST
- 生成 `Binary` 节点及其 span range

这个 helper 的设计价值是：

1. 避免每层运算符重复写几乎相同的 folding 逻辑。
2. 让结合性成为显式参数，而不是隐藏在手写分支中。

当前 `implies` 采用右结合，其余多数二元运算采用左结合。

### 3. postfix 和 primary 负责“语法形状”冻结

`build_postfix_expr(...)` 当前把：

- `a.b`
- `a[i]`

lower 成：

- `MemberAccess`
- `IndexAccess`

`build_primary_expr(...)` 则负责区分：

- literal
- call
- struct literal
- qualified value
- path
- list/set/map literal
- `some(...)`
- `none`
- 分组表达式

这意味着后续阶段不应该再通过 `expr.text` 猜“它原来是不是一个 member access / call / some expr”。

## Temporal formula lowering 模式

temporal expression 当前和普通表达式类似，也采用一条独立的 precedence 链：

```text
build_temporal_implies_expr
  -> build_temporal_or_expr
  -> build_temporal_and_expr
  -> build_temporal_until_expr
  -> build_temporal_unary_expr
  -> build_temporal_atom
```

并复用通用 helper：

- `build_temporal_chain(...)`

### temporal atom 的边界

frontend 当前会把下面几类 atom 显式区分出来：

- `EmbeddedExpr`
- `Called`
- `InState`
- `Running`
- `Completed`

这里的设计重点是：

1. frontend 只区分语法形态，不做语义裁决。
2. `running(...)` / `completed(...)` 是否可用，交给 validate。
3. embedded expr 的类型和 purity，交给 typecheck/validate。

也就是说，frontend 负责“长成什么树”，不是“这个树在语境里是否合法”。

## Statement 和 type lowering 模式

### statement

当前 statement lowering 已经把 flow handler 中的语句稳定成：

- `Let`
- `Assign`
- `If`
- `Goto`
- `Return`
- `Assert`
- `Expr`

因此 typecheck / validate 不需要再从 block 文本中重新识别控制流结构。

### type syntax

`build_type_syntax(...)` / `build_primitive_type_syntax(...)` 当前把 surface type lower 成结构化 `TypeSyntax`：

- primitive kinds
- `Named`
- `Optional`
- `List`
- `Set`
- `Map`
- bounded `String`
- `Decimal(scale)`

这一步很关键，因为 resolver/typecheck 处理的是类型树，而不是原始类型字符串。

## SourceRange 与 text 绑定

frontend 当前通过一组 helper 统一处理位置：

- `context_range(...)`
- `token_range(...)`
- `terminal_range(...)`
- `span_range(...)`
- `source_text(...)`

配合：

- `make_decl(...)`
- `make_expr_syntax(...)`
- `make_temporal_expr_syntax(...)`

这套模式表达了两个边界：

1. 所有 AST 节点都应在 frontend 建立稳定 `SourceRange`。
2. 若节点保留 `text`，它只是 source slice 的快照，而不是后续阶段的主要输入。

### 为什么保留 `text`

当前 `ExprSyntax` / `TemporalExprSyntax` 保留 `text`，主要用于：

- dump/debug
- 错误消息辅助
- 局部可读性

不应该把它当作：

- resolver 的二次解析输入
- typecheck 的临时语义来源
- backend 的序列化依据

## parse_text / parse_file / parse_project 的关系

### parse_text

`parse_text(...)` 是最底层的 parse + lowering 入口。

它负责：

1. 构造 `SourceFile`
2. 安装 `DiagnosticErrorListener`
3. 执行 lexer/parser
4. 用 `ProgramBuilder` 生成 `ast::Program`

### parse_file

`parse_file(...)` 是文件系统 wrapper：

1. 读取文件内容
2. 组装 `display_name`
3. 转调 `parse_text(...)`

这保证了 parse tree 和 lowering 逻辑不会分裂成两套入口。

### parse_project

`parse_project(...)` 在 frontend 内部完成：

1. entry file 校验
2. search root 归一化
3. module -> path 解析
4. 递归装载 imported source
5. 生成 `SourceUnit` / `ImportEdge` / `SourceGraph`
6. 汇总 diagnostics

这里最关键的设计点是：

- project-aware 仍然是 frontend 责任

因为它本质上属于“稳定输入模型构建”，而不是语义判定。

## collect_program_imports 的作用

`collect_program_imports(...)` 说明 frontend 在 project-aware 模式下还承担一个中间职责：

- 从 AST 中提取 import 请求和 module 声明，供 graph 构建使用

注意这个步骤仍然属于 frontend，而不是 resolver，因为它做的是：

1. source-level dependency extraction
2. module ownership 检查

而不是名字绑定。

## AST printer 的角色

`AstPrinter` 和 `dump_program_outline(...)` 的作用不是“正式语义输出”，而是：

- 为 parser/lowering 提供可读调试边界

这也是为什么：

1. `dump-ast` 虽然现在可输出 project-aware 视图，但它仍然只是对多个单文件 AST 的分组展示。
2. project-aware 模式下的 `dump-project` 继续只负责 source graph 装载视图，而不是 AST 细节。

当前调试输出边界分得很明确：

1. `dump-ast`
   - 看单文件 AST，或按 source/module 分组的 project-aware AST
2. `dump-project`
   - 看 source graph 装载结果
3. `emit-ir`
   - 看 validate 后稳定 IR

## 对后续扩展的落点指南

### 新增语法结构

通常需要依次修改：

1. grammar
2. `ProgramBuilder`
3. `include/ahfl/frontend/ast.hpp`
4. `AstPrinter`

不要跳过 AST 层，直接在 resolver/typecheck 里读 grammar 细节。

### 新增运算符

优先判断：

1. 属于哪一层 precedence。
2. 左结合还是右结合。
3. 是否需要新增 `ExprSyntaxKind` / `TemporalExprSyntaxKind`，还是只扩展 operator enum。

### 新增 project-aware source 元数据

优先判断：

1. 它属于 `SourceUnit` / `ImportEdge` / `SourceGraph` 哪一层。
2. 它是否应该在 frontend 构建完成，而不是由 CLI 临时拼接。

## 推荐阅读顺序

建议按下面顺序读：

1. `include/ahfl/frontend/ast.hpp`
2. `include/ahfl/frontend/frontend.hpp`
3. `src/frontend/frontend.cpp` 中的 helper 与 `ProgramBuilder`
4. `src/frontend/frontend.cpp` 中的 `AstPrinter`
5. `src/frontend/project.cpp` 中的 project descriptor / source graph 装载路径

阅读重点：

1. 先看 AST node kind 和 syntax kind。
2. 再看 `ProgramBuilder` 如何从 grammar 分支映射到 AST。
3. 最后看 `src/frontend/project.cpp` 中的 source graph 构建，以及 `frontend.cpp` / `project.cpp` 之间的 debug 边界。

## 对后续实现的约束

后续继续扩展 frontend 时，应保持以下原则：

1. generated ANTLR context 不得泄漏到公共头。
2. `ProgramBuilder` 继续作为 parse tree -> AST 的唯一常规 lowering 边界。
3. precedence、结合性和语法形态都应在 frontend 冻结，不应留给后续阶段猜测。
4. `SourceRange` 和 source slice 应在 lowering 时建立，不应由后续阶段补写。
5. project-aware 输入模型仍由 frontend 构建，不应漂移到 CLI 或 semantics。
