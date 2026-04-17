# AHFL Core V0.2 Semantics Architecture

本文说明 AHFL Core V0.2 `semantics` 层的实现组织，面向需要阅读 `resolver` / `typecheck` / `validate` 代码，或继续扩展静态语义能力的工程实现者。

关联文档：

- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [compiler-evolution-v0.2.zh.md](./compiler-evolution-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
- [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md)

## 目标

本文主要回答五个问题：

1. `semantics` 层为什么拆成 `resolve`、`typecheck`、`validate` 三个相邻 pass。
2. 这三个 pass 各自依赖哪些输入、产出哪些稳定对象。
3. project-aware 模式下，语义阶段如何携带 `module_name` / `source_id` 上下文。
4. 新能力应该落在哪个语义阶段，而不是跨层污染。
5. 读代码时，应该先看哪些对象，再看哪些 `.cpp` 内部实现。

## 总体定位

`semantics` 层位于：

- `frontend` 之后
- `ir` 之前

它不负责：

- 解析源码
- 重新访问文件系统
- 直接输出 backend 文本

它负责把“已经 lower 完成的 AST / SourceGraph”变成“稳定、可复用的语义结果”。

当前固定链路为：

```text
AST / SourceGraph
  -> resolve
  -> typecheck
  -> validate
  -> IR lowering
```

三者的分工应始终保持如下边界：

1. `resolve`
   - 回答“这个名字指向谁”。
2. `typecheck`
   - 回答“这个表达式或声明在静态类型上是否成立”。
3. `validate`
   - 回答“这个已经 resolve + typecheck 完成的结构，在领域约束上是否整体成立”。

## 公共边界

当前公共头文件为：

- `include/ahfl/semantics/resolver.hpp`
- `include/ahfl/semantics/typecheck.hpp`
- `include/ahfl/semantics/validate.hpp`

其 API 形态非常刻意：

1. 每个阶段都同时支持：
   - `const ast::Program &`
   - `const SourceGraph &`
2. 每个阶段都返回单独的 result object，而不是共享一个不断膨胀的“万能上下文”。
3. pass 内部状态留在 `.cpp` 中，不暴露到公共头。

这意味着：

1. 阶段间数据交换靠 result object。
2. 不鼓励跨阶段直接共享内部缓存。
3. 单文件模式和 project-aware 模式复用同一套语义边界。

## Resolver

### 作用

`Resolver` 的目标是建立稳定的名字绑定结果，包括：

- 顶层声明符号表
- 每个引用节点对应的目标符号
- import alias 绑定

关键对象：

- `SymbolNamespace`
- `SymbolKind`
- `ReferenceKind`
- `Symbol`
- `ResolvedReference`
- `ImportBinding`
- `SymbolTable`
- `ResolveResult`

### 实现组织

`src/semantics/resolver.cpp` 中的 `ResolverPass` 采用多阶段遍历，而不是单次边走边判定。

在 project-aware 模式下，固定顺序为：

```text
CollectImports
  -> RegisterSymbols
  -> ResolveReferences
  -> detect_type_alias_cycles
```

这样拆开的原因是：

1. import alias 必须先收集，后续 qualified name 才能稳定展开。
2. 所有顶层符号必须先注册，后续引用解析才能避免“遍历顺序偶然正确”。
3. type alias cycle 是全局图性质，适合在引用解析后统一检查。

### 名称空间模型

当前顶层名字按 namespace 分开管理：

- `Types`
- `Consts`
- `Capabilities`
- `Predicates`
- `Agents`
- `Workflows`

这意味着：

1. 顶层符号不是一个扁平 map。
2. 新 declaration kind 若可命名，必须先决定它进入哪个 namespace。
3. “同名是否冲突”是 namespace-sensitive 的，不应在 parser 或 backend 阶段偷做。

### canonical name 与 local name

`Symbol` 同时保留：

- `local_name`
- `canonical_name`
- `module_name`
- `source_id`

其中：

1. `local_name`
   - 当前 source/module 里看到的名字。
2. `canonical_name`
   - 稳定跨模块名字，供后续 typecheck / IR / backend 使用。
3. `module_name` + `source_id`
   - project-aware 模式下定位声明归属。

V0.2 之后，不应再假设“名字 + range”足以确定引用目标。

### import alias 与 source-local 生命周期

`ImportBinding` 和 `import_aliases_` 只在 source 上下文内生效。

这条约束很重要：

1. alias 不是全局符号。
2. resolver 不能把 alias 混进普通 `SymbolTable`。
3. typecheck / validate 若要理解 import 后的引用，应该消费 resolver 结果，而不是重做 alias 展开。

### resolver 该做什么

应该做：

- 注册顶层声明
- 解析 type name、call target、contract target、workflow node target 等引用
- 记录 import 绑定
- 诊断重复声明、歧义引用、未解析引用、type alias cycle

不该做：

- 推导表达式类型
- 决定 purity
- 校验状态机 reachability 或 workflow DAG
- 接触 backend-specific 结构

## TypeCheck

### 作用

`TypeChecker` 在 resolver 结果之上建立静态类型环境，并记录表达式类型。

关键对象：

- `TypeEnvironment`
- `StructTypeInfo`
- `EnumTypeInfo`
- `CapabilityTypeInfo`
- `PredicateTypeInfo`
- `AgentTypeInfo`
- `WorkflowTypeInfo`
- `ExpressionTypeInfo`
- `TypeCheckResult`

### 执行顺序

`TypeCheckPass::run()` 当前固定为：

```text
index_declarations
  -> build_type_environment
  -> check_const_initializers
  -> check_struct_defaults
  -> check_contracts
  -> check_flows
  -> check_workflows
```

这个顺序表达了一个明确设计：

1. 先把 declaration-level 类型签名冻住。
2. 再检查引用这些签名的表达式和结构。

不要反过来在检查表达式时临时拼装 declaration type info，这会让错误顺序和缓存行为不可预测。

### 类型环境的职责

`TypeEnvironment` 当前承载的是 declaration-level 稳定语义：

- 常量类型
- `struct` field 信息
- `enum` variant 信息
- capability / predicate 签名
- agent / workflow 输入输出 schema

它不是：

- 局部变量作用域
- 控制流图
- backend lowering cache

局部表达式环境由 `ValueContext` 和各类 `check_*` 过程在 `.cpp` 内部维护，而不是进入公共头。

### ExpressionTypeInfo 的定位

`ExpressionTypeInfo` 按 `SourceRange + SourceId` 记录：

- 表达式的静态类型
- 表达式是否 pure

这一步是 V0.2 很关键的公共边界，因为：

1. `validate` 会复用它来检查 temporal embedded expr 的 `Bool` / purity 约束。
2. IR lowering 与后续工具可以把“表达式的静态类型结果”视作稳定事实。
3. project-aware 模式下，同一个 range 在不同 source 中不能混淆，所以必须带 `source_id`。

### purity 的放置位置

当前 purity 是表达式类型检查的副产物，而不是 validate 阶段临时重算。

这样做的原因是：

1. purity 与表达式构成、调用、路径访问直接相关。
2. 同一表达式可能同时被 contract、temporal、flow 条件复用。
3. validate 只应消费“这个表达式已经被证明 pure / impure”的结果，而不应复制一份表达式语义分析器。

### type alias 的处理

类型别名在 typecheck 阶段采用：

- `resolved_alias_types_`
- `active_aliases_`

这说明别名展开是语义环境构建问题，而不是 resolver 的最终职责。

resolver 只负责把“名字指向哪个 type symbol”稳定下来；alias 是否递归、最终展开成什么 `TypePtr`，应由 typecheck 负责。

### typecheck 该做什么

应该做：

- 建 declaration-level 类型签名
- 检查 const/default/contract/flow/workflow 中的表达式类型
- 检查 assignability
- 记录 purity
- 检查 workflow node input / return 的 schema 兼容性

不该做：

- 验证 workflow 依赖图是否有环
- 验证 agent final state reachability
- 验证 temporal atom 是否出现在合法语境

这些属于 validate。

## Validate

### 作用

`Validator` 的职责不是再做一遍语义构建，而是检查“整体结构是否成立”。

`ValidationPass::run()` 当前固定为：

```text
index_declarations
  -> check_agents
  -> check_contracts
  -> check_flows
  -> check_workflows
```

### 当前主要检查项

#### 1. Agent

`check_agents()` 主要检查：

- state 重复
- initial / final state 是否已声明
- capability 重复
- transition 端点是否合法

#### 2. Contract

`check_contracts()` 主要检查：

- contract target 是否为 agent
- `called(...)` / `in_state(...)` / workflow-only atom 的语境合法性
- contract temporal formula 中状态名是否合法

#### 3. Flow

`check_flows()` 主要检查：

- handler state 是否存在
- handler 是否重复
- 是否缺失非 final state 的 handler
- `goto` 是否沿着 agent declaration 允许的 transition
- final-state handler 中的控制流是否合理

#### 4. Workflow

`check_workflows()` 主要检查：

- workflow node 名字是否重复
- 依赖节点是否存在
- workflow dependency 是否有环
- `running(...)` / `completed(...)` 的节点和 final state 是否有效
- contract-only atom 是否错误出现在 workflow safety/liveness 中

### validate 与 typecheck 的衔接

`validate` 复用 `TypeCheckResult::find_expression_type(...)` 来检查 temporal embedded expr：

- 必须是 `Bool`
- 必须是 pure

这条边界非常重要：

1. validate 关心“能不能用”，不关心“怎么推导出来”。
2. 若 validate 重新遍历表达式并手写类型规则，会和 typecheck 出现漂移。

### validate 该做什么

应该做：

- graph / 状态机 / DAG / temporal 语境级检查
- 基于已知类型结果做整体一致性校验

不该做：

- 重建 `TypeEnvironment`
- 重新解析名字
- 重新计算表达式 purity

## SourceGraph-aware 上下文模式

`resolve`、`typecheck`、`validate` 三个 pass 共享一个重要实现模式：

- `current_source_`
- `current_source_id_`
- `current_module_name_`

以及辅助函数：

- `enter_source(...)`
- `leave_source()`
- `source_unit_for(...)`
- `with_symbol_context(...)`

这个模式解决的是：

1. pass 在 graph 上遍历时，当前正在检查哪个 source。
2. 某个符号定义在别的 source 时，如何临时切换 diagnostics 与 lookup 上下文。
3. `find_reference(...)` / `find_expression_type(...)` 如何在多 source 下仍然稳定。

对后续实现的约束是：

1. 新语义逻辑若依赖 source 归属，优先复用这套上下文切换模式。
2. 不要在局部函数里自己偷偷拼 `module_name` / `source_id` 规则。
3. 不要让 diagnostics 脱离 source 上下文，否则 project-aware 模式会退化成难读的全局报错。

## 扩展落点指南

### 新增顶层声明

需要依次判断：

1. resolver
   - 是否进入新的或现有的 namespace。
2. typecheck
   - 是否需要新的 `*TypeInfo`。
3. validate
   - 是否有结构约束。

### 新增表达式

通常先改：

1. `TypeCheckPass::check_expr_impl(...)`
2. 对应 `check_*` 分支
3. `ExpressionTypeInfo` 记录路径

只有当它会影响 temporal / workflow / flow 结构合法性时，validate 才需要感知。

### 新增 temporal atom

通常先改：

1. resolver 的 `ReferenceKind` 与 temporal resolve 逻辑
2. typecheck 中 embedded expr 的上下文要求
3. validate 中语境合法性检查

如果某个 atom 需要 workflow-only 或 contract-only 约束，应该落在 validate，而不是 parser。

## 推荐阅读顺序

如果要快速建立心智模型，推荐按下面顺序读：

1. `include/ahfl/semantics/resolver.hpp`
2. `include/ahfl/semantics/typecheck.hpp`
3. `include/ahfl/semantics/validate.hpp`
4. `src/semantics/resolver.cpp`
5. `src/semantics/typecheck.cpp`
6. `src/semantics/validate.cpp`

阅读重点：

1. 先看 result object 和关键 map 长什么样。
2. 再看 pass 的固定执行顺序。
3. 最后再看每类 declaration / expr 的分支细节。

## 对后续实现的约束

后续继续扩展 `semantics` 时，应保持以下原则：

1. `resolve` 不升级成“半个 typechecker”。
2. `typecheck` 不升级成“半个 validator”。
3. `validate` 不回头重做 resolve / typecheck。
4. project-aware 语义始终通过 `source_id` / `module_name` 显式建模，不靠隐式全局状态猜测。
5. 公共头只暴露稳定结果，不暴露 pass 内部缓存与访问信息。
