# AHFL Durable Store Adapter Receipt Persistence Response Prototype Compatibility Contract V0.19

本文正式冻结 AHFL V0.19 中 `Response` 与 `ResponseReviewSummary` 的 compatibility / versioning contract。它承接 V0.18 已冻结的 `PersistenceRequest` / `PersistenceReviewSummary` 边界，以及 V0.19 Issue 01-03 已落地的 durable adapter receipt persistence response prototype scope、persistence response / review model 边界与 layering 规则，明确 future real durable store adapter、receipt persistence response explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md](./durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md)
- [native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md](../design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md)
- [issue-backlog-v0.19.zh.md](../plan/issue-backlog-v0.19.zh.md)

## 目标

本文主要回答六个问题：

1. V0.19 adapter-receipt-persistence-response-facing artifact 的正式版本入口冻结为什么。
2. `Response` 与 `ResponseReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 adapter-receipt-persistence-response-facing 最小稳定输入。
5. future real durable store adapter / receipt persistence response / recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 durable adapter receipt persistence response prototype 的稳定依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord
  -> CheckpointPersistenceDescriptor
  -> PersistenceExportManifest
  -> StoreImportDescriptor
  -> Request
  -> Decision
  -> Receipt
  -> PersistenceRequest
  -> Response
  -> ResponseReviewSummary
```

其中：

1. `PersistenceRequest`
   - adapter receipt persistence 对 receipt 的 machine-facing request、persistence blocker、persistence boundary 的第一事实来源
2. `Response`
   - adapter receipt persistence 对 persistence request 的 machine-facing response、response status / outcome、response boundary、response blocker 的第一事实来源
3. `ResponseReviewSummary`
   - reviewer-facing projection，不是 durable adapter receipt persistence response state 第一事实来源
4. future real durable store adapter / receipt persistence response / recovery protocol
   - 只能建立在 persistence response / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `ResponseReviewSummary` 不得跳过 persistence response 自创私有 adapter receipt persistence response state machine
2. future prototype 不得回退依赖 AST、trace、host log、response review、persistence review、receipt review、decision review、request review、store import review、checkpoint review、export review、audit report 或 provider payload
3. 真实 import receipt persistence response id、resume token、store URI、object path、database key 与 executor payload 仍不属于当前版本

## Artifact 1: Response

### 正式版本入口

`Response` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-decision-receipt-persistence-response.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_durable_store_import_decision_receipt_persistence_request_format_version = "ahfl.durable-store-import-decision-receipt-persistence-request.v1"`
3. 校验 `source_durable_store_import_decision_receipt_format_version = "ahfl.durable-store-import-decision-receipt.v1"`
4. 校验 `source_durable_store_import_decision_format_version = "ahfl.durable-store-import-decision.v1"`
5. 校验 `source_durable_store_import_request_format_version = "ahfl.durable-store-import-request.v1"`
6. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`
7. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`
8. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
9. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
10. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
11. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
12. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
13. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
14. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`

换句话说：

1. 顶层 `format_version` 是 durable adapter receipt persistence response consumer 的主校验入口
2. source version 字段是 durable receipt persistence response 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过"看到 response status / outcome / blocker / identity 字段还在"就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-decision-receipt-persistence-response.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / export package / store import candidate / durable request / durable decision / durable receipt / durable receipt persistence request / durable receipt persistence response 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status`、`descriptor_status`、`request_status`、`decision_status`、`receipt_status`、`persistence_request_status` 与 durable-receipt-persistence-response-facing 顶层状态
3. `durable_store_import_decision_identity`
4. `durable_store_import_receipt_identity`
5. `durable_store_import_receipt_persistence_request_identity`
6. `durable_store_import_receipt_persistence_response_identity`
7. `planned_durable_identity`
8. `receipt_boundary_kind`
9. `receipt_persistence_boundary_kind`
10. `receipt_persistence_response_boundary_kind`
11. `response_status` (Accepted / Blocked / Deferred / Rejected)
12. `response_outcome` (AcceptPersistenceRequest / BlockBlockedRequest / DeferDeferredRequest / RejectFailedRequest)
13. `acknowledged_for_response`
14. `next_required_adapter_capability`
15. `response_blocker`
16. source artifact format version 链

当前明确不属于 `ahfl.durable-store-import-decision-receipt-persistence-response.v1` 稳定承诺的是：

1. 真实 import receipt persistence response id
2. recovery handle / resume token / retry token
3. store URI、object path、database key、replication metadata、GC / retention policy
4. host telemetry、deployment metadata、provider payload、connector credential
5. transaction commit protocol、operator command ABI、distributed restart plan 或 recovery daemon state

## Artifact 2: ResponseReviewSummary

### 正式版本入口

`ResponseReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-decision-receipt-persistence-response-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_durable_store_import_decision_receipt_persistence_response_format_version = "ahfl.durable-store-import-decision-receipt-persistence-response.v1"`
3. 校验 `source_durable_store_import_decision_receipt_persistence_request_format_version = "ahfl.durable-store-import-decision-receipt-persistence-request.v1"`
4. 校验 `source_durable_store_import_decision_receipt_format_version = "ahfl.durable-store-import-decision-receipt.v1"`
5. 校验 `source_durable_store_import_decision_format_version = "ahfl.durable-store-import-decision.v1"`
6. 校验 `source_durable_store_import_request_format_version = "ahfl.durable-store-import-request.v1"`
7. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`

这意味着：

