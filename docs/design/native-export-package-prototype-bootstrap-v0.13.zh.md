# AHFL Native Export Package Prototype Bootstrap V0.13

本文冻结 AHFL V0.13 中 export package prototype、`PersistenceExportManifest` / artifact bundle，以及 store-import-adjacent local state 的最小边界。它承接 V0.12 已完成的 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor` 与 `PersistenceReviewSummary`，目标是把仓库推进到“可稳定表达 export-package-facing machine artifact”的下一层边界，但仍不进入真实 durable checkpoint store、object storage layout、resume protocol、crash recovery 或 host telemetry 领域。

关联文档：

- [native-persistence-prototype-bootstrap-v0.12.zh.md](./native-persistence-prototype-bootstrap-v0.12.zh.md)
- [roadmap-v0.13.zh.md](../plan/roadmap-v0.13.zh.md)
- [issue-backlog-v0.13.zh.md](../plan/issue-backlog-v0.13.zh.md)

## 目标

本文主要回答六个问题：

1. V0.13 当前到底要交付什么 export package prototype / export handoff 能力。
2. export manifest 的第一落点在哪里。
3. `CheckpointPersistenceDescriptor`、export manifest、export review 与 future durable store adapter / recovery ABI 各自如何分层。
4. future durable store prototype 可以稳定依赖哪条输入链路。
5. 哪些内容当前只属于 local export package prototype，而不属于 durable store import / recovery。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 resume token、crash recovery、distributed worker restart、failover orchestration、recovery daemon 或 launcher ABI
3. wall clock metrics、machine id、worker id、host log payload、provider request/response capture 或 operator console
4. tenant、region、secret、deployment、endpoint、connector SDK integration、store credential、store URI 或 object path
5. 真正的 runtime resume executor、retry、timeout、backoff、cancellation propagation、preemption 或 statement-level interpreter

## 当前稳定输入链

V0.13 当前建议只沿着下面这条链路扩展 export package prototype：

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
  -> PersistenceExportManifest / ExportArtifactBundle
  -> PersistenceExportReviewSummary
```

这表示：

