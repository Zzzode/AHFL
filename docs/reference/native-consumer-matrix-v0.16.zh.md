# AHFL Native Consumer Matrix V0.16

本文冻结 AHFL V0.16 中 durable-adapter-decision-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceExportManifest`、`StoreImportDescriptor`、`DurableStoreImportRequest`、`DurableStoreImportDecision`、`DurableStoreImportDecisionReviewSummary`、`AuditReport` 与 `DryRunTrace` 各自稳定依赖什么，以及 future real durable store adapter / receipt / recovery explorer 当前应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.15.zh.md](./native-consumer-matrix-v0.15.zh.md)
- [durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md](./durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md)
- [native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md](../design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 durable-adapter-decision-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|---------------------------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | durable decision / receipt / recovery ABI |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | decision outcome、capability gap |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | durable decision 第一事实来源 |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | adapter decision / receipt state machine |
| Scheduler Snapshot Consumer | scheduler-facing local state 的第一正式输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix、checkpoint-friendly local state | durable request / decision identity |
| Checkpoint Record Consumer | checkpoint-facing machine artifact 输入 | `emit-checkpoint-record`、`checkpoint_record::CheckpointRecord`、`validate_checkpoint_record(...)` | persistable prefix、checkpoint identity、resume basis / blocker、local-only boundary | durable checkpoint id、resume token、store metadata |
| Persistence Descriptor Consumer | persistence-facing machine artifact 输入 | `emit-persistence-descriptor`、`persistence_descriptor::CheckpointPersistenceDescriptor`、`validate_persistence_descriptor(...)` | planned durable identity、exportable prefix、persistence basis / blocker、source version chain | store URI、object path、operator payload |
| Export Manifest Consumer | export-package-facing machine artifact 输入 | `emit-export-manifest`、`persistence_export::PersistenceExportManifest`、`validate_persistence_export_manifest(...)` | export package identity、artifact bundle、manifest status、manifest boundary、store-import blocker、source version chain | receipt payload、recovery command ABI |
| Store Import Descriptor Consumer | store-import-facing machine artifact 输入 | `emit-store-import-descriptor`、`store_import::StoreImportDescriptor`、`validate_store_import_descriptor(...)` | store import candidate identity、staging artifact set、descriptor boundary、import readiness、descriptor status、staging blocker、source version chain | durable decision 第一事实来源 |
| Durable Store Import Request Consumer | durable adapter request machine artifact 输入 | `emit-durable-store-import-request`、`durable_store_import::DurableStoreImportRequest`、`validate_durable_store_import_request(...)` | request identity、requested artifact set、request boundary、adapter readiness / blocker、source version chain | 真实 durable store executor / receipt |
| Durable Store Import Decision Consumer | durable adapter decision machine artifact 输入 | `emit-durable-store-import-decision`、`durable_store_import::DurableStoreImportDecision`、`validate_durable_store_import_decision(...)` | decision identity、decision status / outcome、decision boundary、accepted-for-future-execution、capability gap、decision blocker、source version chain | import receipt id、resume token、store mutation ABI |
| Durable Store Import Decision Review Consumer | reviewer-facing durable adapter decision summary 输入 | `emit-durable-store-import-decision-review`、`durable_store_import::DurableStoreImportDecisionReviewSummary`、`validate_durable_store_import_decision_review_summary(...)` | decision preview、adapter contract summary、next action、next-step recommendation、decision blocker projection | 独立 durable adapter / recovery 状态机、operator payload |
| Audit Report Consumer | review / CI / archive 的 aggregate 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、journal failed-event 计数、consistency summary | decision / receipt 第一事实来源 |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 durable adapter decision / receipt ABI |

## 当前共享入口

V0.16 当前 durable-adapter-decision-facing consumer 共享入口是：

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
18. `durable_store_import::build_durable_store_import_request(...)`
19. `durable_store_import::validate_durable_store_import_request(...)`
20. `durable_store_import::build_durable_store_import_decision(...)`
21. `durable_store_import::validate_durable_store_import_decision(...)`
22. `durable_store_import::build_durable_store_import_decision_review_summary(...)`
23. `durable_store_import::validate_durable_store_import_decision_review_summary(...)`
24. `ahflc emit-execution-plan`
25. `ahflc emit-runtime-session`
26. `ahflc emit-execution-journal`
27. `ahflc emit-replay-view`
28. `ahflc emit-scheduler-snapshot`
29. `ahflc emit-checkpoint-record`
30. `ahflc emit-persistence-descriptor`
31. `ahflc emit-export-manifest`
32. `ahflc emit-store-import-descriptor`
33. `ahflc emit-durable-store-import-request`
34. `ahflc emit-durable-store-import-decision`
35. `ahflc emit-durable-store-import-decision-review`

这表示：

1. future real durable store adapter 不应自己重建 session / journal / replay / snapshot / checkpoint / persistence / export / store import / durable request / decision 语义
2. reviewer tooling 不应在 CLI / golden / 外部脚本中自行拼接 decision preview、capability gap 或 next action 语义
3. durable-adapter-decision-facing 扩展应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-request -> durable-decision -> durable-decision-review` 层次前进，而不是跳层

