# AHFL Native Consumer Matrix V0.14

本文冻结 AHFL V0.14 中 store-import-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceExportManifest`、`StoreImportDescriptor`、`StoreImportReviewSummary`、`AuditReport` 与 `DryRunTrace` 各自稳定依赖什么，以及 future durable store adapter 当前应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.13.zh.md](./native-consumer-matrix-v0.13.zh.md)
- [store-import-prototype-compatibility-v0.14.zh.md](./store-import-prototype-compatibility-v0.14.zh.md)
- [native-store-import-prototype-bootstrap-v0.14.zh.md](../design/native-store-import-prototype-bootstrap-v0.14.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 store-import-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|-------------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | persistence blocker、store import ABI |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | staging artifact set、planned durable identity |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | reviewer-facing staging preview、resume token |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | store import state 第一事实来源、durable recovery ABI |
| Scheduler Snapshot Consumer | scheduler-facing local state 的第一正式输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix、checkpoint-friendly local state | store import candidate identity、store mutation plan |
| Checkpoint Record Consumer | checkpoint-facing machine artifact 输入 | `emit-checkpoint-record`、`checkpoint_record::CheckpointRecord`、`validate_checkpoint_record(...)` | persistable prefix、checkpoint identity、resume basis / blocker、local-only boundary | durable checkpoint id、resume token、store metadata |
| Persistence Descriptor Consumer | persistence-facing machine artifact 输入 | `emit-persistence-descriptor`、`persistence_descriptor::CheckpointPersistenceDescriptor`、`validate_persistence_descriptor(...)` | planned durable identity、exportable prefix、persistence basis / blocker、source version chain | durable store mutation ABI、store URI、operator payload |
| Export Manifest Consumer | export-package-facing machine artifact 输入 | `emit-export-manifest`、`persistence_export::PersistenceExportManifest`、`validate_persistence_export_manifest(...)` | export package identity、artifact bundle、manifest status、manifest boundary、store-import blocker、source version chain | durable store mutation ABI、store URI、object path、operator payload |
| Export Review Consumer | reviewer-facing export package summary 输入 | `emit-export-review`、`persistence_export::PersistenceExportReviewSummary`、`validate_persistence_export_review_summary(...)` | store boundary、import preview、artifact bundle preview、manifest readiness / blocker / next-step summary | 独立 durable store / recovery 状态机、host-side payload |
| Store Import Descriptor Consumer | store-import-facing machine artifact 输入 | `emit-store-import-descriptor`、`store_import::StoreImportDescriptor`、`validate_store_import_descriptor(...)` | store import candidate identity、staging artifact set、descriptor boundary、import readiness、descriptor status、staging blocker、source version chain | durable checkpoint id、resume token、store URI、object path、database key、import receipt |
| Store Import Review Consumer | reviewer-facing store import staging summary 输入 | `emit-store-import-review`、`store_import::StoreImportReviewSummary`、`validate_store_import_review_summary(...)` | store boundary、staging preview、staging artifact preview、descriptor readiness / blocker / next-step summary | 独立 durable store / recovery 状态机、真实导入命令、host-side payload |
| Audit Report Consumer | review / CI / archive 的 aggregate 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、journal failed-event 计数、consistency summary | store import state 第一事实来源、store mutation ABI |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 store import / recovery ABI |

## 当前共享入口

V0.14 当前 store-import-facing consumer 共享入口是：

1. `handoff::build_execution_plan(...)`
2. `runtime_session::build_runtime_session(...)`
3. `runtime_session::validate_runtime_session(...)`
4. `execution_journal::build_execution_journal(...)`
5. `execution_journal::validate_execution_journal(...)`
6. `replay_view::build_replay_view(...)`
7. `replay_view::validate_replay_view(...)`
8. `scheduler_snapshot::build_scheduler_snapshot(...)`
9. `scheduler_snapshot::validate_scheduler_snapshot(...)`
10. `checkpoint_record::build_checkpoint_record(...)`
11. `checkpoint_record::validate_checkpoint_record(...)`
12. `persistence_descriptor::build_persistence_descriptor(...)`
13. `persistence_descriptor::validate_persistence_descriptor(...)`
14. `persistence_export::build_persistence_export_manifest(...)`
15. `persistence_export::validate_persistence_export_manifest(...)`
16. `store_import::build_store_import_descriptor(...)`
17. `store_import::validate_store_import_descriptor(...)`
18. `store_import::build_store_import_review_summary(...)`
19. `store_import::validate_store_import_review_summary(...)`
20. `ahflc emit-execution-plan`
21. `ahflc emit-runtime-session`
22. `ahflc emit-execution-journal`
23. `ahflc emit-replay-view`
24. `ahflc emit-scheduler-snapshot`
25. `ahflc emit-checkpoint-record`
26. `ahflc emit-persistence-descriptor`
27. `ahflc emit-export-manifest`
28. `ahflc emit-store-import-descriptor`
29. `ahflc emit-store-import-review`

这表示：

1. future durable store adapter 不应自己重建 session / journal / replay / snapshot / checkpoint / persistence / export / store import 语义
2. reviewer tooling 不应在 CLI / golden / 外部脚本中自行拼接 staging artifact preview、staging preview 或 boundary 结论
3. store-import-facing 扩展应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> store-import-review` 层次前进，而不是跳层

## 当前扩展模板

当前建议复用下面十八层模板：

1. authoring 输入层
   - `ahfl.package.json`
2. handoff 共享模型层
   - `handoff::Package`
3. planning artifact 层
   - `handoff::ExecutionPlan`