1. export-package-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review 或 host log，而是正式 `PersistenceExportManifest` artifact
2. `CheckpointPersistenceDescriptor` 仍是 planned durable identity、exportable prefix、persistence basis 与 blocker 的第一事实来源
3. export package prototype 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest` 前进，而不是回退扫描 source、descriptor、review 文本或 host log
4. `PersistenceReviewSummary` 仍只是 readable projection，不是 export manifest ABI 的第一输入
5. future durable store adapter / recovery protocol 只能在 export manifest 边界冻结后继续规划

## Export Package Prototype 的成功标准

V0.13 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing export manifest
2. export manifest 能回答“当前是否可形成稳定 export package handoff”
3. export manifest 能回答“当前 bundle 中逻辑上包含哪些 artifact entry、还缺什么”
4. export review 能把 machine-facing export package 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在已有 persistence-facing artifact 与 `CheckpointPersistenceDescriptor` 之上，而不是在 CLI / 文本输出层私造状态机

它不表示：

1. 当前仓库已经实现 durable store import pipeline
2. 当前仓库已经支持 crash recovery
3. 当前仓库已经拥有 production resume protocol
4. 当前仓库已经承诺 object storage layout、database key、store URI 或 operator command ABI

## Export Manifest 的第一边界

V0.13 当前建议 `PersistenceExportManifest` 至少回答下面五类问题：

1. 当前 export package 是为哪个 package / workflow / session / run / persistence descriptor 生成的
2. 当前逻辑 artifact bundle 中应包含哪些 machine-facing / reviewer-facing entry
3. 当前 export package 是否 ready；若未 ready，store-import blocker 是什么
4. 当前 export package boundary 是 local planning only，还是已经具备 stable store-import-adjacent shape
5. 当前 export manifest 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. artifact bundle entry 已结构化、可稳定序列化
2. export package identity 与 source artifact identity 可被显式校验
3. store-import blocker 来源于正式 artifact，而不是 review 文本猜测
4. manifest 只描述“应导出什么 artifact”，不描述“artifact 最终存到哪里”

当前不要求：

1. durable checkpoint id
2. recovery handle / resume token
3. store location / object path / database key / replication metadata
4. host / machine / deployment payload

## Persistence / Export Manifest / Future Store Adapter 的职责拆分

V0.13 当前冻结的分层关系是：

1. `CheckpointRecord`
   - checkpoint-facing machine artifact
   - 提供 persistable prefix、checkpoint identity、resume basis 与 resume blocker
2. `CheckpointPersistenceDescriptor`
   - persistence-facing machine artifact
   - 提供 planned durable identity、exportable prefix、persistence basis 与 persistence blocker
3. `PersistenceExportManifest`
   - export-package-facing machine artifact
   - 提供 export package identity、artifact bundle、manifest readiness 与 store-import blocker
4. `PersistenceExportReviewSummary`
   - reviewer-facing export summary
   - 只归档 export manifest 的可读视图，不重新创造私有 durable store / recovery 状态机
5. future durable store adapter / recovery protocol
   - 不属于 V0.13 当前阶段
   - 只能在 export manifest compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 store-import-ready 语义，再倒逼 export manifest 适配
2. 不允许 future durable store prototype 直接从 persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 export package state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 artifact bundle / blocker 语义

## Layering-Sensitive Compatibility 规则

V0.13 当前还必须额外冻结下面三条 layering-sensitive 规则：

1. `CheckpointPersistenceDescriptor` 仍然是 export manifest 的直接 machine-facing 上游
   - `PersistenceExportManifest` 可以消费 persistence descriptor
   - 但不应回退让 `PersistenceReviewSummary` 变成 machine-facing 第一输入
2. `PersistenceExportReviewSummary` 仍然只是 projection
   - 它可以总结 import preview / store boundary / next-step recommendation
   - 但不应承担 durable store mutation、resume token 派发、recovery command synthesis
3. future durable store adapter / recovery protocol 仍然是下一层
   - 它可以复用 export manifest 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、recovery handle 或 operator payload

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `PersistenceReviewSummary`、`CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 export manifest 的第一输入
2. 让 `PersistenceExportManifest` 承担真实 durable store mutation plan、store import executor ABI 或 recovery launcher ABI
3. 让 `PersistenceExportReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 export-package-facing 第一事实来源

## Export Review / Persistence Review / Checkpoint Review / Scheduler Review / Audit / Trace 的边界

V0.13 当前明确区分六类可读输出：

1. `SchedulerDecisionSummary`
   - 回答“scheduler 当前怎么看 ready / blocked / next action”
   - 第一输入是 `SchedulerSnapshot`
2. `CheckpointReviewSummary`
   - 回答“checkpoint 当前能否形成 persistable basis、resume 还缺什么”
   - 第一输入是 `CheckpointRecord`
3. `PersistenceReviewSummary`
   - 回答“当前能否形成 stable persistence handoff、planned durable identity 还缺什么”
   - 第一输入是 `CheckpointPersistenceDescriptor`
4. future `PersistenceExportReviewSummary`
   - 回答“当前能否形成 stable export package handoff、artifact bundle 还缺什么”
   - 第一输入是 `PersistenceExportManifest`
5. `AuditReport`
   - 回答“这次运行最终应如何被 archive / CI / review 归档”
   - 第一输入仍是 runtime-adjacent aggregate，而不是 export package ABI
6. `DryRunTrace`
   - 回答“本地 mock dry-run 做了什么”
   - 仍是 contributor-facing trace，不是 checkpoint / persistence / export / recovery ABI

因此：

1. 不允许用 `PersistenceReviewSummary` 倒推 `PersistenceExportManifest`
2. 不允许用 `PersistenceExportReviewSummary` 倒推 durable store adapter / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 export-package-facing 第一事实来源

## V0.13 Scope 冻结结论

V0.13 当前范围只包括：

1. local / offline export package manifest 的 scope 与术语冻结
2. future durable store prototype 的最小 export package 输入边界
3. export manifest、export review 与 future store adapter 的职责切分准备

V0.13 当前明确不包括：

1. 真实 durable store import / export executor
2. 真实 object path、store URI、bucket layout、database key 或 retention policy
3. 真实 recovery command、launcher ABI、resume token 或 crash recovery protocol
4. host telemetry、provider payload、connector credential、deployment metadata 或 operator payload

## Export Package Prototype 与 Future Durable Store Adapter / Recovery Protocol 的升级关系

V0.13 当前冻结的升级关系是：

1. `PersistenceExportManifest`
   - local / offline machine artifact
   - 只回答“当前 export package handoff 是否已准备好”
2. future durable store adapter / recovery protocol
   - durable / importable protocol layer
   - 才回答“如何存储、由谁导入、导入句柄是什么、后续如何恢复”

这意味着：

1. future durable store adapter / recovery protocol 可以复用 `PersistenceExportManifest` 的 export package identity、artifact bundle、manifest boundary 与 blocker
2. future durable store adapter / recovery protocol 不应要求 V0.13 当前就携带 durable checkpoint id、resume token、store URI、object path 或 store metadata
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `PersistenceExportManifest` / `PersistenceExportReviewSummary`

## Future Durable Store Prototype 当前应怎么消费

V0.13 future durable store prototype 当前只建议依赖：

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
   - 提供 planned durable identity、exportable prefix、persistence basis 与 blocker
8. future `PersistenceExportManifest`
   - 提供应导出的 artifact bundle 与 store-import-adjacent handoff 边界

当前不建议 future durable store prototype 直接依赖：

1. `PersistenceReviewSummary`
   - 它仍是 reviewer-facing readable projection
2. `CheckpointReviewSummary`
   - 它仍是 reviewer-facing readable projection
3. `SchedulerDecisionSummary`
   - 它仍是 reviewer-facing readable projection
4. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
5. AST / raw source / descriptor 文件
6. 真实 host log / provider payload
