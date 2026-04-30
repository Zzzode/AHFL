# AHFL Native Consumer Matrix V0.19

本文冻结 AHFL V0.19 中 durable-adapter-receipt-persistence-response-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceExportManifest`、`StoreImportDescriptor`、`Request`、`Decision`、`Receipt`、`PersistenceRequest`、`Response`、`ResponseReviewSummary` 各自稳定依赖什么，以及 future real durable store adapter / receipt persistence response / recovery explorer 当前应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.18.zh.md](./native-consumer-matrix-v0.18.zh.md)
- [durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md](./durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md)
- [native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md](../design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 durable-adapter-receipt-persistence-response-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|---------------------------------------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | durable receipt persistence response / recovery ABI |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | receipt persistence response identity / blocker |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | durable persistence response 第一事实来源 |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | durable adapter persistence response 状态机 |
| Scheduler Snapshot Consumer | scheduler-facing local state 输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix | durable persistence response identity |
| Checkpoint Record Consumer | checkpoint-facing machine artifact 输入 | `emit-checkpoint-record`、`checkpoint_record::CheckpointRecord`、`validate_checkpoint_record(...)` | persistable prefix、checkpoint identity、resume basis / blocker | durable checkpoint id、resume token、store metadata |
| Persistence Descriptor Consumer | persistence-facing machine artifact 输入 | `emit-persistence-descriptor`、`persistence_descriptor::CheckpointPersistenceDescriptor`、`validate_persistence_descriptor(...)` | planned durable identity、exportable prefix、persistence basis / blocker | store URI、object path、operator payload |
| Export Manifest Consumer | export-package-facing machine artifact 输入 | `emit-export-manifest`、`persistence_export::PersistenceExportManifest`、`validate_persistence_export_manifest(...)` | export package identity、artifact bundle、manifest status、manifest boundary | receipt persistence response payload、recovery ABI |
| Store Import Descriptor Consumer | store-import-facing machine artifact 输入 | `emit-store-import-descriptor`、`store_import::StoreImportDescriptor`、`validate_store_import_descriptor(...)` | store import candidate identity、staging artifact set、descriptor status | durable decision / receipt / persistence response 第一事实来源 |
| Durable Store Import Request Consumer | durable adapter request artifact 输入 | `emit-durable-store-import-request`、`durable_store_import::DurableStoreImportRequest`、`validate_durable_store_import_request(...)` | request identity、adapter readiness / blocker、source version chain | 真实 durable store executor / receipt persistence response |
| Durable Store Import Decision Consumer | durable adapter decision artifact 输入 | `emit-durable-store-import-decision`、`durable_store_import::DurableStoreImportDecision`、`validate_durable_store_import_decision(...)` | decision identity、decision status / outcome、capability gap、decision blocker | import receipt id、resume token、store mutation ABI |
| Durable Store Import Receipt Consumer | durable adapter receipt artifact 输入 | `emit-durable-store-import-receipt`、`durable_store_import::DurableStoreImportDecisionReceipt`、`validate_durable_store_import_decision_receipt(...)` | receipt identity、receipt status / outcome、receipt boundary、accepted_for_receipt_archive、receipt blocker | 真实 receipt persistence id、store URI、object path、database key |
| Durable Store Import Receipt Persistence Request Consumer | durable adapter receipt persistence request artifact 输入 | `emit-durable-store-import-receipt-persistence-request`、`durable_store_import::DurableStoreImportDecisionReceiptPersistenceRequest`、`validate_durable_store_import_decision_receipt_persistence_request(...)` | persistence request identity、persistence status / outcome、persistence boundary、accepted_for_receipt_persistence、next required capability、receipt persistence blocker | 真实 import receipt persistence response id、resume token、store URI、object path、database key、credential |
| Durable Store Import Receipt Persistence Response Consumer | durable adapter receipt persistence response artifact 输入 | `emit-durable-store-import-receipt-persistence-response`、`durable_store_import::DurableStoreImportDecisionReceiptPersistenceResponse`、`validate_durable_store_import_decision_receipt_persistence_response(...)` | response identity、response status / outcome、response boundary、acknowledged_for_response、next required capability、response blocker | 真实 import receipt persistence response id、resume token、store URI、object path、database key、credential |
| Durable Store Import Receipt Persistence Response Review Consumer | reviewer-facing durable adapter receipt persistence response summary 输入 | `emit-durable-store-import-receipt-persistence-response-review`、`durable_store_import::DurableStoreImportDecisionReceiptPersistenceResponseReviewSummary`、`validate_durable_store_import_decision_receipt_persistence_response_review_summary(...)` | response preview、adapter response contract summary、next action、next-step recommendation | 独立 durable adapter / recovery 状态机、operator payload、store mutation command |
| Audit Report Consumer | review / CI / archive 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、consistency summary | durable receipt persistence response 第一事实来源 |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 durable adapter decision / receipt / persistence response ABI |

## 当前共享入口

V0.19 当前 durable-adapter-receipt-persistence-response-facing consumer 共享入口是：

1. `durable_store_import::build_durable_store_import_decision(...)`
2. `durable_store_import::validate_durable_store_import_decision(...)`
3. `durable_store_import::build_durable_store_import_decision_receipt(...)`
4. `durable_store_import::validate_durable_store_import_decision_receipt(...)`
5. `durable_store_import::build_durable_store_import_decision_receipt_persistence_request(...)`
6. `durable_store_import::validate_durable_store_import_decision_receipt_persistence_request(...)`
7. `durable_store_import::build_durable_store_import_decision_receipt_persistence_response(...)`
8. `durable_store_import::validate_durable_store_import_decision_receipt_persistence_response(...)`
9. `durable_store_import::build_durable_store_import_decision_receipt_persistence_response_review_summary(...)`
10. `durable_store_import::validate_durable_store_import_decision_receipt_persistence_response_review_summary(...)`
11. `ahflc emit-durable-store-import-decision`
12. `ahflc emit-durable-store-import-receipt`
13. `ahflc emit-durable-store-import-receipt-persistence-request`
14. `ahflc emit-durable-store-import-receipt-persistence-response`
15. `ahflc emit-durable-store-import-receipt-persistence-response-review`

这表示：

1. future real durable store adapter 不应在 CLI / 脚本中私造 persistence response state machine
2. reviewer tooling 不应用 trace / review 文本反推 machine-facing persistence response 语义
3. durable-adapter-receipt-persistence-response-facing 扩展应沿着 `decision -> receipt -> receipt-persistence-request -> receipt-persistence-response -> receipt-persistence-response-review` 层次推进

## 推荐消费顺序

当前推荐依赖顺序是：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointPersistenceDescriptor`
8. `PersistenceExportManifest`
9. `StoreImportDescriptor`
10. `Request`
11. `Decision`
12. `Receipt`
13. `PersistenceRequest`
14. `Response`
15. `ResponseReviewSummary`

