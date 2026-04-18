# AHFL Native Consumer Bootstrap V0.5

本文冻结 AHFL V0.5 中 reference Package Reader 与 reference Execution Planner bootstrap 的最小边界。它承接 V0.4 handoff package consumer matrix 与 V0.5 package authoring 边界，但不定义真实 runtime、scheduler、connector 或 deployment 行为。

关联文档：

- [native-handoff-architecture-v0.4.zh.md](./native-handoff-architecture-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](./native-package-authoring-architecture-v0.5.zh.md)
- [native-package-metadata-v0.4.zh.md](./native-package-metadata-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](../reference/native-consumer-matrix-v0.4.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [roadmap-v0.5.zh.md](../plan/roadmap-v0.5.zh.md)

适用范围：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `src/backends/native_json.cpp`
- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/native/`
- `tests/handoff/`
- `docs/reference/`

## 目标

本文主要回答四个问题：

1. V0.5 仓库内 reference Package Reader 应验证什么。
2. V0.5 仓库内 reference Execution Planner bootstrap 应投影什么。
3. 这两类 reference consumer 可稳定依赖 handoff package 的哪些字段。
4. 哪些能力仍属于 future Native runtime，不能混入 bootstrap。

## 非目标

本文不定义：

1. runtime launcher、scheduler 或 worker lifecycle
2. capability connector / host adapter / tool execution
3. retry、timeout、concurrency、backpressure 或 distributed transaction 策略
4. policy enforcement runtime
5. workflow statement/data semantics 的完整解释器
6. package registry、publish、deployment 或 rollout

## 总体定位

V0.5 的 reference consumer 不是 runtime。它们的定位是：

```text
emit-native-json / handoff::Package
  -> reference Package Reader
  -> reference Execution Planner bootstrap
  -> future Native runtime implementation
```

这表示：

1. reference consumer 只消费 emitted handoff package 或等价 `handoff::Package` 对象。
2. reference consumer 不读取 AST、raw source、project descriptor 或 package authoring descriptor。
3. reference consumer 用于验证 handoff package 已足够支撑 future runtime-adjacent consumer 的最小接入。
4. reference consumer 产出的是 review / bootstrap 摘要，不是可执行 runtime plan。

## Consumer 边界

V0.5 只冻结两类 reference consumer：

### 1. Reference Package Reader

职责：

1. 读取 package format version
2. 读取 package metadata
3. 读取 executable target 列表
4. 读取 entry / export target
5. 读取 capability binding slots
6. 读取 package-level policy obligations

它回答的问题是：

1. 这个 package 的身份是什么？
2. 默认 entry target 是什么？
3. 对外 exports 是什么？
4. 运行前需要哪些 capability binding slot？
5. package 有哪些 policy / audit surface 需要被下游承接？

它不回答：

1. 如何启动 workflow
2. 如何绑定真实 connector
3. 如何执行 capability call
4. 如何调度节点
5. 如何 enforcement policy

### 2. Reference Execution Planner Bootstrap

职责：

1. 读取 workflow executable target
2. 读取 workflow execution graph
3. 读取 entry nodes 与 dependency edges
4. 读取 node lifecycle summary
5. 读取 workflow input / return summary
6. 产出最小 planner bootstrap 摘要

它回答的问题是：

1. 一个 workflow 包含哪些 node？
2. 哪些 node 是 graph entry？
3. node 之间有哪些 `after` dependency？
4. node 的受限 lifecycle 边界是什么？
5. workflow 的输入和返回值摘要是什么？

它不回答：

1. 何时真正启动 node
2. node 失败如何重试
3. capability call 如何执行
4. workflow value 如何在 runtime 内存中传递
5. 何时持久化状态或发出 observability 事件

## 稳定输入字段

Reference Package Reader 可依赖：

1. 顶层 `format_version`
2. `metadata`
3. `executable_targets`
4. `capability_binding_slots`
5. `policy_obligations`
6. `formal_observations`

Reference Execution Planner Bootstrap 可依赖：

1. workflow target identity
2. workflow `execution_graph`
3. graph `entry_nodes`
4. graph `dependency_edges`
5. node `lifecycle`
6. node `input_summary`
7. workflow `return_summary`

两者都不应依赖：

1. JSON 字段顺序
2. AST declaration 顺序
3. raw expression text 作为长期 ABI
4. source file path 作为 runtime identity
5. 未文档化的临时字段

## 最小输出形态

V0.5 不强制 reference consumer 必须是新 CLI 命令、测试 helper 还是库 API，但它们输出的稳定信息应至少能归约成下面两类摘要。

### Package Reader Summary

```text
package <name>@<version>
format: ahfl.native-package.v1
entry: workflow <canonical-name>
exports:
  - workflow <canonical-name>
capability bindings:
  - <capability> as <binding-key>
policy obligations:
  - <owner> <kind> <observation-symbol>
```

这类摘要用于 package review、consumer smoke test 与 compatibility diff。

### Execution Planner Bootstrap Summary

```text
workflow <canonical-name>
entry nodes:
  - <node>
dependencies:
  - <from> -> <to>
nodes:
  - <node>: target=<agent>, lifecycle=<summary>
value summary:
  input: <restricted-summary>
  return: <restricted-summary>
```

这类摘要用于验证 workflow graph 已足够支撑后续 planner 接入，但不承诺生成可执行 schedule。

## 错误与诊断边界

Reference consumer 应暴露三类错误：

### 1. Package Format 错误

例如：

1. `format_version` 不支持
2. required top-level field 缺失
3. 字段类型与 compatibility 文档不一致

### 2. Package Consistency 错误

例如：

1. entry target 不存在于 executable targets
2. export target 不存在于 executable targets
3. capability binding slot 引用重复 key
4. workflow graph dependency 指向不存在 node

### 3. Consumer Boundary 错误

例如：

1. planner bootstrap 被要求处理非 workflow target
2. package reader 被要求解析 deployment-only 字段
3. reference consumer 需要 raw source 才能继续工作

第三类错误尤其重要：它说明 handoff package 当前边界不足，或 consumer 实现越界。

## 与 V0.5 Package Authoring 的关系

V0.5 package authoring 负责让 author 显式提供 package identity、entry/export target 与 capability binding key。

Reference consumer 只消费 authoring 经过 compiler / handoff lowering 后形成的 package：

```text
ahfl.package.json
  -> PackageAuthoringInput
  -> handoff::PackageMetadata
  -> handoff::Package
  -> reference consumer
```

因此 reference consumer 不应：

1. 直接读取 `ahfl.package.json`
2. 重新解析 package authoring descriptor
3. 修复 authoring validation 漏掉的问题
4. 推断缺失的 entry/export target

如果 emitted package 缺少必要 metadata，reference consumer 应报告 package-level 错误，而不是回退到 authoring 输入。

## 与 Runtime 的关系

Reference Execution Planner Bootstrap 不是 scheduler。它只把 handoff package 中已稳定的 workflow graph 投影为 planner bootstrap 摘要。

它不拥有：

1. event loop
2. worker pool
3. state persistence
4. retry / timeout policy
5. connector execution
6. policy enforcement side effect

后续真正实现 AHFL Native runtime 时，可以把 bootstrap 摘要作为输入参考，但不能把它视为 runtime ABI 的全部。

## 推荐实现顺序

后续 Issue 08 实现 reference consumer 时，建议顺序为：

1. 先基于 `handoff::Package` 增加 Package Reader summary helper
2. 再基于 workflow target 增加 Execution Planner bootstrap helper
3. 再选择是否暴露 CLI debug / review 命令
4. 最后补 golden / regression / docs

不要反过来先写一个 JSON-specific parser，再回头把逻辑抽成公共对象模型。

## 对后续实现的约束

后续 Issue 08+ 实现必须遵守：

1. reference consumer 必须消费 emitted handoff package 或 `handoff::Package` 对象。
2. reference consumer 不得读取 AST、raw source、project descriptor 或 `ahfl.package.json`。
3. Package Reader 不得承诺 deployment / connector 配置。
4. Execution Planner Bootstrap 不得承诺 scheduler / retry / timeout / persistence 行为。
5. 若 bootstrap 需要新共享字段，应先扩 handoff package object model，并同步 compatibility / golden / docs。
6. 若 bootstrap 只服务单一 runtime 私有实现，不应进入公共 reference consumer。

## 当前状态

本文冻结 V0.5 reference Package Reader 与 Execution Planner Bootstrap 的最小边界。M0 的 scope、package authoring 与 consumer bootstrap 三个设计门已具备进入 M1/M2 实现前的共同术语和职责拆分。
