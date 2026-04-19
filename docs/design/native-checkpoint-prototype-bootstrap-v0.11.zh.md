# AHFL Native Checkpoint Prototype Bootstrap V0.11

本文冻结 AHFL V0.11 中 checkpoint prototype、checkpoint record / resume basis，以及 durable-adjacent local state 的最小边界。它承接 V0.10 已完成的 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot` 与 `SchedulerDecisionSummary`，目标是把仓库推进到“可稳定表达 checkpoint-facing machine artifact”的下一层边界，但仍不进入 durable checkpoint store、resume protocol、crash recovery 或 host telemetry 领域。

关联文档：

- [native-scheduler-prototype-bootstrap-v0.10.zh.md](./native-scheduler-prototype-bootstrap-v0.10.zh.md)
- [roadmap-v0.11.zh.md](../plan/roadmap-v0.11.zh.md)
- [issue-backlog-v0.11.zh.md](../plan/issue-backlog-v0.11.zh.md)

## 目标

本文主要回答六个问题：

1. V0.11 当前到底要交付什么 checkpoint prototype / resume basis 能力。
2. checkpoint record 的第一落点在哪里。
3. `SchedulerSnapshot`、checkpoint record、checkpoint review 与 future recovery ABI 各自如何分层。
4. future persistence prototype 可以稳定依赖哪条输入链路。
5. 哪些内容当前只属于 local checkpoint prototype，而不属于 durable recovery。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. durable checkpoint store、数据库 schema、object storage layout、replication 或 queue durability
2. 真实 resume protocol、crash recovery、distributed worker restart、failover orchestration 或 recovery daemon
3. wall clock metrics、machine id、worker id、host log payload 或 provider request/response capture
4. tenant、region、secret、deployment、endpoint、connector SDK integration 或 store credentials
5. 真正的 runtime resume executor、retry、timeout、backoff、cancellation propagation、preemption 或 statement-level interpreter

## 当前稳定输入链

V0.11 当前建议只沿着下面这条链路扩展 checkpoint prototype：

```text
ExecutionPlan
  + DryRunRequest
  + CapabilityMockSet
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord / ResumeBasis
  -> Checkpoint Review Summary
```

这表示：

1. checkpoint-facing state 的第一落点不是 AST、trace、scheduler review 或 host log，而是正式 `CheckpointRecord` artifact
2. `SchedulerSnapshot` 仍是 ready set、blocked frontier、executed prefix 与 `checkpoint_friendly` 的第一事实来源
3. checkpoint prototype 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint` 前进，而不是回退扫描 source、descriptor、review 文本或 host log
4. `SchedulerDecisionSummary` 仍只是 readable projection，不是 checkpoint ABI 的第一输入
5. future recovery protocol 只能在 checkpoint record 边界冻结后继续规划

## Checkpoint Prototype 的成功标准

V0.11 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing checkpoint record
2. checkpoint record 能回答“当前是否可形成 persistable prefix”
3. checkpoint record 能回答“当前 resume basis 是什么、还缺什么”
4. checkpoint review 能把 machine-facing checkpoint 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在已有 runtime-adjacent artifact 之上，而不是在 CLI / 文本输出层私造状态机

它不表示：

1. 当前仓库已经实现 durable persistence store
2. 当前仓库已经支持 crash recovery
3. 当前仓库已经拥有 production resume protocol

## Checkpoint Record 的第一边界

V0.11 当前建议 `CheckpointRecord` 至少回答下面五类问题：

1. 当前 checkpoint 是为哪个 package / workflow / session / run 生成的
2. 当前可持久化的 executed prefix 到哪里为止
3. 当前 resume 是否 ready；若未 ready，blocker 是什么
4. 当前 checkpoint 是 local-only basis，还是已经具备 durable-adjacent shape
5. 当前 checkpoint 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. persistable prefix 已结构化、可稳定序列化
2. checkpoint identity 与 source artifact identity 可被显式校验
3. resume basis / blocker 来源于正式 artifact，而不是 review 文本猜测

当前不要求：

1. durable checkpoint id
2. recovery handle / resume token
3. store location / replication metadata
4. host / machine / deployment payload

## Checkpoint Prototype / Scheduler Artifact / Future Recovery 的职责拆分

V0.11 当前冻结的分层关系是：

1. `RuntimeSession`
   - workflow / node 当前状态
   - 提供 final / partial / failed state snapshot
2. `ExecutionJournal`
   - deterministic event sequence
   - 提供 prefix ordering 与 failure progression
3. `ReplayView`
   - consistency projection
   - 只解释 session / journal 是否能形成稳定 progression
4. `SchedulerSnapshot`
   - scheduler-facing local state
   - 提供 ready set、blocked frontier、executed prefix 与 `checkpoint_friendly`
5. `CheckpointRecord`
   - checkpoint-facing machine artifact
   - 提供 persistable prefix、checkpoint identity、resume basis 与 resume blocker
6. `Checkpoint Review Summary`
   - reviewer-facing checkpoint summary
   - 只归档 checkpoint record 的可读视图，不重新创造私有 recovery 状态机
7. future recovery protocol / durable store
   - 不属于 V0.11 当前阶段
   - 只能在 checkpoint record compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 resume-ready 语义，再倒逼 checkpoint record 适配
2. 不允许 future persistence prototype 直接从 scheduler review、trace、source 或 host log 倒推 checkpoint state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 persistable prefix / blocker 语义

## 当前层次总表

V0.11 当前建议把 checkpoint-adjacent consumer 固定在下面七层：

