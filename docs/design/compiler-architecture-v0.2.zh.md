# AHFL Core V0.2 Compiler Architecture

本文给出 AHFL Core V0.2 编译器的整体架构说明，面向需要阅读代码、扩展语义阶段或补充 backend 的工程实现者。

关联文档：

- [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [module-loading-v0.2.zh.md](./module-loading-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)
- [repository-layout-v0.2.zh.md](./repository-layout-v0.2.zh.md)

## 目标

本文主要回答四个问题：

1. 当前编译器有哪些模块，它们各自负责什么。
2. 一次编译请求会按什么顺序流经这些模块。
3. V0.2 的 project-aware 模式相对 V0.1 增加了哪些核心对象。
4. 如果要读代码或改代码，应该从哪一层开始建立心智模型。

## 总体分层

AHFL Core 当前可以按下面五层理解：

1. `support`
   - 基础设施：所有权、source range、diagnostics。
2. `frontend`
   - 解析与 AST lowering。
3. `semantics`
   - resolver、typecheck、validate 与语义类型环境。
4. `ir`
   - validate 之后的稳定中间表示，以及文本/JSON 序列化。
5. `backends` / `cli`
   - backend driver、SMV emitter、最终命令行入口。

对应源码目录：

```text
include/ahfl/support
include/ahfl/frontend
include/ahfl/semantics
include/ahfl/ir
include/ahfl/backends

src/frontend
src/semantics
src/ir
src/backends
src/cli
```

## 一次编译请求的主链路

当前编译主链路固定为：

```text
source file(s)
  -> parse
  -> AST lowering
  -> resolve
  -> typecheck
  -> validate
  -> IR lowering
  -> backend emit
```

单文件模式与 project-aware 模式共享后半段，只是在 frontend 的输入粒度不同：

- 单文件模式
  - `ParseResult { SourceFile, ast::Program, diagnostics }`
- project-aware 模式
  - `ProjectParseResult { SourceGraph, diagnostics }`

## 核心对象地图

### 1. Source / Diagnostics 基础对象

位于：

- `include/ahfl/support/source.hpp`
- `include/ahfl/support/diagnostics.hpp`

关键对象：

- `SourceFile`
- `SourceRange`
- `SourceId`
- `ImportRequest`
- `Diagnostic`
- `DiagnosticBag`

这一层的设计意图是：

1. 不携带语言语义。
2. 能被 frontend、semantics、ir、backends 共同复用。
3. 让所有后续阶段都通过统一的 source ownership 和 diagnostics 表达错误。

### 2. Frontend 对象

位于：

- `include/ahfl/frontend/ast.hpp`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`

关键对象：

- `ast::Program`
- `ast::Decl`
- `ParseResult`
- `SourceUnit`
- `SourceGraph`
- `ProjectInput`
- `ProjectParseResult`

frontend 的职责不是“做语义判断”，而是：

1. 接触 ANTLR parse tree。
2. 把 parse tree lower 成 hand-written AST。
3. 在 project-aware 模式下构建 source graph。

### 3. Resolver 对象

位于：

- `include/ahfl/semantics/resolver.hpp`
- `src/semantics/resolver.cpp`

关键对象：

- `Symbol`
- `SymbolTable`
- `ResolvedReference`
- `ImportBinding`
- `ResolveResult`

resolver 的核心问题是：

- “这个名字在当前 module/source 上下文里到底指向哪个声明？”

V0.2 之后，resolver 不再只处理单个 `ast::Program`，还要显式处理：

- `module_name`
- `source_id`
- import alias 的 source-local 生命周期

### 4. TypeCheck / Validate 对象

位于：

- `include/ahfl/semantics/typecheck.hpp`
- `include/ahfl/semantics/validate.hpp`
- `src/semantics/typecheck.cpp`
- `src/semantics/validate.cpp`

关键对象：

- `TypeEnvironment`
- `StructTypeInfo` / `EnumTypeInfo`
- `CapabilityTypeInfo` / `AgentTypeInfo` / `WorkflowTypeInfo`
- `ExpressionTypeInfo`
- `TypeCheckResult`
- `ValidationResult`

这一层分成两个相邻但不同的问题：

- `typecheck`
  - 表达式、类型、schema、参数与返回值是否静态成立。
- `validate`
  - 状态机、workflow DAG、contract temporal atom、flow 结构是否领域上成立。

### 5. IR / Backend 对象

位于：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `src/backends/smv.cpp`

关键对象：

- `ir::Program`
- declaration / expr / statement / temporal IR nodes
- `DeclarationProvenance`
- `FormalObservation`
- `BackendKind`

这一层的作用是：

1. 把 validate 之后的语义模型变成稳定后端边界。
2. 把 project-aware ownership 显式写进 IR，而不是让 backend 自己猜。
3. 把 shared `formal_observations` 提供给 JSON IR 和 formal backend。

## V0.2 相对 V0.1 的核心变化

如果只记 V0.2 的增量，可以记住下面四件事：

### 1. 输入从单文件扩展到了 source graph

新增的关键前提不再是“一个 `Program` 就代表一次编译”，而是：

- 一次编译可以由多个 source units 组成。

### 2. 名称解析和类型检查必须带 source/module 上下文

V0.2 之后，很多 lookup 不能再只靠：

- `name`
- `range`

而必须联合：

- `module_name`
- `source_id`
- `reference kind`

否则多文件下会出现同名声明冲突、同 range 假命中等问题。

### 3. IR 必须显式表达 provenance

project-aware emission 的关键不是“把多文件拼起来”，而是：

- 拼起来之后还能看出每条 declaration 原来属于哪个 module/source。

这就是 `DeclarationProvenance` 的意义。

### 4. formal backend 共享 `formal_observations`

V0.2 不再允许 backend 私下决定 contract/workflow 里的纯布尔 expr 怎么命名。
命名和 ownership 必须先冻结在 IR 层，再由 backend 消费。

## 各模块的实现概念

### `support`

实现概念：

- 把“语言无关但编译器离不开”的东西收拢到最底层。

典型特征：

- 头文件薄。
- 无 ANTLR 依赖。
- 无语义阶段依赖。

### `frontend`

实现概念：

- “唯一允许碰 parse tree 的层”。

典型特征：

- 负责 AST shape，而不是语义裁决。
- 负责 source graph 组装，而不是符号解析。
- diagnostics 尽量在这里就带上 source ownership。

### `resolver`

实现概念：

- 先把“名字属于谁”这件事彻底说清，再进入类型和领域规则。

典型特征：

- canonical name 全局稳定。
- local name 按 module/source 隔离。
- import alias 是 source-local 而不是 project-global。

### `typecheck`

实现概念：

- 为表达式和声明建立语义类型，不直接承担状态机或 workflow 结构规则。

典型特征：

- 维护 `TypeEnvironment`。
- 给 expression 建立 `ExpressionTypeInfo`。
- 在多文件模式下用 `source_id` 避免 lookup 假命中。

### `validate`

实现概念：

- 在“名字”和“类型”都已经稳定之后，再做领域完整性约束。

典型特征：

- 处理 flow/workflow/contract 的结构关系。
- 不重做 resolver/typecheck 的工作。
- 产出的错误应该是领域错误，而不是泛化的 type error。

### `ir`

实现概念：

- 作为 frontend 语义链路和 backend 的稳定分界面。

典型特征：

- 不依赖 parse tree。
- 不要求消费方理解 grammar trivia。
- 在 V0.2 下显式携带 provenance 和 shared observations。

### `backends`

实现概念：

- backend-specific exporter 只能消费已经稳定的 IR/语义模型。

典型特征：

- `driver` 做分发。
- `smv` 做 restricted formal lowering。
- 任何抽象边界都必须先写进设计文档。

## 推荐阅读顺序

如果你是第一次读这个编译器，建议顺序如下：

1. [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
2. 本文
3. [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
4. `include/ahfl/frontend/frontend.hpp`
5. `include/ahfl/semantics/resolver.hpp`
6. `include/ahfl/semantics/typecheck.hpp`
7. `include/ahfl/ir/ir.hpp`
8. [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)

如果你是要改实现，建议顺序如下：

1. 先确认改动属于哪一层。
2. 再看该层的公共头。
3. 最后再进 `.cpp` 看 pass state 与 helper 细节。

## 对后续工作的约束

后续如果继续扩编译器，建议坚持下面几条：

1. 不要把 loader 逻辑扩散到 semantics。
2. 不要让 backend 反向决定 IR 的 ownership 或 naming。
3. 不要把 validator 变成“第二个 typechecker”。
4. 新增 project-aware 能力时，先补公共对象和 diagnostics，再扩命令表面能力。
