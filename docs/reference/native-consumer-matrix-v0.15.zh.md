# AHFL Native Consumer Matrix V0.15

本文冻结 AHFL V0.15 中 durable-store-import-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceExportManifest`、`StoreImportDescriptor`、`DurableStoreImportRequest`、`DurableStoreImportReviewSummary`、`AuditReport` 与 `DryRunTrace` 各自稳定依赖什么，以及 future real durable store adapter / recovery explorer 当前应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.14.zh.md](./native-consumer-matrix-v0.14.zh.md)
- [durable-store-import-prototype-compatibility-v0.15.zh.md](./durable-store-import-prototype-compatibility-v0.15.zh.md)
- [native-durable-store-import-prototype-bootstrap-v0.15.zh.md](../design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 durable-store-import-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|---------------------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | durable store import request、recovery ABI |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | requested artifact set、durable request identity |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | durable adapter blocker、resume token |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | durable store import state 第一事实来源、recovery ABI |
| Scheduler Snapshot Consumer | scheduler-facing local state 的第一正式输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix、checkpoint-friendly local state | durable request identity、store mutation plan |
| Checkpoint Record Consumer | checkpoint-facing machine artifact 输入 | `emit-checkpoint-record`、`checkpoint_record::CheckpointRecord`、`validate_checkpoint_record(...)` | persistable prefix、checkpoint identity、resume basis / blocker、local-only boundary | durable checkpoint id、resume token、store metadata |
| Persistence Descriptor Consumer | persistence-facing machine artifact 输入 | `emit-persistence-descriptor`、`persistence_descriptor::CheckpointPersistenceDescriptor`、`validate_persistence_descriptor(...)` | planned durable identity、exportable prefix、persistence basis / blocker、source version chain | durable store mutation ABI、store URI、operator payload |
| Export Manifest Consumer | export-package-facing machine artifact 输入 | `emit-export-manifest`、`persistence_export::PersistenceExportManifest`、`validate_persistence_export_manifest(...)` | export package identity、artifact bundle、manifest status、manifest boundary、store-import blocker、source version chain | durable store mutation ABI、object path、operator payload |
| Export Review Consumer | reviewer-facing export package summary 输入 | `emit-export-review`、`persistence_export::PersistenceExportReviewSummary`、`validate_persistence_export_review_summary(...)` | store boundary、import preview、artifact bundle preview、manifest readiness / blocker / next-step summary | 独立 durable store / recovery 状态机、host-side payload |
| Store Import Descriptor Consumer | store-import-facing machine artifact 输入 | `emit-store-import-descriptor`、`store_import::StoreImportDescriptor`、`validate_store_import_descriptor(...)` | store import candidate identity、staging artifact set、descriptor boundary、import readiness、descriptor status、staging blocker、source version chain | durable store import request 第一事实来源、resume token、store URI、import receipt |
| Store Import Review Consumer | reviewer-facing store import staging summary 输入 | `emit-store-import-review`、`store_import::StoreImportReviewSummary`、`validate_store_import_review_summary(...)` | store boundary、staging preview、staging artifact preview、descriptor readiness / blocker / next-step summary | 独立 durable store / recovery 状态机、真实导入命令、host-side payload |
| Durable Store Import Request Consumer | durable adapter-facing machine artifact 输入 | `emit-durable-store-import-request`、`durable_store_import::DurableStoreImportRequest`、`validate_durable_store_import_request(...)` | durable request identity、requested artifact set、request boundary、adapter readiness、adapter blocker、source version chain | 真实 durable store executor、object uploader、database writer、resume token、import receipt |
| Durable Store Import Review Consumer | reviewer-facing durable request summary 输入 | `emit-durable-store-import-review`、`durable_store_import::DurableStoreImportReviewSummary`、`validate_durable_store_import_review_summary(...)` | request preview、requested artifact preview、adapter boundary summary、next-step recommendation、next action | 独立 durable store / recovery 状态机、operator payload、launcher ABI |
| Audit Report Consumer | review / CI / archive 的 aggregate 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、journal failed-event 计数、consistency summary | durable store import state 第一事实来源、store mutation ABI |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 durable store import / recovery ABI |

## 当前共享入口

V0.15 当前 durable-store-import-facing consumer 共享入口是：

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
20. `durable_store_import::build_durable_store_import_request(...)`
21. `durable_store_import::validate_durable_store_import_request(...)`
22. `durable_store_import::build_durable_store_import_review_summary(...)`
23. `durable_store_import::validate_durable_store_import_review_summary(...)`
24. `ahflc emit-execution-plan`
25. `ahflc emit-runtime-session`
26. `ahflc emit-execution-journal`
27. `ahflc emit-replay-view`
28. `ahflc emit-scheduler-snapshot`
29. `ahflc emit-checkpoint-record`
30. `ahflc emit-persistence-descriptor`
31. `ahflc emit-export-manifest`
32. `ahflc emit-store-import-descriptor`
33. `ahflc emit-store-import-review`
34. `ahflc emit-durable-store-import-request`
35. `ahflc emit-durable-store-import-review`

这表示：

1. future real durable store adapter 不应自己重建 session / journal / replay / snapshot / checkpoint / persistence / export / store import / durable request 语义
2. reviewer tooling 不应在 CLI / golden / 外部脚本中自行拼接 requested artifact preview、request preview 或 boundary 结论
3. durable-store-import-facing 扩展应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-store-import-request -> durable-store-import-review` 层次前进，而不是跳层

