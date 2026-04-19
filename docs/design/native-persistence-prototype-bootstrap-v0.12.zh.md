# AHFL Native Persistence Prototype Bootstrap V0.12

本文冻结 AHFL V0.12 中 persistence prototype、persistence descriptor / planned durable identity，以及 store-adjacent local state 的最小边界。它承接 V0.11 已完成的 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord` 与 `CheckpointReviewSummary`，目标是把仓库推进到“可稳定表达 persistence-facing machine artifact”的下一层边界，但仍不进入真实 durable checkpoint store、resume protocol、crash recovery 或 host telemetry 领域。

关联文档：

- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](./native-checkpoint-prototype-bootstrap-v0.11.zh.md)
- [roadmap-v0.12.zh.md](../plan/roadmap-v0.12.zh.md)
- [issue-backlog-v0.12.zh.md](../plan/issue-backlog-v0.12.zh.md)

## 目标

本文主要回答六个问题：

1. V0.12 当前到底要交付什么 persistence prototype / persistence handoff 能力。
2. persistence descriptor 的第一落点在哪里。
3. `CheckpointRecord`、persistence descriptor、persistence review 与 future durable store / recovery ABI 各自如何分层。
4. future persistence prototype 可以稳定依赖哪条输入链路。
5. 哪些内容当前只属于 local persistence prototype，而不属于 durable recovery。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability 或 garbage collection
2. 真实 resume protocol、crash recovery、distributed worker restart、failover orchestration 或 recovery daemon
3. wall clock metrics、machine id、worker id、host log payload 或 provider request/response capture
4. tenant、region、secret、deployment、endpoint、connector SDK integration 或 store credentials
5. 真正的 runtime resume executor、retry、timeout、backoff、cancellation propagation、preemption 或 statement-level interpreter

## 当前稳定输入链

V0.12 当前建议只沿着下面这条链路扩展 persistence prototype：

```text
ExecutionPlan
  + DryRunRequest
  + CapabilityMockSet
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord / ResumeBasis
  -> CheckpointPersistenceDescriptor / PersistenceExportBasis
  -> Persistence Review Summary
```

这表示：

1. persistence-facing state 的第一落点不是 AST、trace、checkpoint review、scheduler review 或 host log，而是正式 `CheckpointPersistenceDescriptor` artifact
2. `CheckpointRecord` 仍是 persistable prefix、checkpoint identity、resume basis 与 blocker 的第一事实来源
3. persistence prototype 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence` 前进，而不是回退扫描 source、descriptor、review 文本或 host log
4. `CheckpointReviewSummary` 仍只是 readable projection，不是 persistence ABI 的第一输入
5. future durable store / recovery protocol 只能在 persistence descriptor 边界冻结后继续规划

## Persistence Prototype 的成功标准

V0.12 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing persistence descriptor
2. persistence descriptor 能回答“当前是否可形成稳定 persistence handoff”
3. persistence descriptor 能回答“planned durable checkpoint identity 是什么、还缺什么”
4. persistence review 能把 machine-facing persistence 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在已有 runtime-adjacent artifact 与 `CheckpointRecord` 之上，而不是在 CLI / 文本输出层私造状态机

它不表示：

1. 当前仓库已经实现 durable persistence store
2. 当前仓库已经支持 crash recovery
3. 当前仓库已经拥有 production resume protocol

## Persistence Descriptor 的第一边界

V0.12 当前建议 `CheckpointPersistenceDescriptor` 至少回答下面五类问题：

1. 当前 persistence handoff 是为哪个 package / workflow / session / run / checkpoint 生成的
2. 当前可导出的 checkpoint prefix 到哪里为止
3. 当前 persistence handoff 是否 ready；若未 ready，blocker 是什么
4. 当前 planned durable identity / export basis 是 local planning only，还是已经具备 stable store-adjacent shape
5. 当前 persistence descriptor 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. exportable prefix 已结构化、可稳定序列化
2. planned durable identity 与 source artifact identity 可被显式校验
3. persistence blocker 来源于正式 artifact，而不是 review 文本猜测

当前不要求：

