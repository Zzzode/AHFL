# AHFL Native Scheduler Prototype Bootstrap V0.10

本文冻结 AHFL V0.10 中 richer scheduler prototype、scheduler snapshot / cursor，以及 checkpoint-friendly local state 的最小边界。它承接 V0.9 已完成的 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView` 与 `AuditReport`，目标是把仓库推进到“可稳定表达 scheduler-facing local state”的下一层 artifact 边界，但仍不进入生产 scheduler、durable persistence、checkpoint store 或 host telemetry 领域。

关联文档：

- [native-partial-session-failure-bootstrap-v0.9.zh.md](./native-partial-session-failure-bootstrap-v0.9.zh.md)
- [roadmap-v0.10.zh.md](../plan/roadmap-v0.10.zh.md)
- [issue-backlog-v0.10.zh.md](../plan/issue-backlog-v0.10.zh.md)

## 目标

本文主要回答六个问题：

1. V0.10 当前到底要交付什么 scheduler prototype / checkpoint-friendly 能力。
2. scheduler snapshot / cursor 的最小状态集合是什么。
3. scheduler-facing state 的第一落点在哪里。
4. `RuntimeSession`、`ExecutionJournal`、`ReplayView`、scheduler snapshot 与 review summary 各自如何分层。
5. future persistence explorer 可以稳定依赖哪条输入链路。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. 生产 runtime launcher、distributed worker orchestration、host daemon 或 control plane
2. durable persistence store、真实 checkpoint 文件格式、resume protocol、crash recovery 或 durable queue
3. wall clock metrics、machine id、worker id、host log payload 或 provider request/response capture
4. tenant、region、secret、deployment、endpoint、connector SDK integration
5. 真正的并发调度、retry、timeout、backoff、cancellation propagation 或 preemption

## 当前稳定输入链

V0.10 当前建议只沿着下面这条链路扩展 scheduler prototype：

```text
ExecutionPlan
  + DryRunRequest
  + CapabilityMockSet
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot / Cursor
  -> Scheduler Review Summary