## 当前扩展模板

当前建议复用下面二十层模板：

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
15. durable-store-import-facing machine state 层
   - `durable_store_import::DurableStoreImportRequest`
16. reviewer-facing durable store import summary 层
   - `durable_store_import::DurableStoreImportReviewSummary`
17. aggregate audit 层
   - `audit_report::AuditReport`
18. direct helper / validator 层
   - `build_durable_store_import_request(...)`
   - `validate_durable_store_import_request(...)`
   - `build_durable_store_import_review_summary(...)`
   - `validate_durable_store_import_review_summary(...)`
19. golden / regression 层
   - `tests/durable_store_import/`
   - `v0.15-durable-store-import-golden`
20. backend / CLI 输出层
   - `emit-durable-store-import-request`
   - `emit-durable-store-import-review`

新增 durable-store-import-facing consumer prototype 时，不应跳过第 2-19 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

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
10. 若要做 machine-facing durable adapter request handoff
   - 再消费 `DurableStoreImportRequest`
11. 若要做 reviewer-facing durable request guidance
   - 最后消费 `DurableStoreImportReviewSummary`

换句话说：

- `StoreImportDescriptor` 适合回答“当前 stable store import handoff 到底长什么样”；
- `DurableStoreImportRequest` 适合回答“future real durable store adapter 当前最小能稳定依赖什么 request contract”；
- `DurableStoreImportReviewSummary` 适合回答“reviewer 现在应该如何理解 request boundary / requested artifact preview / next step”；
- `AuditReport` 仍适合回答“这次运行应如何被 review / CI / archive 归档”；
- `DryRunTrace` 仍适合“看懂本地 mock dry-run 做了什么”，但不是正式 durable store import ABI。

## Future Real Durable Store Adapter 应落在哪里

当前 future real durable store adapter / recovery explorer 的推荐依赖顺序是：

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
10. 若需要 machine-facing durable adapter request
   - 再消费 `DurableStoreImportRequest`
11. 若需要 reviewer-facing guidance
   - 最后消费 `DurableStoreImportReviewSummary`

这意味着：

1. future real durable store adapter 不应直接从 `ReplayView` / `AuditReport` 倒推 durable store import state
2. future real durable store adapter 不应直接从 `DryRunTrace` 推导 requested artifact set、durable request identity 或 adapter blocker
3. `DurableStoreImportReviewSummary` 是 projection，不是 durable store / recovery protocol
4. 若以后需要 durable checkpoint id / resume token / store location / import ABI，应先扩 durable store import compatibility contract，而不是把字段塞进 review 输出

## 当前反模式

当前明确不建议：

1. 跳过 `DurableStoreImportRequest`，直接在 `emit-durable-store-import-review` / 外部脚本里私造 request preview state machine
2. 把 `StoreImportReviewSummary` 当 durable store import request 的第一事实来源
3. 把 `DryRunTrace` 当 durable store import prototype / recovery explorer 的正式第一输入
4. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `DurableStoreImportRequest`，直接从 AST、source、trace 重建 durable store import request identity / requested artifact set / blocker
5. 在 request / review 中塞入 durable checkpoint id、resume token、store URI、object path、database key、credential、host telemetry、provider payload 或 import receipt
6. 在未更新 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 durable-store-import-facing 稳定边界

## 当前状态

截至当前实现：

1. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-store-import-request -> durable-store-import-review`
2. `DurableStoreImportRequest` 已成为 durable request identity / requested artifact set / adapter readiness / blocker 的第一正式事实来源
3. `DurableStoreImportReviewSummary` 已成为 reviewer-facing request preview / requested artifact preview / next-step recommendation 的正式 projection
4. `v0.15-durable-store-import-golden` 与 `ahfl-v0.15` 已作为 V0.15 consumer matrix 的最小 golden / aggregate 回归锚点
