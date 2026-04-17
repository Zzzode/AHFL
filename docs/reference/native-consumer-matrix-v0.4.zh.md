# AHFL Native Consumer Matrix V0.4

本文给出 AHFL V0.4 handoff package 的 runtime-facing consumer matrix，目标是让 future Native/runtime-adjacent consumer、backend 扩展者和贡献者快速判断“当前 handoff package 适合被谁消费、应依赖哪些字段、扩展时必须经过哪些公共路径”。

关联文档：

- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [issue-backlog-v0.4.zh.md](../plan/issue-backlog-v0.4.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 handoff package 面向哪些 runtime-facing consumer。
2. 不同 consumer 应依赖 handoff package 的哪一层边界。
3. 新增 handoff-adjacent consumer 时，最小扩展流程是什么。
4. 如何确认新的 runtime-facing 扩展没有绕开 driver / compatibility / docs / tests 体系。

## 当前 consumer 类型

V0.4 当前建议把 runtime-facing consumer 分成三类：

### 1. Package Reader

典型用途：

1. 读取 package identity / entry/export target
2. 做版本检查
3. 做 package 级 capability / policy surface 预检

当前可稳定依赖：

- 顶层 `format_version`
- `metadata`
- `capability_binding_slots`
- `policy_obligations`

不应依赖：

- 完整公式树
- deployment 配置
- 字段顺序

### 2. Execution Planner

典型用途：

1. 读取 workflow graph
2. 建立节点调度依赖
3. 承接最小 lifecycle 边界

当前可稳定依赖：

- workflow `execution_graph`
- node `lifecycle`
- workflow `input_summary`
- workflow `return_summary`

不应依赖：

- flow statement tree
- path-sensitive dataflow
- scheduler / retry / timeout 私有策略

### 3. Policy / Audit Consumer

典型用途：

1. 读取 contract / workflow obligation 边界
2. 关联 observation symbol
3. 对 capability / policy surface 做额外审计

当前可稳定依赖：

- `policy_obligations`
- `formal_observations`
- `capability_binding_slots`

不应依赖：

- temporal / expr 完整公式树作为长期 ABI
- runtime host / connector 配置

## Consumer Matrix

| Consumer | 主要用途 | 当前稳定输入 | 当前明确不承诺 |
|----------|----------|--------------|----------------|
| Package Reader | 版本/metadata/导出入口预检 | `format_version`、`metadata`、`capability_binding_slots`、`policy_obligations` | deployment 配置、字段顺序 |
| Execution Planner | workflow DAG 与最小 lifecycle 承接 | `execution_graph`、node `lifecycle`、workflow value summary | 完整执行器输入、scheduler 策略 |
| Policy / Audit Consumer | policy surface / observation 关联 | `policy_obligations`、`formal_observations`、capability slots | 公式树 ABI、host connector 私有细节 |
| Golden / CI Consumer | 稳定性 diff 与 compatibility 守卫 | 全量 `emit-native-json` 输出 | 运行时行为本身 |

## 当前 runtime-facing 扩展路径

新增 handoff-adjacent consumer 或 handoff backend 扩展时，当前建议顺序为：

```text
确认 consumer 类型
  -> 确认它依赖 package 的哪一层稳定边界
  -> 若为共享信息，先扩 handoff object model
  -> 更新 emitter / backend
  -> 通过 driver / CLI 接入
  -> 补 native golden / regression
  -> 更新 compatibility / consumer matrix / contributor docs
```

这条路径的关键约束是：

1. 不要绕开 `BackendKind -> driver -> ahflc`
2. 不要让 consumer 回退到 AST 或 raw source 补语义
3. 不要在未更新 compatibility 文档的情况下静默扩张稳定边界

## 最小扩展模板

当后续要新增一个 runtime-facing consumer 或 handoff-adjacent backend 时，最小交付应至少包含：

1. 明确它属于哪类 consumer
2. 明确它依赖 `metadata`、`execution_graph`、`capability_binding_slots`、`policy_obligations`、`formal_observations` 里的哪些边界
3. 若需要新增共享字段，先改 `include/ahfl/handoff/package.hpp`
4. 更新 `src/handoff/package.cpp` lowering
5. 更新 `src/backends/native_json.cpp` 或新增受控 backend
6. 补至少一个 `tests/native/` golden
7. 更新 [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
8. 更新本文档

## 何时不该新增 runtime-facing 边界

出现以下情况时，不应直接把需求加进 handoff package：

1. 信息只服务于某个单一 runtime adapter 的私有实现
2. 信息属于 deployment / environment / connector 配置
3. 信息需要完整执行解释器才能成立，而当前 handoff 只承诺受限摘要
4. 新字段无法通过“旧 consumer 忽略未知字段”安全退化

## 与现有 backend 扩展路径的关系

当前仓库里：

1. `emit-summary` 仍是最小 compiler-facing backend 扩展示例
2. `emit-native-json` 是当前 runtime-facing backend 扩展示例

两者共同验证：

1. 新 backend 不应绕开 driver
2. CLI 应保持薄层
3. golden / docs / compatibility 必须同步进入同一变更

区别在于：

1. `emit-summary` 主要验证 compiler/tooling consumer 路径
2. `emit-native-json` 主要验证 runtime-facing consumer 路径

## 当前状态

截至当前实现：

1. runtime-facing consumer matrix 已冻结为 Package Reader、Execution Planner、Policy / Audit Consumer 三类
2. `emit-native-json` 已覆盖这些 consumer 共同依赖的最小稳定边界
3. 后续若新增 handoff-adjacent consumer，应先对齐本文档和 [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
