# AHFL Native Runtime Session Bootstrap V0.7

本文冻结 AHFL V0.7 中 runtime session bootstrap 的最小边界。它承接 V0.6 的 Execution Plan、Capability Mock Set 与 Dry-Run Trace，但不定义生产 runtime launcher、真实 connector、deployment 或 persistence。

关联文档：

- [native-execution-plan-architecture-v0.6.zh.md](./native-execution-plan-architecture-v0.6.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](./native-dry-run-bootstrap-v0.6.zh.md)
- [roadmap-v0.7.zh.md](../plan/roadmap-v0.7.zh.md)

## 目标

本文主要回答四个问题：

1. runtime session 当前允许消费哪些输入。
2. runtime session 需要冻结哪些最小状态。
3. step-wise scheduler bootstrap 应如何与 V0.6 dry-run 区分。
4. runtime session 与 future launcher / scheduler / persistence 的职责如何拆分。

## 非目标

本文不定义：

1. 真实 capability connector 或 provider integration
2. deployment、tenant、region、secret、endpoint
3. persistence、checkpoint / resume、distributed scheduling
4. parallel execution、retry、timeout、backoff
5. 完整 statement / expression interpreter

## 输入边界

V0.7 runtime session 当前建议只消费四类输入：

```text
Execution Plan
  + CapabilityMockSet
  + DryRunRequest
  + RuntimeSessionOptions
  -> RuntimeSession
  -> ExecutionJournal
```

其中：

1. `Execution Plan`
   - 提供 workflow DAG、node lifecycle、binding reference、value summary
2. `CapabilityMockSet`
   - 提供 deterministic mock result fixture
3. `DryRunRequest`
   - 提供 workflow canonical name、input fixture、run id
4. `RuntimeSessionOptions`
   - 提供 scheduler bootstrap 策略选项，例如顺序模式或 event verbosity

runtime session 不应直接消费 AST、raw source、`ahfl.project.json`、`ahfl.workspace.json` 或 `ahfl.package.json`。

## Runtime Session 的最小状态

当前建议最小 `RuntimeSession` 至少包含：

1. 顶层 `format_version`
2. source execution plan identity
3. workflow canonical name
4. run id / session id
5. workflow status
6. node instance 状态
7. 已解析的 dependency satisfaction 结果
8. node mock usage 摘要

当前不承诺稳定的 session 状态包括：

1. host worker id
2. wall clock timestamp
3. memory / thread / process 指标
4. persistence offset 或 checkpoint id

## Step-Wise Scheduler Bootstrap

V0.7 当前建议把 scheduler bootstrap 限制为：

1. 单 workflow
2. 单进程 / 本地
3. deterministic
4. 默认顺序执行
5. 基于 `ExecutionPlan` DAG 产生显式 node state transition

这意味着：

1. V0.6 dry-run trace 解决的是“是否能跑通一条 deterministic review 路径”
2. V0.7 runtime session 解决的是“能否把这条路径提升为 step-wise runtime state surface”
3. 仍然不是生产 scheduler

## 与 Execution Journal 的关系

当前建议：

1. `RuntimeSession`
   - 表示某一时刻的 session state snapshot
2. `ExecutionJournal`
   - 表示 session bootstrap 过程中发生的稳定 event sequence
3. `DryRunTrace`
   - 仍可保留为 contributor-facing summary / review 面

换句话说：

- session 关注状态
- journal 关注事件
- trace 关注审查输出

## 与 future runtime 的边界

V0.7 runtime session bootstrap 不是 future launcher。它只用于：

1. 冻结 scheduler-ready 的最小 node state surface
2. 冻结 deterministic event model
3. 验证 execution plan 到 runtime state 的投影关系
4. 为 future replay / audit / scheduler prototype 提供正式输入边界

它不用于：

1. 证明真实 provider 可用
2. 证明部署配置正确
3. 证明并发调度或性能语义正确
4. 证明 persistence / recovery 正确

## 当前状态

截至当前设计：

1. V0.6 已把 execution plan、mock input 与 dry-run trace 冻结为稳定前置层
2. V0.7 下一步适合把这些输入提升为 runtime session + execution journal 的正式边界
3. 后续 scheduler / audit / replay 相关 consumer 都应以本文为共同边界