1. durable checkpoint id
2. recovery handle / resume token
3. store location / replication metadata
4. host / machine / deployment payload

## Checkpoint / Persistence / Future Recovery 的职责拆分

V0.12 当前冻结的分层关系是：

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
6. `CheckpointPersistenceDescriptor`
   - persistence-facing machine artifact
   - 提供 planned durable identity、exportable prefix、persistence basis 与 persistence blocker
7. `Persistence Review Summary`
   - reviewer-facing persistence summary
   - 只归档 persistence descriptor 的可读视图，不重新创造私有 durable store / recovery 状态机
8. future durable store / recovery protocol
   - 不属于 V0.12 当前阶段
   - 只能在 persistence descriptor compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 persistence-ready 语义，再倒逼 persistence descriptor 适配
2. 不允许 future persistence prototype 直接从 checkpoint review、scheduler review、trace、source 或 host log 倒推 persistence state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 exportable prefix / blocker 语义

## Layering-Sensitive Compatibility 规则

V0.12 当前还必须额外冻结下面三条 layering-sensitive 规则：

1. `CheckpointRecord` 仍然是 persistence descriptor 的直接 machine-facing 上游
   - `CheckpointPersistenceDescriptor` 可以消费 checkpoint artifact
   - 但不应回退让 `CheckpointReviewSummary` 变成 machine-facing 第一输入
2. `PersistenceReviewSummary` 仍然只是 projection
   - 它可以总结 export preview / store boundary / next-step recommendation
   - 但不应承担 durable store mutation、resume token 派发、recovery command synthesis