1. `ResponseReviewSummary` 不是独立于 persistence response 的另一套 durable adapter taxonomy
2. review consumer 若不支持 `ahfl.durable-store-import-decision-receipt-persistence-response.v1`，也不应宣称支持 `ahfl.durable-store-import-decision-receipt-persistence-response-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-decision-receipt-persistence-response-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / export package / store import candidate / durable request / durable decision / durable receipt / durable receipt persistence request / durable receipt persistence response 的标识字段
2. `decision_status`、`receipt_status`、`persistence_request_status`、`response_status` 与 durable-adapter-receipt-persistence-response-facing 顶层状态
3. `durable_store_import_decision_identity`
4. `durable_store_import_receipt_identity`
5. `durable_store_import_receipt_persistence_request_identity`
6. `durable_store_import_receipt_persistence_response_identity`
7. `response_boundary_kind`
8. `response_outcome`
9. `acknowledged_for_response`
10. `next_required_adapter_capability`
11. `response_blocker`
12. `response_preview`
13. `adapter_response_contract_summary`
14. `next_step_recommendation`
15. `next_action`

当前明确不属于 `ahfl.durable-store-import-decision-receipt-persistence-response-review.v1` 稳定承诺的是：

1. 独立 durable adapter / recovery 状态机
2. durable mutation execution plan
3. operator payload、host-side explanation chain
4. 真实导入命令、launcher ABI、store execution payload 或 import receipt persistence response payload

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.18 durable-receipt-persistence-facing artifact
   - 明确只接受 V0.18 已冻结 artifact
2. 若 consumer 需要 V0.19 durable adapter receipt persistence response machine state
   - 显式检查 `ahfl.durable-store-import-decision-receipt-persistence-response.v1`
3. 若 consumer 需要 reviewer-facing durable adapter receipt persistence response summary
   - 同时显式检查 `ahfl.durable-store-import-decision-receipt-persistence-response-review.v1` 与 `source_durable_store_import_decision_receipt_persistence_response_format_version`
4. 不允许通过未知 `response_status` / `response_outcome` / `response_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 durable receipt persistence review、durable receipt review、durable decision review、durable request review、store import review、export review、checkpoint review、scheduler review、trace 或 host log 回填 adapter-receipt-persistence-response-facing artifact 缺失语义

换句话说：

1. durable-receipt-persistence-facing compatibility 不等于 durable-receipt-persistence-response-facing 自动兼容
2. durable receipt persistence response review compatibility 不等于可以绕过 durable receipt persistence response compatibility
3. local durable adapter receipt persistence response prototype 不等于当前版本已经承诺真实 durable store adapter / receipt persistence response / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 adapter-receipt-persistence-response-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 durable receipt persistence response identity、response boundary、response outcome、response preview 或 next-step recommendation 的稳定含义
3. 改变 durable receipt persistence response review 对 persistence response 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local durable adapter receipt persistence response prototype 升级为真实 durable store adapter / receipt persistence response / recovery ABI

当前 V0.19 最可能触发 version bump 的变化包括：

1. `Response` 新增真实 import receipt persistence response id / store location / resume token / executor payload 语义
2. `ResponseReviewSummary` 从 projection 改成独立 durable adapter / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `PersistenceRequest` 不再是 durable receipt persistence response 的直接上游 machine artifact

## Docs / Code / Golden / Tests 同步流程

为了避免 compatibility contract 与实现漂移，当前 V0.19 要求以下联动约束：

1. 改 `Response` 的稳定字段或语义时
   - 同步更新 `include/ahfl/durable_store_import/` 与 `src/durable_store_import/` 对应实现
   - 同步更新 `tests/durable_store_import/decision.cpp`
   - 同步更新 `tests/durable_store_import/*.durable-store-import-receipt-persistence-response.json`
   - 同步更新本文对应 artifact 章节
2. 改 `ResponseReviewSummary` 的稳定字段或语义时
   - 同步更新 `include/ahfl/durable_store_import/` 与 `src/durable_store_import/` 对应实现
   - 同步更新 `tests/durable_store_import/decision.cpp`
   - 同步更新 `tests/durable_store_import/*.durable-store-import-receipt-persistence-response-review`
   - 同步更新本文对应 artifact 章节
3. 改 receipt persistence response CLI 输出、golden 路径或标签切片时
   - 同步更新 `src/cli/pipeline_runner.cpp`
   - 同步更新 `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake`
   - 同步更新 `.github/workflows/ci.yml` 的 `ahfl-v0.19` 回归步骤
4. 改 durable-receipt-persistence-response-facing 扩展顺序或 consumer 边界时
   - 同步更新未来 `docs/reference/native-consumer-matrix-v0.19.zh.md`
   - 同步更新未来 `docs/reference/contributor-guide-v0.19.zh.md`
   - 同步更新 `docs/plan/roadmap-v0.19.zh.md` 与 `docs/plan/issue-backlog-v0.19.zh.md`

## 当前冻结结论

截至 V0.19 当前实现：

1. `Response` 的正式版本入口为 `ahfl.durable-store-import-decision-receipt-persistence-response.v1`
2. `ResponseReviewSummary` 的正式版本入口为 `ahfl.durable-store-import-decision-receipt-persistence-response-review.v1`
3. `PersistenceRequest` 仍是 durable adapter receipt persistence response 的直接 machine-facing 上游
4. review 仍然只是 projection，不得承接真实 durable store adapter / receipt persistence response / recovery ABI
5. future durable adapter / reviewer tooling 必须先显式过版本检查，再消费 adapter-receipt-persistence-response-facing artifact