## Future Real Durable Store Adapter 应落在哪里

当前 future real durable store adapter / receipt persistence response / recovery explorer 的推荐依赖顺序是：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointPersistenceDescriptor`
8. `PersistenceExportManifest`
9. `StoreImportDescriptor`
10. `Request`
11. `Decision`
12. `Receipt`
13. `PersistenceRequest`
14. `Response`
15. `ResponseReviewSummary`

这意味着：

1. `Response` 是 future real adapter 当前可稳定消费的 machine-facing persistence response 边界
2. `ResponseReviewSummary` 是 projection，不是 durable mutation / recovery ABI
3. import receipt persistence response id、resume token、store URI、object path、database key 仍不属于 V0.19 稳定承诺

## 当前反模式

当前明确不建议：

1. 跳过 `Response`，直接在 review / 外部脚本中私造 persistence response state machine
2. 把 `ResponseReviewSummary` 当 durable receipt persistence response 的第一事实来源
3. 跳过 `PersistenceRequest`，直接从 response review / persistence review / trace 反推 persistence response identity 或 blocker
4. 在 persistence response / review 中塞入 import receipt persistence response id、resume token、store URI、object path、database key、credential、host telemetry 或 provider payload
5. 在未同步 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 durable-adapter-receipt-persistence-response-facing 稳定边界

## 当前状态

截至当前实现：

1. durable receipt persistence response 的 model / validation / bootstrap / emission 已全部落地
2. durable receipt persistence response review 的 model / validation / emission 已全部落地
3. `ahfl-v0.19` 与 `v0.19-durable-store-import-receipt-persistence-response-*` 标签已覆盖 model、bootstrap、review、emission 与 golden
