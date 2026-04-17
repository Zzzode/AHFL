# AHFL Native Handoff Architecture V0.4

本文冻结 AHFL 在 V0.4 阶段从 Core 编译器走向后续 Native/runtime 的 handoff 边界。它不定义 runtime 实现，而是定义“Core 最终应交给 Native 什么稳定工件”。

关联文档：

- [core-scope-v0.1.en.md](./core-scope-v0.1.en.md)
- [native-package-metadata-v0.4.zh.md](./native-package-metadata-v0.4.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [native-handoff-usage-v0.4.zh.md](../reference/native-handoff-usage-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](./project-descriptor-architecture-v0.3.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)
- [roadmap-v0.4.zh.md](../plan/roadmap-v0.4.zh.md)

适用范围：

- `include/ahfl/frontend/frontend.hpp`
- `include/ahfl/handoff/package.hpp`
- `src/frontend/frontend.cpp`
- `src/handoff/package.cpp`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `src/cli/ahflc.cpp`
- `docs/reference/`
- `tests/`

## 目标

本文主要回答四个问题：

1. V0.4 要把 Core 的哪个边界正式变成 Native handoff 工件。
2. handoff 工件和当前 IR / JSON IR 是什么关系。
3. 哪些 runtime-facing 信息应该保留，哪些仍然不能承诺。
4. Core 编译器、project descriptor、后续 Native runtime 的职责如何拆分。

## 非目标

本文不定义：

1. runtime launcher、scheduler 或 host integration
2. `tool` 实现体、`llm_config`、host connector 配置
3. observability / compliance authoring 运行时
4. registry、distribution、remote package publish
5. statement/data semantics 的完整执行模型

## 当前问题

V0.3 完成后，仓库已经具备：

1. project-aware frontend / checker / IR / backend 边界
2. manifest / workspace 驱动的项目入口
3. 受限但稳定的 flow summary、workflow value summary、formal observation export
4. downstream tooling 可消费的 `emit-ir` / `emit-ir-json` / `emit-summary` / `emit-smv`

但它仍缺一个正式约定：

- 如果未来进入 AHFL Native，Core 编译器究竟应该交出怎样的 runtime-facing handoff 工件。

当前 IR 更偏向“编译器与 backend 的公共语义边界”，而不是“Native runtime 的直接消费工件”。如果不先冻结这层边界，后续很容易出现两种错误：

1. 直接让 runtime 回读 AST / raw source / ad-hoc project metadata
2. 让某个单一 runtime 实现反过来绑架 Core IR 的形状

## 总体定位

V0.4 的定位不是“开始写 runtime”，而是：

- 把 Core 输出收束成一份 future Native 可消费的 handoff package / execution descriptor

整体链路应保持为：

```text
manifest/workspace
  -> ProjectInput
  -> SourceGraph
  -> validated semantic model
  -> stable IR
  -> native handoff package
  -> future AHFL Native runtime
```

这表示：

1. handoff package 建立在 validate 之后，而不是 parser 之后。
2. handoff package 是 IR 的 runtime-facing 投影，不是新的 parser 输出。
3. Native runtime 不应重新定义 module loading、resolver 或 typecheck 规则。

## Handoff Package 应保留什么

V0.4 期望 future Native handoff package 至少保留下面几类信息：

### 1. Package / Project Identity

- package / project 名称
- entry workflow 或 entry agent
- package format version
- 与 source graph 对应的稳定 provenance

### 2. Execution Graph

- workflow node DAG
- `after` 依赖
- node target agent
- node lifecycle 的受限摘要

当前实现备注：

1. `emit-native-json` 已显式导出 `execution_graph`
2. `execution_graph` 当前至少包含 `entry_nodes` 与 `dependency_edges`
3. 每个 workflow node 当前已显式导出最小 `lifecycle` 摘要，包括启动条件、完成条件、completion latch 语义，以及目标 agent 的 initial/final states

### 3. External Capability Surface

- capability 接口签名
- 哪些 agent / flow / workflow 间接依赖了哪些 capability
- 供 runtime 绑定外部执行器的稳定 capability identity

### 4. Policy / Contract Surface

- contract clause 的 runtime-facing obligation 边界
- workflow safety / liveness 边界
- shared `formal_observations`

当前实现备注：

1. `emit-native-json` 已显式导出 package 级 `policy_obligations`
2. `policy_obligations` 当前统一承接 contract `requires` / `ensures` / `invariant` / `forbid` 与 workflow `safety` / `liveness`
3. obligation 当前导出 owner target、clause index 与 observation symbol 关联，不直接把完整公式树承诺为 runtime ABI

### 5. Restricted Control / Data Summary

- flow handler summary
- workflow value summary

当前实现备注：

1. capability dependency surface 当前由 `capability_binding_slots` 统一表达
2. restricted workflow value summary 当前由 node `input_summary` 与 workflow `return_summary` 稳定表达
3. `emit-native-json` 已能在同一份 handoff package 中同时导出 policy surface、capability surface 与 workflow value summary

这层信息的用途不是承诺完整执行语义，而是让 runtime 或后续 Native tooling 不必重新回看 AST 才能理解：

1. 什么会被执行
2. 依赖关系是什么
3. 哪些约束必须在 runtime 中继续被承接

## Handoff Package 不应保留什么

V0.4 当前明确不承诺把下面内容变成 runtime ABI：

1. parse tree / token trivia
2. 原始源码字符串作为执行依据
3. `tool` body、prompt、host-side connector 配置
4. capability 参数值、返回值、次数、时序的完整执行语义
5. flow 语句逐步求值与 path-sensitive dataflow
6. runtime scheduler、retry/backoff/timeout、distributed transaction 细节

换句话说：

- handoff package 是“执行边界摘要 + 约束边界”，不是“完整解释器输入”。

## 与当前 IR 的关系

V0.4 应保持下面的层级：

### IR

- 面向编译器 backend / tooling consumer 的稳定公共语义边界

### Native Handoff Package

- 面向 future Native runtime / runtime-adjacent tooling 的运行时投影边界

这两者关系应是：

1. handoff package 建立在 IR 之上
2. handoff package 可以省略 runtime 不需要的语法细节
3. handoff package 不得要求 consumer 回退到 AST 或源码补语义
4. handoff package 不应反过来把 IR 强行收缩成某个 runtime 私有格式

## Descriptor 与 Packaging 的关系

当前 `ahfl.project.json` / `ahfl.workspace.json` 只负责 project 输入，不负责 runtime packaging。

V0.4 的约束是：

1. project descriptor 继续负责“从哪里编译”
2. future handoff/package metadata 负责“要交给 Native 什么工件”
3. 不能把 runtime deployment / scheduler 配置偷塞回 module loading 或 workspace selection

这意味着 V0.4 后续实现必须先回答：

1. package metadata 是 project descriptor 的可选区段，还是独立 handoff descriptor
2. entry workflow / export target 如何显式声明
3. package identity 与 project identity 是否一一对应

这些问题在 [native-package-metadata-v0.4.zh.md](./native-package-metadata-v0.4.zh.md) 中进一步冻结为 V0.4 的最小 metadata 约束。

## CLI / Backend 约束

若 V0.4 引入新的 handoff 导出命令，必须遵守：

1. 继续经由 backend driver 分发
2. 继续支持 `--search-root`、`--project`、`--workspace --project-name`
3. CLI 不直接承担 handoff object 的拼装细节

建议目标形态类似：

- `emit-native-json`
- 或等价的 handoff/package emission 命令

但命令名本身不是本文的核心；核心是：

- runtime-facing 导出仍是 validate 后的统一 backend 路径，而不是一段 CLI 特判逻辑

当前实现备注：

1. 仓库已落地 `emit-native-json`
2. 该命令继续通过 `BackendKind -> driver -> ahflc` 统一接入
3. `handoff` 仍保持为独立层，负责 package object model 与 lowering；JSON 序列化位于独立 backend，而不是回塞到 CLI 或 `ir`

## 对后续实现的约束

Issue 01-03 之后，V0.4 相关实现必须遵守：

1. 不要直接把 runtime feature request 变成 grammar 扩张
2. 不要让 Native runtime 重新定义 Core 的 compile unit / symbol / type 规则
3. 不要让 handoff package 依赖 raw source 文本补语义
4. 若某项信息被多个 runtime-adjacent consumer 共享，优先先冻结到 IR，再投影到 handoff package
5. 若某项信息只是单一 runtime adapter 的私有细节，不应进入公共 handoff 契约

## 当前建议顺序

V0.4 更稳妥的推进顺序应是：

1. 先冻结 handoff package 的定位和字段边界
2. 再决定 package metadata 与 entry/export model
3. 再引入新的 emission backend
4. 最后补 compatibility、golden、CI 和 contributor docs

不要反过来先造一个 runtime-specific JSON，再回头猜哪些字段应该长期稳定。