4. deterministic input 层
   - `CapabilityMockSet`
   - `DryRunRequest`
5. state snapshot 层
   - `runtime_session::RuntimeSession`
6. event sequence 层
   - `execution_journal::ExecutionJournal`
7. replay projection 层
   - `replay_view::ReplayView`
8. scheduler-facing local state 层
   - `scheduler_snapshot::SchedulerSnapshot`
9. checkpoint-facing machine state 层
   - `checkpoint_record::CheckpointRecord`
10. persistence-facing machine state 层
   - `persistence_descriptor::CheckpointPersistenceDescriptor`
11. export-package-facing machine state 层
   - `persistence_export::PersistenceExportManifest`
12. reviewer-facing export summary 层
   - `persistence_export::PersistenceExportReviewSummary`
13. store-import-facing machine state 层
   - `store_import::StoreImportDescriptor`
14. reviewer-facing store import summary 层
   - `store_import::StoreImportReviewSummary`
15. aggregate audit 层
   - `audit_report::AuditReport`
16. direct helper / validator 层
   - `build_store_import_descriptor(...)`
   - `validate_store_import_descriptor(...)`
   - `build_store_import_review_summary(...)`
   - `validate_store_import_review_summary(...)`
17. golden / regression 层
   - `tests/store_import/`
   - `v0.14-store-import-golden`
18. backend / CLI 输出层
   - `emit-store-import-descriptor`
   - `emit-store-import-review`

新增 store-import-facing consumer prototype 时，不应跳过第 2-17 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

## 推荐消费顺序

当前推荐依赖顺序是：

1. 若要做 planning / dependency reasoning
   - 优先消费 `ExecutionPlan`
2. 若要做 final state / failure snapshot reasoning
   - 优先消费 `RuntimeSession`
3. 若要做 event ordering / failure progression / prefix execution reasoning
   - 优先消费 `ExecutionJournal`
4. 若要做 consistency / replay verification
   - 再消费 `ReplayView`
5. 若要做 ready-set / blocked-frontier / checkpoint-friendly local state
   - 再消费 `SchedulerSnapshot`
6. 若要做 machine-facing persistable basis / resume blocker
   - 再消费 `CheckpointRecord`
7. 若要做 machine-facing planned durable identity / export blocker
   - 再消费 `CheckpointPersistenceDescriptor`
8. 若要做 machine-facing export package handoff
   - 再消费 `PersistenceExportManifest`
9. 若要做 machine-facing store import handoff
   - 再消费 `StoreImportDescriptor`
10. 若要做 reviewer-facing staging guidance
   - 最后消费 `StoreImportReviewSummary`

换句话说：

- `PersistenceExportManifest` 适合回答“当前 export package 的 machine-facing handoff 到底长什么样”；
- `StoreImportDescriptor` 适合回答“当前 stable store import handoff 到底长什么样，future durable store adapter 最小能依赖什么”；
- `StoreImportReviewSummary` 适合回答“reviewer 现在应该如何理解 staging boundary / staging preview / next step”；
- `AuditReport` 仍适合回答“这次运行应如何被 review / CI / archive 归档”；
- `DryRunTrace` 仍适合“看懂本地 mock dry-run 做了什么”，但不是正式 store import ABI。

## Future Durable Store Adapter 应落在哪里

当前 future durable store adapter / recovery explorer 的推荐依赖顺序是：

1. 先消费 `ExecutionPlan`
2. 再消费 `RuntimeSession`
3. 再消费 `ExecutionJournal`
4. 若需要 consistency 视角
   - 再消费 `ReplayView`
5. 若需要 scheduler-facing local state
   - 再消费 `SchedulerSnapshot`
6. 若需要 machine-facing checkpoint basis
   - 再消费 `CheckpointRecord`
7. 若需要 machine-facing persistence handoff
   - 再消费 `CheckpointPersistenceDescriptor`
8. 若需要 machine-facing export package handoff
   - 再消费 `PersistenceExportManifest`
9. 若需要 machine-facing store import handoff
   - 再消费 `StoreImportDescriptor`
10. 若需要 reviewer-facing guidance
   - 最后消费 `StoreImportReviewSummary`

这意味着：

1. future durable store adapter 不应直接从 `ReplayView` / `AuditReport` 倒推 store import state
2. future durable store adapter 不应直接从 `DryRunTrace` 推导 staging artifact set、candidate identity 或 blocker
3. `StoreImportReviewSummary` 是 projection，不是 durable store / recovery protocol
4. 若以后需要 durable checkpoint id / resume token / store location / import ABI，应先扩 store import compatibility contract，而不是把字段塞进 descriptor / review 输出

## 当前反模式

当前明确不建议：

1. 跳过 `StoreImportDescriptor`，直接在 `emit-store-import-review` / 外部脚本里私造 staging preview state machine
2. 把 `DryRunTrace` 当 store import prototype / durable store explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor`，直接从 AST、source、trace 重建 store import candidate identity / staging artifact set / blocker
4. 在 descriptor / review 中塞入 durable checkpoint id、resume token、store URI、object path、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 store-import-facing 稳定边界

## 当前状态

截至当前实现：

1. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> store-import-review`
2. `StoreImportDescriptor` 已成为 store import candidate identity / staging artifact set / import readiness / blocker 的第一正式事实来源
3. `StoreImportReviewSummary` 已成为 reviewer-facing store boundary / staging preview / next-step recommendation 的正式 projection
4. `v0.14-store-import-golden` 已作为 V0.14 consumer matrix 的最小 golden 回归锚点