3. future durable store / recovery protocol 仍然是下一层
   - 它可以复用 persistence descriptor 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、recovery handle 或 operator payload

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `CheckpointReviewSummary` 或 `SchedulerDecisionSummary` 反向成为 persistence descriptor 的第一输入
2. 让 `CheckpointPersistenceDescriptor` 承担真实 durable store mutation plan 或 recovery launcher ABI
3. 让 `PersistenceReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 persistence-facing 第一事实来源

## 当前层次总表

V0.12 当前建议把 persistence-adjacent consumer 固定在下面八层：

| 层级 | 正式 artifact / 入口 | 当前职责 | 不应承担 |
|------|----------------------|----------|----------|
| Planning | `ExecutionPlan` | DAG、dependency、execution order、return contract | runtime 结果、checkpoint blocker |
| Runtime State | `RuntimeSession` | workflow / node 当前状态、partial / failed / completed snapshot | exportable prefix 结论、recovery ABI |
| Event Sequence | `ExecutionJournal` | deterministic event ordering、failure progression | readable review、store metadata |
| Consistency Projection | `ReplayView` | session / journal consistency、progression projection | persistence identity、resume token |
| Scheduler State | `SchedulerSnapshot` | ready set、blocked frontier、executed prefix、`checkpoint_friendly` | checkpoint / persistence contract |
| Checkpoint State | `CheckpointRecord` | persistable prefix、checkpoint identity、resume basis、resume blocker | durable store handoff、recovery daemon ABI |
| Persistence State | `CheckpointPersistenceDescriptor` | planned durable identity、exportable prefix、persistence basis、persistence blocker | readable contributor summary、durable mutation execution |
| Review / Guidance | `PersistenceReviewSummary` | contributor-facing persistence readable projection、export preview | machine-facing第一事实来源、durable mutation plan |

这张表的含义是：

1. `CheckpointPersistenceDescriptor` 是 persistence-facing machine artifact 的第一正式落点。
2. `CheckpointRecord` 仍然只负责 checkpoint-facing machine state，不应被动扩张成 store contract。
3. `PersistenceReviewSummary` 只负责可读总结，不应反向定义 persistence state。
4. future durable store / recovery protocol 只能建立在 `CheckpointPersistenceDescriptor` compatibility 冻结之后，而不是绕过当前层次。

## Persistence Review / Checkpoint Review / Scheduler Review / Audit / Trace 的边界

V0.12 当前明确区分五类可读输出：

1. `SchedulerDecisionSummary`
   - 回答“scheduler 当前怎么看 ready / blocked / next action”
   - 第一输入是 `SchedulerSnapshot`
2. `CheckpointReviewSummary`
   - 回答“checkpoint 当前能否形成 persistable basis、resume 还缺什么”
   - 第一输入是 `CheckpointRecord`
3. `PersistenceReviewSummary`
   - 回答“当前能否形成 stable persistence handoff、planned durable identity 还缺什么”
   - 第一输入是 `CheckpointPersistenceDescriptor`
4. `AuditReport`
   - 回答“这次运行最终应如何被 archive / CI / review 归档”
   - 第一输入仍是 runtime-adjacent aggregate，而不是 persistence ABI
5. `DryRunTrace`
   - 回答“本地 mock dry-run 做了什么”
   - 仍是 contributor-facing trace，不是 checkpoint / persistence / recovery ABI

因此：

1. 不允许用 `CheckpointReviewSummary` 倒推 `CheckpointPersistenceDescriptor`
2. 不允许用 `PersistenceReviewSummary` 倒推 durable store / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 persistence-facing 第一事实来源

## Persistence Prototype 与 Future Durable Store / Recovery Protocol 的升级关系

V0.12 当前冻结的升级关系是：

1. `CheckpointPersistenceDescriptor`
   - local / offline machine artifact
   - 只回答“当前 persistence handoff 是否已准备好”
2. future durable store / recovery protocol
   - durable / resumable protocol layer
   - 才回答“如何存储、由谁存储、存储句柄是什么、后续如何恢复”

这意味着：

1. future durable store / recovery protocol 可以复用 `CheckpointPersistenceDescriptor` 的 exportable prefix、planned durable identity、basis / blocker
2. future durable store / recovery protocol 不应要求 V0.12 当前就携带 durable checkpoint id、resume token、store metadata
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `CheckpointPersistenceDescriptor` / `PersistenceReviewSummary`

## Persistence-Facing State 对 Review / Recovery 的影响

V0.12 当前建议：

1. persistence descriptor 必须区分“上游 checkpoint result failure”和“persistence-facing inconsistency / bootstrap error”
2. persistence review summary 必须区分“当前可生成 local persistence handoff”与“当前已经是 durable recovery ABI”
3. future persistence guidance 可以讨论哪些字段可被 durable store / recovery protocol 继承，但不应直接把 resume token / store metadata 塞回当前 artifact

换句话说：

1. exportable prefix / planned durable identity / persistence basis 先属于 persistence descriptor
2. reviewer-facing export preview / next-step guidance 先属于 persistence review summary
3. durable checkpoint id / recovery handle / crash recovery 先不属于当前 artifact

## Future Persistence Prototype 当前应怎么消费

V0.12 future persistence prototype 当前只建议依赖：

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
7. `CheckpointPersistenceDescriptor`
   - 提供 exportable prefix、planned durable identity 与 persistence basis

当前不建议 future persistence prototype 直接依赖：

1. `CheckpointReviewSummary`
   - 它仍是 reviewer-facing readable projection
2. `SchedulerDecisionSummary`
   - 它仍是 reviewer-facing readable projection
3. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
4. AST / raw source / descriptor 文件
5. 真实 host log / provider payload

## 当前实现前的约束

Issue 02-09 开始编码前，后续实现必须遵守：

1. persistence prototype 的第一实现目标是稳定 artifact，不是 durable store 模拟器
2. builder diagnostics、checkpoint result failure 与 persistence-facing inconsistency 必须严格区分
3. persistence descriptor / review summary 不得混入 host telemetry、deployment metadata、store metadata 或 provider payload
4. 若 persistence-facing state 需要改变现有 checkpoint artifact 语义，应通过 artifact versioning 明确迁移
5. CLI 输出路径只负责序列化稳定 artifact，不得在 backend printer 中二次发明 persistence-ready 语义

## 当前状态

截至当前设计：

1. V0.11 已完成 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> checkpoint-review` prototype 闭环
2. V0.12 M0 已冻结 persistence prototype 的 scope、non-goals 与上游输入链
3. 下一阶段实现应先进入 persistence descriptor / planned durable identity 的最小模型与 layering，而不是先做 durable store 假实现或 recovery 占位
