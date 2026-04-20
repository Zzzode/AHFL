# AHFL Native Store Import Prototype Bootstrap V0.14

本文冻结 AHFL V0.14 中 store import prototype、`StoreImportDescriptor` / staging artifact set，以及 future durable store adapter 可稳定消费的 store-import-facing local state 最小边界。它承接 V0.13 已完成的 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceExportManifest` 与 `PersistenceExportReviewSummary`，目标是把仓库推进到“可稳定表达 store-import-facing machine artifact”的下一层边界，但仍不进入真实 durable store import executor、object storage layout、resume protocol、crash recovery 或 host telemetry 领域。

关联文档：

- [native-export-package-prototype-bootstrap-v0.13.zh.md](./native-export-package-prototype-bootstrap-v0.13.zh.md)
- [roadmap-v0.14.zh.md](../plan/roadmap-v0.14.zh.md)
- [issue-backlog-v0.14.zh.md](../plan/issue-backlog-v0.14.zh.md)

## 目标

本文主要回答六个问题：

1. V0.14 当前到底要交付什么 store import prototype / adapter-facing handoff 能力。
2. `StoreImportDescriptor` 的第一落点在哪里。
3. `PersistenceExportManifest`、store import descriptor、store import review 与 future durable store adapter / recovery ABI 各自如何分层。
4. future durable store adapter 当前可以稳定依赖哪条输入链路。
5. 哪些内容当前只属于 local store import prototype，而不属于真实 durable store / recovery。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 resume token、crash recovery、distributed worker restart、failover orchestration、recovery daemon、transaction commit protocol 或 launcher ABI
3. wall clock metrics、machine id、worker id、host log payload、provider request/response capture、operator console 或 SIEM integration
4. tenant、region、secret、deployment、endpoint、connector SDK integration、store credential、store URI、object path、database key 或 import receipt
5. 真正的 runtime resume executor、repair job、retry、timeout、backoff、cancellation propagation、preemption 或 statement-level interpreter

## 当前稳定输入链

V0.14 当前建议只沿着下面这条链路扩展 store import prototype：

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
  -> StoreImportDescriptor / StagingArtifactSet
  -> StoreImportReviewSummary
```

这表示：