| 层级 | 正式 artifact / 入口 | 当前职责 | 不应承担 |
|------|----------------------|----------|----------|
| Planning | `ExecutionPlan` | DAG、dependency、execution order、return contract | runtime 结果、checkpoint blocker |
| Runtime State | `RuntimeSession` | workflow / node 当前状态、partial / failed / completed snapshot | persistable prefix 结论、recovery ABI |
| Event Sequence | `ExecutionJournal` | deterministic event ordering、failure progression | readable review、store metadata |
| Consistency Projection | `ReplayView` | session / journal consistency、progression projection | checkpoint identity、resume token |
| Scheduler State | `SchedulerSnapshot` | ready set、blocked frontier、executed prefix、`checkpoint_friendly` | checkpoint record、durable store contract |
| Checkpoint State | `CheckpointRecord` | persistable prefix、checkpoint identity、resume basis、resume blocker | readable contributor summary、recovery daemon ABI |
| Review / Guidance | `CheckpointReviewSummary` | contributor-facing checkpoint readable projection、resume preview | machine-facing第一事实来源、durable mutation plan |

这张表的含义是：

1. `CheckpointRecord` 是 checkpoint-facing machine artifact 的第一正式落点。
2. `SchedulerSnapshot` 仍然只负责 scheduler local state，不应被动扩张成 persistence ABI。
3. `CheckpointReviewSummary` 只负责可读总结，不应反向定义 checkpoint state。
4. future recovery protocol 只能建立在 `CheckpointRecord` compatibility 冻结之后，而不是绕过当前层次。

## Checkpoint Review / Scheduler Review / Audit / Trace 的边界

V0.11 当前明确区分四类可读输出：

1. `SchedulerDecisionSummary`
   - 回答“scheduler 当前怎么看 ready / blocked / next action”
   - 第一输入是 `SchedulerSnapshot`
2. `CheckpointReviewSummary`
   - 回答“checkpoint 当前能否形成 persistable basis、resume 还缺什么”
   - 第一输入是 `CheckpointRecord`
3. `AuditReport`
   - 回答“这次运行最终应如何被 archive / CI / review 归档”
   - 第一输入仍是 runtime-adjacent aggregate，而不是 checkpoint ABI
4. `DryRunTrace`
   - 回答“本地 mock dry-run 做了什么”
   - 仍是 contributor-facing trace，不是 scheduler / checkpoint / recovery ABI

因此：

1. 不允许用 `SchedulerDecisionSummary` 倒推 `CheckpointRecord`
2. 不允许用 `CheckpointReviewSummary` 倒推 recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 checkpoint-facing第一事实来源

## Checkpoint Prototype 与 Future Recovery Protocol 的升级关系

V0.11 当前冻结的升级关系是：

1. `CheckpointRecord`
   - local / offline machine artifact
   - 只回答“当前 checkpoint basis 是否已准备好”
2. future recovery protocol
   - durable / resumable protocol layer
   - 才回答“如何恢复、由谁恢复、恢复句柄是什么”

这意味着：

1. future recovery protocol 可以复用 `CheckpointRecord` 的 persistable prefix、identity、basis / blocker
2. future recovery protocol 不应要求 V0.11 当前就携带 durable checkpoint id、resume token、store metadata
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `CheckpointRecord` / `CheckpointReviewSummary`

## Checkpoint-Facing State 对 Review / Recovery 的影响

V0.11 当前建议：

1. checkpoint record 必须区分“上游 runtime result failure”和“checkpoint-facing inconsistency / bootstrap error”
2. checkpoint review summary 必须区分“当前可生成 local checkpoint basis”与“当前已经是 durable recovery ABI”
3. future recovery guidance 可以讨论哪些字段可被 recovery protocol 继承，但不应直接把 resume token / store metadata 塞回当前 artifact

换句话说：

1. persistable prefix / checkpoint identity / resume basis 先属于 checkpoint record
2. reviewer-facing resume preview / next-step guidance 先属于 checkpoint review summary
3. durable checkpoint id / recovery handle / crash recovery 先不属于当前 artifact

## Future Persistence Prototype 当前应怎么消费

V0.11 future persistence prototype 当前只建议依赖：

1. `ExecutionPlan`
   - 提供 DAG / dependency / execution order
2. `RuntimeSession`
   - 提供 workflow / node 当前状态
3. `ExecutionJournal`
   - 提供 deterministic event ordering
4. `ReplayView`
   - 提供 session / journal consistency projection
5. `SchedulerSnapshot`
   - 提供 scheduler-facing ready set、blocked frontier 与 executed prefix
6. `CheckpointRecord`
   - 提供 persistable prefix、checkpoint identity 与 resume basis

当前不建议 future persistence prototype 直接依赖：

1. `SchedulerDecisionSummary`
   - 它仍是 reviewer-facing readable projection
2. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
3. AST / raw source / descriptor 文件
4. 真实 host log / provider payload

## 当前实现前的约束

Issue 02-09 开始编码前，后续实现必须遵守：

1. checkpoint prototype 的第一实现目标是稳定 artifact，不是 durable store 模拟器
2. builder diagnostics、runtime result failure 与 checkpoint-facing inconsistency 必须严格区分
3. checkpoint record / review summary 不得混入 host telemetry、deployment metadata、store metadata 或 provider payload
4. 若 checkpoint-facing state 需要改变现有 scheduler artifact 语义，应通过 artifact versioning 明确迁移
5. CLI 输出路径只负责序列化稳定 artifact，不得在 backend printer 中二次发明 resume-ready 语义

## 当前状态

截至当前设计：

1. V0.10 已完成 `plan -> session -> journal -> replay -> snapshot -> review` scheduler prototype 闭环
2. V0.11 M0 已冻结 checkpoint prototype 的 scope、non-goals 与上游输入链
3. 下一阶段实现应先进入 checkpoint record / resume basis 的最小模型与 layering，而不是先做 recovery 假实现或 durable store 占位