## 当前扩展模板

当前建议复用下面二十二层模板：

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
12. store-import-facing machine state 层
   - `store_import::StoreImportDescriptor`
13. durable-request-facing machine state 层
   - `durable_store_import::DurableStoreImportRequest`
14. durable-decision-facing machine state 层
   - `durable_store_import::DurableStoreImportDecision`
15. reviewer-facing durable decision summary 层
   - `durable_store_import::DurableStoreImportDecisionReviewSummary`
16. aggregate audit 层
   - `audit_report::AuditReport`
17. direct helper / validator 层
   - `build_durable_store_import_decision(...)`
   - `validate_durable_store_import_decision(...)`
   - `build_durable_store_import_decision_review_summary(...)`
   - `validate_durable_store_import_decision_review_summary(...)`
18. single-file golden / regression 层
   - `tests/durable_store_import/ok_workflow_value_flow.with_package.durable-store-import-decision.json`
   - `tests/durable_store_import/ok_workflow_value_flow.with_package.durable-store-import-decision-review`
19. project golden / regression 层
   - `tests/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision.json`
   - `tests/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision-review`
20. workspace golden / regression 层
   - `tests/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision.json`
   - `tests/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision-review`
21. labeled regression 层
   - `v0.16-durable-store-import-decision-model`
   - `v0.16-durable-store-import-decision-bootstrap`
   - `v0.16-durable-store-import-decision-review-model`
   - `v0.16-durable-store-import-decision-emission`
   - `v0.16-durable-store-import-decision-golden`
22. backend / CLI 输出层
   - `emit-durable-store-import-decision`
   - `emit-durable-store-import-decision-review`

新增 durable-adapter-decision-facing consumer prototype 时，不应跳过第 2-21 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

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
11. 若要做 machine-facing durable adapter decision handoff
   - 再消费 `DurableStoreImportDecision`
12. 若要做 reviewer-facing durable decision guidance
   - 最后消费 `DurableStoreImportDecisionReviewSummary`

## Future Real Durable Store Adapter 应落在哪里

当前 future real durable store adapter / receipt / recovery explorer 的推荐依赖顺序是：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointPersistenceDescriptor`
8. `PersistenceExportManifest`
9. `StoreImportDescriptor`
10. `DurableStoreImportRequest`
11. `DurableStoreImportDecision`
12. `DurableStoreImportDecisionReviewSummary`

这意味着：

1. future real durable store adapter 不应直接从 `ReplayView` / `AuditReport` 倒推 durable decision state
2. future real durable store adapter 不应直接从 `DryRunTrace` 推导 capability gap、decision blocker、decision status / outcome
3. `DurableStoreImportDecisionReviewSummary` 是 projection，不是 durable adapter / receipt / recovery protocol
4. 若以后需要 receipt id / resume token / store location / recovery ABI，应先扩 `DurableStoreImportDecision` compatibility contract，而不是先改 review 输出

## 当前反模式

当前明确不建议：

1. 跳过 `DurableStoreImportDecision`，直接在 `emit-durable-store-import-decision-review` 或外部脚本里私造 decision 状态机
2. 把 `DurableStoreImportDecisionReviewSummary` 当 durable adapter decision 的第一事实来源
3. 跳过 `DurableStoreImportRequest`，直接从 `StoreImportDescriptor` 派生 decision identity / decision blocker 并宣称稳定
4. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `DurableStoreImportRequest` / `DurableStoreImportDecision`，直接从 AST、source、trace 或 host log 重建 adapter decision 语义
5. 在 decision / review 中塞入 import receipt id、resume token、store URI、object path、database key、credential、host telemetry 或 provider payload
6. 在未同步 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 durable-adapter-decision-facing 稳定边界

## 当前状态

截至当前实现：

1. V0.16 decision / review 的 model / validation / bootstrap / emission 已全部落地
2. `ahfl-v0.16` 与 `v0.16-durable-store-import-decision-*` 标签已覆盖 direct model、bootstrap、review model、emission 与 golden
3. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-request -> durable-decision -> durable-decision-review`