1. store-import-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review、export review 或 host log，而是正式 `StoreImportDescriptor` artifact
2. `PersistenceExportManifest` 仍是 store import candidate identity、artifact bundle basis、manifest readiness 与 store-import-adjacent boundary 的直接 machine-facing 上游
3. store import prototype 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor` 前进，而不是回退扫描 source、descriptor、review 文本或 host log
4. `PersistenceExportReviewSummary` 仍只是 readable projection，不是 store import descriptor ABI 的第一输入
5. future durable store adapter / recovery protocol 只能在 store import descriptor compatibility 冻结后继续规划

## Store Import Prototype 的成功标准

V0.14 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing store import descriptor
2. store import descriptor 能回答“当前是否已形成 stable store-import-facing handoff”
3. store import descriptor 能回答“当前 staging artifact set 中逻辑上包含哪些 adapter-facing artifact entry、还缺什么”
4. store import review 能把 machine-facing store import 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在已有 export-facing artifact 与 `PersistenceExportManifest` 之上，而不是在 CLI / 文本输出层私造状态机

它不表示：

1. 当前仓库已经实现 durable store import pipeline
2. 当前仓库已经支持 crash recovery
3. 当前仓库已经拥有 production resume protocol
4. 当前仓库已经承诺 object storage layout、database key、store URI、import receipt 或 operator command ABI

## Store Import Descriptor 的第一边界

V0.14 当前建议 `StoreImportDescriptor` 至少回答下面五类问题：

1. 当前 store import candidate 是为哪个 package / workflow / session / run / export manifest 生成的
2. 当前 staging artifact set 中应包含哪些 machine-facing / reviewer-facing entry
3. 当前 descriptor 是否 import-ready；若未 ready，staging blocker 是什么
4. 当前 store import boundary 是 local staging only，还是已经具备 stable adapter-consumable shape
5. 当前 descriptor 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. store import candidate identity 与 source export identity 已结构化、可稳定序列化
2. staging artifact set entry 已结构化、可稳定序列化
3. staging blocker 来源于正式 artifact，而不是 review 文本猜测
4. descriptor 只描述“future adapter 当前可稳定消费什么 artifact”，不描述“artifact 最终存到哪里”

当前不要求：

1. durable checkpoint id
2. recovery handle / resume token / import receipt
3. store location / object path / database key / replication metadata
4. host / machine / deployment payload

## Persistence / Export Manifest / Store Import Descriptor / Future Store Adapter 的职责拆分

V0.14 当前冻结的分层关系是：

1. `CheckpointRecord`
   - checkpoint-facing machine artifact
   - 提供 persistable prefix、checkpoint identity、resume basis 与 resume blocker
2. `CheckpointPersistenceDescriptor`
   - persistence-facing machine artifact
   - 提供 planned durable identity、exportable prefix、persistence basis 与 persistence blocker
3. `PersistenceExportManifest`
   - export-package-facing machine artifact
   - 提供 export package identity、artifact bundle、manifest readiness 与 store-import-adjacent boundary
4. `StoreImportDescriptor`
   - store-import-facing machine artifact
   - 提供 store import candidate identity、staging artifact set、import readiness 与 staging blocker
5. `StoreImportReviewSummary`
   - reviewer-facing store import summary
   - 只归档 store import descriptor 的可读视图，不重新创造私有 durable store / recovery 状态机
6. future durable store adapter / recovery protocol
   - 不属于 V0.14 当前阶段
   - 只能在 store import descriptor compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 import-ready 语义，再倒逼 store import descriptor 适配
2. 不允许 future durable store adapter 直接从 export review、persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 store import state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 staging artifact set / blocker 语义

## Layering-Sensitive Compatibility 规则

V0.14 当前还必须额外冻结下面四条 layering-sensitive 规则：

1. `PersistenceExportManifest` 仍然是 store import descriptor 的直接 machine-facing 上游
   - `StoreImportDescriptor` 可以消费 export manifest
   - 但不应回退让 `PersistenceExportReviewSummary` 变成 machine-facing 第一输入
2. `StoreImportReviewSummary` 仍然只是 projection
   - 它可以总结 staging preview / store boundary / next-step recommendation
   - 但不应承担 durable store mutation、resume token 派发、import receipt synthesis 或 recovery command synthesis
3. future durable store adapter / recovery protocol 仍然是下一层
   - 它可以复用 store import descriptor 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、recovery handle、import receipt 或 operator payload
4. `PersistenceExportManifest` 与 `StoreImportDescriptor` 的职责不能合并
   - export manifest 负责 export-package-facing bundle 边界
   - store import descriptor 负责 adapter-facing staging handoff 边界

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `PersistenceExportReviewSummary`、`PersistenceReviewSummary`、`CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 store import descriptor 的第一输入
2. 让 `StoreImportDescriptor` 承担真实 durable store mutation plan、store import executor ABI 或 recovery launcher ABI
3. 让 `StoreImportReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 store-import-facing 第一事实来源

## Store Import Review / Export Review / Persistence Review / Checkpoint Review / Scheduler Review / Audit / Trace 的边界

V0.14 当前明确区分七类可读输出：

1. `SchedulerDecisionSummary`
   - 回答“scheduler 当前怎么看 ready / blocked / next action”
   - 第一输入是 `SchedulerSnapshot`
2. `CheckpointReviewSummary`
   - 回答“checkpoint 当前能否形成 persistable basis、resume 还缺什么”
   - 第一输入是 `CheckpointRecord`
3. `PersistenceReviewSummary`
   - 回答“当前能否形成 stable persistence handoff、planned durable identity 还缺什么”
   - 第一输入是 `CheckpointPersistenceDescriptor`
4. `PersistenceExportReviewSummary`
   - 回答“当前能否形成 stable export package handoff、artifact bundle 还缺什么”
   - 第一输入是 `PersistenceExportManifest`
5. future `StoreImportReviewSummary`
   - 回答“当前能否形成 stable store import handoff、staging artifact set 还缺什么”
   - 第一输入是 `StoreImportDescriptor`
6. `AuditReport`
   - 回答“这次运行最终应如何被 archive / CI / review 归档”
   - 第一输入仍是 runtime-adjacent aggregate，而不是 store import ABI
7. `DryRunTrace`
   - 回答“本地 mock dry-run 做了什么”
   - 仍是 contributor-facing trace，不是 checkpoint / persistence / export / store-import / recovery ABI

因此：

1. 不允许用 `PersistenceExportReviewSummary` 倒推 `StoreImportDescriptor`
2. 不允许用 `StoreImportReviewSummary` 倒推 durable store adapter / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 store-import-facing 第一事实来源

## V0.14 Scope 冻结结论

V0.14 当前范围只包括：

1. local / offline store import descriptor 的 scope 与术语冻结
2. future durable store adapter 的最小 store-import-facing 输入边界
3. store import descriptor、store import review 与 future store adapter 的职责切分准备

V0.14 当前明确不包括：

1. 真实 durable store import / export executor
2. 真实 object path、store URI、bucket layout、database key、retention policy 或 import receipt
3. 真实 recovery command、launcher ABI、resume token 或 crash recovery protocol
4. host telemetry、provider payload、connector credential、deployment metadata 或 operator payload

## Store Import Prototype 与 Future Durable Store Adapter / Recovery Protocol 的升级关系

V0.14 当前冻结的升级关系是：

1. `StoreImportDescriptor`
   - local / offline machine artifact
   - 只回答“当前 store import handoff 是否已准备好”
2. future durable store adapter / recovery protocol
   - durable / importable protocol layer
   - 才回答“如何存储、由谁导入、导入句柄是什么、后续如何恢复”

这意味着：

1. future durable store adapter / recovery protocol 可以复用 `StoreImportDescriptor` 的 store import candidate identity、staging artifact set、descriptor boundary 与 staging blocker
2. future durable store adapter / recovery protocol 不应要求 V0.14 当前就携带 durable checkpoint id、resume token、store URI、object path、database key 或 import receipt
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `StoreImportDescriptor` / `StoreImportReviewSummary`

## Future Durable Store Adapter 当前应怎么消费

V0.14 future durable store adapter 当前只建议依赖：

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
8. `PersistenceExportManifest`
   - 提供 export package identity、artifact bundle 与 store-import-adjacent handoff 边界
9. future `StoreImportDescriptor`
   - 提供 adapter-facing staging artifact set、import readiness 与 staging blocker

当前不建议 future durable store adapter 直接依赖：

1. `PersistenceExportReviewSummary`
   - 它仍是 reviewer-facing readable projection
2. `PersistenceReviewSummary`
   - 它仍是 reviewer-facing readable projection
3. `CheckpointReviewSummary`
   - 它仍是 reviewer-facing readable projection
4. `SchedulerDecisionSummary`
   - 它仍是 reviewer-facing readable projection
5. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
6. AST / raw source / descriptor 文件
7. 真实 host log / provider payload

