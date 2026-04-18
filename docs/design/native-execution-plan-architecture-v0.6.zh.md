# AHFL Native Execution Plan Architecture V0.6

本文冻结 AHFL V0.6 中 Execution Plan artifact 的最小边界。它承接 V0.5 的 `handoff::Package` 与 `ExecutionPlannerBootstrap`，但不定义真实 runtime scheduler、connector 或 deployment 行为。

关联文档：

- [native-consumer-bootstrap-v0.5.zh.md](./native-consumer-bootstrap-v0.5.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](./native-package-authoring-architecture-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](../reference/native-handoff-usage-v0.5.zh.md)
- [native-consumer-matrix-v0.5.zh.md](../reference/native-consumer-matrix-v0.5.zh.md)
- [roadmap-v0.6.zh.md](../plan/roadmap-v0.6.zh.md)

## 目标

本文主要回答四个问题：

1. V0.6 的 Execution Plan artifact 解决什么问题。
2. Execution Plan 与 `handoff::Package`、planner bootstrap helper 的关系是什么。
3. 哪些字段进入当前稳定边界，哪些仍留给 future runtime。
4. 后续 `emit-execution-plan`、dry-run runner 与 future scheduler 应如何复用这层 artifact。

## 非目标

本文不定义：

1. 真实 scheduler、parallel execution、retry 或 timeout 策略
2. capability connector / provider / SDK / secret / endpoint 配置
3. state persistence、checkpoint / resume、tenant / region / deployment 语义
4. 完整 agent state machine execution 或 expression interpreter

## 当前问题

V0.5 当前已经提供：

```text
ahfl.package.json
  -> handoff::Package
  -> build_package_reader_summary(...)
  -> build_execution_planner_bootstrap(...)
```

但这条路径还缺一层正式 artifact：

1. `ExecutionPlannerBootstrap` 只是 helper 返回对象，不是稳定输出格式
2. CLI 还没有 execution plan emitter
3. dry-run runner 未来若直接消费 helper 临时对象，会把 planner 语义绑死在 C++ 内部表示上

因此 V0.6 需要把 planner bootstrap 提升为正式 Execution Plan。

## 分层关系

当前建议层次为：

```text
package authoring descriptor
  -> handoff::Package
  -> Execution Planner Bootstrap helper
  -> Execution Plan artifact
  -> local dry-run runner
  -> future runtime scheduler / launcher
```

职责拆分：

1. `handoff::Package`
   - 表示 runtime-adjacent 的完整 handoff contract
2. `ExecutionPlannerBootstrap`
   - 表示从 package 中抽出的 planner bootstrap 语义
3. Execution Plan
   - 表示可输出、可回归、可被 runner / future scheduler 消费的稳定 planning artifact

## 最小稳定字段

V0.6 当前建议把以下字段纳入 Execution Plan 的最小稳定边界：

1. 顶层 `format_version`
2. source package identity / provenance
3. workflow canonical name
4. workflow input / output type
5. entry nodes
6. dependency edges
7. node name、target、after dependencies
8. node lifecycle 摘要
9. node input summary
10. workflow return summary
11. capability binding references

当前不承诺进入长期稳定边界的内容：

1. runtime worker id / host assignment
2. concurrency slot、retry policy、timeout policy
3. connector endpoint / auth / secret
4. state snapshot / persistence record
5. 真实 node output payload

## 建议的最小形态

Execution Plan 的建议形态：

```text
ExecutionPlan
  -> PlanIdentity
  -> source package metadata
  -> workflow plans[]

WorkflowPlan
  -> workflow canonical name
  -> input/output type
  -> entry nodes[]
  -> dependency edges[]
  -> nodes[]
  -> return summary

WorkflowNodePlan
  -> node name
  -> target agent canonical name
  -> after[]
  -> lifecycle summary
  -> input summary
  -> capability binding references[]
```

## 与 V0.5 helper 的关系

V0.6 当前建议：

1. `build_execution_planner_bootstrap(...)`
   - 继续作为 direct helper
2. 新增 `build_execution_plan(...)`
   - 从 `handoff::Package` 或 bootstrap helper 构建正式 plan object
3. `emit-execution-plan`
   - 只序列化正式 plan object，不直接拼装 workflow 语义

## 与 future runtime 的边界

Execution Plan 不是 scheduler，也不是 launcher。它只回答：

1. 要执行哪个 workflow
2. workflow 中有哪些 node
3. node 之间有哪些依赖
4. node 对应哪个 target agent
5. 每个 node 的最小 lifecycle / value summary 是什么

当前不回答：

1. 何时重试
2. 何时超时
3. 如何并行
4. 如何做 host placement
5. 如何恢复中断状态

## 当前状态

截至当前设计：

1. V0.6 已把 Execution Plan 明确定义为介于 planner bootstrap helper 与 future runtime scheduler 之间的独立 artifact
2. 这层 artifact 继续依赖 handoff package 的稳定语义，不重新定义 Core / AST / authoring 规则
3. 后续 `emit-execution-plan`、dry-run runner 与 compatibility 文档都应以本文为共同边界