```

这表示：

1. scheduler-facing state 的第一落点不是 AST、trace 或 host log，而是正式 scheduler snapshot / cursor artifact
2. `RuntimeSession` 仍是 workflow / node 当前状态的第一事实来源
3. `ExecutionJournal` 仍是 deterministic event ordering / failure progression 的第一事实来源
4. `ReplayView` 仍只负责 session + journal 的 consistency projection
5. richer scheduler prototype 应沿着 `plan -> session -> journal -> replay -> scheduler snapshot` 前进，而不是回退扫描 source、descriptor 或 host log

## Scheduler Snapshot / Cursor 的最小模型

V0.10 当前冻结的 scheduler prototype 最小问题是：

1. 当前哪些 node 可作为 next runnable set
2. 哪些 node 仍被阻断，以及阻断原因是什么
3. 当前 executed prefix 停在什么位置
4. workflow 当前是可继续推进、已 terminal，还是只能保持 partial
5. 当前 local state 是否已达到 checkpoint-friendly 边界

当前建议 `SchedulerSnapshot` / `SchedulerCursor` 至少向下面的最小集合演进：

### A. 顶层 prototype 状态

1. `Runnable`
   - 当前存在 ready set，可继续推进局部调度
2. `Waiting`
   - 当前不存在 ready set，但 workflow 仍未 terminal
3. `TerminalCompleted`
   - workflow 已成功完成
4. `TerminalFailed`
   - workflow 已形成稳定失败结论
5. `TerminalPartial`
   - workflow 当前保留 partial snapshot，但本轮 prototype 不再继续推进

### B. Ready Set 最小边界

ready set 当前建议至少回答：

1. 哪些 node 当前可执行
2. 每个 ready node 对应哪个 execution order / dependency 前缀
3. 是否已经拥有稳定输入摘要与 capability binding 摘要

### C. Blocked Reason 最小边界

blocked reason 当前建议至少回答：

1. 当前 node 是依赖未满足、被 terminal workflow 阻断，还是被 failure-path 前缀阻断
2. 当前 blocked reason 是否来自正式 runtime artifact，而不是 CLI / script 私造解释
3. 当前 blocked node 是否仍可能在 future step 变为 ready

### D. Executed Prefix / Cursor 最小边界

cursor 当前建议至少回答：

1. 当前已执行到哪个 deterministic prefix
2. 当前 next decision 应从哪个 ready set / blocked frontier 继续
3. 当前 snapshot 是否能被视为 checkpoint-friendly local state

这里的 checkpoint-friendly 只表示：

1. 当前 state 已结构化、可稳定序列化
2. future persistence explorer 可据此讨论 durable checkpoint 应该承接哪些字段

它不表示：

1. 当前仓库已经实现 durable checkpoint store
2. 当前仓库已经支持 resume protocol 或 crash recovery

## Scheduler Prototype / Runtime Artifact / Future Persistence 的职责拆分

V0.10 当前冻结的分层关系是：

1. `RuntimeSession`
   - final / partial / failed state snapshot
   - 拥有 workflow / node 当前状态与最小 failure summary
2. `ExecutionJournal`
   - step-wise event sequence
   - 拥有 deterministic readiness / start / complete / fail progression
3. `ReplayView`
   - consistency projection
   - 只解释 session / journal 是否能形成 deterministic progression
4. `SchedulerSnapshot / Cursor`
   - scheduler-facing local state
   - 拥有 ready set、blocked frontier、executed prefix 与 next-step summary
5. `Scheduler Review Summary`
   - reviewer-facing prototype summary
   - 只归档 scheduler snapshot 的可读视图，不重新创造私有状态机
6. future persistence / recovery protocol
   - 不属于 V0.10 当前阶段
   - 只能在 scheduler snapshot 边界冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 next-step 语义，再倒逼 scheduler snapshot 适配
2. 不允许 future persistence explorer 直接从 trace / source / host log 倒推 checkpoint state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 ready-set / blocked-reason 语义

## Scheduler-Facing State 对 Review / Persistence 的影响

V0.10 当前建议：

1. scheduler snapshot 必须区分“上游 runtime result failure”和“scheduler-facing consistency / bootstrap error”
2. scheduler review summary 必须区分“当前可继续推进”与“当前只是 checkpoint-friendly local state”
3. future persistence guidance 可以讨论哪些字段可被 durable checkpoint 继承，但不应直接把 checkpoint id / resume token 塞回当前 artifact

换句话说：

1. ready set / blocked reason / executed prefix 先属于 scheduler snapshot
2. reviewer-facing next-step summary 先属于 scheduler review summary
3. durable checkpoint id / resume token / crash recovery 先不属于当前 artifact

## Future Persistence Explorer 当前应怎么消费

V0.10 future persistence explorer 当前只建议依赖：

1. `ExecutionPlan`
   - 提供 DAG / dependency / execution order
2. `RuntimeSession`
   - 提供 workflow / node 当前状态
3. `ExecutionJournal`
   - 提供 deterministic event ordering
4. `ReplayView`
   - 提供 session / journal consistency projection
5. `SchedulerSnapshot / Cursor`
   - 提供 scheduler-facing ready set、blocked frontier 与 executed prefix

当前不建议 future persistence explorer 直接依赖：

1. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
2. AST / raw source / descriptor 文件
3. 真实 host log / provider payload

## 当前实现前的约束

Issue 04-09 开始编码前，后续实现必须遵守：

1. scheduler prototype 的第一实现目标是稳定 artifact，不是生产 runtime 模拟器
2. builder diagnostics、runtime result failure 与 scheduler-facing inconsistency 必须严格区分
3. scheduler snapshot / review summary 不得混入 host telemetry、deployment metadata 或 provider payload
4. 若 scheduler-facing state 需要改变现有 runtime artifact 语义，应通过 artifact versioning 明确迁移
5. CLI 输出路径只负责序列化稳定 artifact，不得在 backend JSON printer 中二次发明 next-step 语义

## 当前状态

截至当前设计：

1. V0.9 已完成 `plan -> session -> journal -> replay -> audit` failure-path 闭环
2. V0.10 M0 已冻结 scheduler prototype / checkpoint-friendly 的 scope、最小模型与分层边界
3. 下一阶段实现应先进入 scheduler snapshot / cursor model 与 bootstrap，而不是先做 review surface 或 persistence 假实现
