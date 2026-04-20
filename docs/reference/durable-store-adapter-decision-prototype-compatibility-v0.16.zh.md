# AHFL Durable Store Adapter Decision Prototype Compatibility Contract V0.16

本文正式冻结 AHFL V0.16 中 `DurableStoreImportDecision` 与 `DurableStoreImportDecisionReviewSummary` 的 compatibility / versioning contract。它承接 V0.15 已冻结的 `DurableStoreImportRequest` / `DurableStoreImportReviewSummary` 边界，以及 V0.16 Issue 01-03 已落地的 durable adapter decision prototype scope、decision / review model 边界与 layering 规则，明确 future real durable store adapter、receipt explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [durable-store-import-prototype-compatibility-v0.15.zh.md](./durable-store-import-prototype-compatibility-v0.15.zh.md)
- [native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md](../design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md)
- [issue-backlog-v0.16.zh.md](../plan/issue-backlog-v0.16.zh.md)

## 目标

本文主要回答六个问题：

1. V0.16 adapter-decision-facing artifact 的正式版本入口冻结为什么。
2. `DurableStoreImportDecision` 与 `DurableStoreImportDecisionReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 adapter-decision-facing 最小稳定输入。
5. future real durable store adapter / receipt / recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 durable adapter decision prototype 的稳定依赖方向是：

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
  -> DurableStoreImportRequest
  -> DurableStoreImportDecision
  -> DurableStoreImportDecisionReviewSummary
```

其中：

1. `DurableStoreImportRequest`
   - adapter-facing request identity、requested artifact set、adapter readiness / blocker 的第一事实来源
2. `DurableStoreImportDecision`
   - adapter contract 对 request 的 machine-facing decision、capability gap、decision boundary 的第一事实来源
3. `DurableStoreImportDecisionReviewSummary`
   - reviewer-facing projection，不是 durable adapter state 第一事实来源
4. future real durable store adapter / receipt / recovery protocol
   - 只能建立在 durable decision / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `DurableStoreImportReviewSummary` 不得跳过 decision 自创私有 adapter decision state machine
2. `DurableStoreImportDecisionReviewSummary` 不得跳过 decision 自创 durable adapter / receipt 状态机
3. future prototype 不得回退依赖 AST、trace、host log、request review、store import review、checkpoint review、export review、audit report 或 provider payload
4. 真实 import receipt、resume token、store URI、object path、database key 与 executor payload 仍不属于当前版本

## Artifact 1: DurableStoreImportDecision

### 正式版本入口

`DurableStoreImportDecision` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-decision.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_durable_store_import_request_format_version = "ahfl.durable-store-import-request.v1"`
3. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`
4. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`
5. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
6. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
7. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
8. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
9. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
10. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
11. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`

换句话说：

1. 顶层 `format_version` 是 durable adapter decision consumer 的主校验入口
2. source version 字段是 durable decision 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 decision outcome / blocker / identity 字段还在”就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-decision.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / export package / store import candidate / durable request / durable decision 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status`、`descriptor_status`、`request_status` 与 durable-decision-facing 顶层状态
3. `durable_store_import_request_identity`
4. `durable_store_import_decision_identity`
5. `planned_durable_identity`
6. `request_boundary_kind`
7. `decision_boundary_kind`
8. `decision_status`
9. `decision_outcome`
10. `accepted_for_future_execution`
11. `next_required_adapter_capability`
12. `decision_blocker`
13. source artifact format version 链

当前明确不属于 `ahfl.durable-store-import-decision.v1` 稳定承诺的是：

1. 真实 import receipt id
2. recovery handle / resume token / retry token
3. store URI、object path、database key、replication metadata、GC / retention policy
4. host telemetry、deployment metadata、provider payload、connector credential
5. transaction commit protocol、operator command ABI、distributed restart plan 或 recovery daemon state

## Artifact 2: DurableStoreImportDecisionReviewSummary

### 正式版本入口

`DurableStoreImportDecisionReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-decision-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_durable_store_import_decision_format_version = "ahfl.durable-store-import-decision.v1"`
3. 校验 `source_durable_store_import_request_format_version = "ahfl.durable-store-import-request.v1"`
4. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`

这意味着：

1. `DurableStoreImportDecisionReviewSummary` 不是独立于 decision 的另一套 durable adapter taxonomy
2. review consumer 若不支持 `ahfl.durable-store-import-decision.v1`，也不应宣称支持 `ahfl.durable-store-import-decision-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-decision-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / export package / store import candidate / durable request / durable decision 的标识字段
2. `request_status`、`decision_status` 与 durable-adapter-decision-facing 顶层状态
3. `durable_store_import_request_identity`
4. `durable_store_import_decision_identity`
5. `decision_boundary_kind`
6. `decision_outcome`
7. `accepted_for_future_execution`
8. `next_required_adapter_capability`
9. `decision_blocker`
10. `decision_preview`
11. `adapter_contract_summary`
12. `next_step_recommendation`
13. `next_action`

当前明确不属于 `ahfl.durable-store-import-decision-review.v1` 稳定承诺的是：

1. 独立 durable adapter / recovery 状态机
2. durable mutation execution plan
3. operator payload、host-side explanation chain
4. 真实导入命令、launcher ABI、store execution payload 或 import receipt

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.15 durable-request-facing artifact
   - 明确只接受 V0.15 已冻结 artifact
2. 若 consumer 需要 V0.16 durable adapter decision machine state
   - 显式检查 `ahfl.durable-store-import-decision.v1`
3. 若 consumer 需要 reviewer-facing durable adapter decision summary
   - 同时显式检查 `ahfl.durable-store-import-decision-review.v1` 与 `source_durable_store_import_decision_format_version`
4. 不允许通过未知 `decision_status` / `decision_outcome` / `decision_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 durable request review、store import review、export review、checkpoint review、scheduler review、trace 或 host log 回填 adapter-decision-facing artifact 缺失语义

换句话说：

1. durable-request-facing compatibility 不等于 durable-decision-facing 自动兼容
2. durable decision review compatibility 不等于可以绕过 durable decision compatibility
3. local durable adapter decision prototype 不等于当前版本已经承诺真实 durable store adapter / receipt / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 adapter-decision-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 durable decision identity、decision boundary、decision outcome、capability gap、decision preview 或 next-step recommendation 的稳定含义
3. 改变 durable decision review 对 decision 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local durable adapter decision prototype 升级为真实 durable store adapter / receipt / recovery ABI

当前 V0.16 最可能触发 version bump 的变化包括：

1. `DurableStoreImportDecision` 新增真实 import receipt id / store location / resume token / executor payload 语义
2. `DurableStoreImportDecisionReviewSummary` 从 projection 改成独立 durable adapter / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `DurableStoreImportRequest` 不再是 durable decision 的直接上游 machine artifact

## 当前冻结结论

截至 V0.16 当前 M0：

1. `DurableStoreImportDecision` 的正式版本入口为 `ahfl.durable-store-import-decision.v1`
2. `DurableStoreImportDecisionReviewSummary` 的正式版本入口为 `ahfl.durable-store-import-decision-review.v1`
3. `DurableStoreImportRequest` 仍是 durable adapter decision 的直接 machine-facing 上游
4. review 仍然只是 projection，不得承接真实 durable store adapter / receipt / recovery ABI
5. future durable adapter / reviewer tooling 必须先显式过版本检查，再消费 adapter-decision-facing artifact
