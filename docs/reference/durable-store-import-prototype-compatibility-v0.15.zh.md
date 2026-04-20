# AHFL Durable Store Import Prototype Compatibility Contract V0.15

本文正式冻结 AHFL V0.15 中 `DurableStoreImportRequest` 与 `DurableStoreImportReviewSummary` 的 compatibility / versioning contract。它承接 V0.14 已冻结的 `StoreImportDescriptor` / `StoreImportReviewSummary` 边界，以及 V0.15 Issue 01-03 已落地的 durable store import prototype scope、request / review model 边界与 layering 规则，明确 future real durable store adapter、durable-store-import-facing explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [store-import-prototype-compatibility-v0.14.zh.md](./store-import-prototype-compatibility-v0.14.zh.md)
- [native-durable-store-import-prototype-bootstrap-v0.15.zh.md](../design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)
- [issue-backlog-v0.15.zh.md](../plan/issue-backlog-v0.15.zh.md)

## 目标

本文主要回答六个问题：

1. V0.15 durable-store-import-facing artifact 的正式版本入口冻结为什么。
2. `DurableStoreImportRequest` 与 `DurableStoreImportReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 durable-store-import-facing 最小稳定输入。
5. future real durable store adapter / recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 durable store import prototype 的稳定依赖方向是：

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
  -> DurableStoreImportReviewSummary
```

其中：

1. `StoreImportDescriptor`
   - store import candidate identity、staging artifact set、import readiness / blocker、adapter-facing staging boundary 的第一事实来源
2. `DurableStoreImportRequest`
   - durable adapter request identity、requested artifact set、adapter readiness / blocker、adapter-facing request boundary 的第一事实来源
3. `DurableStoreImportReviewSummary`
   - reviewer-facing projection，不是 durable store import state 第一事实来源
4. future real durable store adapter / recovery protocol
   - 只能建立在 durable store import request / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `StoreImportReviewSummary` 不得跳过 request 自创私有 durable store import state machine
2. `DurableStoreImportReviewSummary` 不得跳过 request 自创 durable store / recovery 状态机
3. future prototype 不得回退依赖 AST、trace、host log、checkpoint review、persistence review、export review、store import review、audit report 或 provider payload
4. durable checkpoint id、resume token、store URI、object path、database key 与 import receipt 仍不属于当前版本

## Artifact 1: DurableStoreImportRequest

### 正式版本入口

`DurableStoreImportRequest` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-request.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`
3. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`
4. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
5. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
6. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
7. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
8. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
9. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
10. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
11. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 durable store import consumer 的主校验入口
2. source version 字段是 durable store import request 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 requested artifact set / blocker / request identity 字段还在”就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-request.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor / export package / store import candidate / durable request 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status`、`descriptor_status` 与 durable-store-import-facing 顶层状态
3. `export_package_identity`
4. `store_import_candidate_identity`
5. `durable_store_import_request_identity`
6. `planned_durable_identity`
7. `request_boundary_kind`
8. `requested_artifact_set.entry_count`
9. `requested_artifact_set.entries`
10. `requested_artifact_set.entries[*].artifact_kind`
11. `requested_artifact_set.entries[*].logical_artifact_name`
12. `requested_artifact_set.entries[*].source_format_version`
13. `requested_artifact_set.entries[*].required_for_import`
14. `requested_artifact_set.entries[*].adapter_role`
15. `adapter_ready`
16. `next_required_adapter_artifact_kind`
17. `request_status`
18. `adapter_blocker`
19. source artifact format version 链

当前明确不属于 `ahfl.durable-store-import-request.v1` 稳定承诺的是：

1. 真实 durable checkpoint id
2. recovery handle / resume token / import receipt
3. store URI、object path、database key、replication metadata、GC / retention policy
4. host telemetry、deployment metadata、provider payload、connector credential
5. transaction commit protocol、operator command ABI、distributed restart plan 或 recovery daemon state

## Artifact 2: DurableStoreImportReviewSummary

### 正式版本入口

`DurableStoreImportReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.durable-store-import-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_durable_store_import_request_format_version = "ahfl.durable-store-import-request.v1"`
3. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`
4. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`

这意味着：

1. `DurableStoreImportReviewSummary` 不是独立于 request 的另一套 durable store taxonomy
2. review consumer 若不支持 `ahfl.durable-store-import-request.v1`，也不应宣称支持 `ahfl.durable-store-import-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.durable-store-import-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor / export package / store import candidate / durable request 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status`、`descriptor_status` 与 durable-store-import-facing 顶层状态
3. `export_package_identity`
4. `store_import_candidate_identity`
5. `durable_store_import_request_identity`
6. `planned_durable_identity`
7. `request_boundary_kind`
8. `requested_artifact_entry_count`
9. `requested_artifact_preview`
10. `adapter_ready`
11. `next_required_adapter_artifact_kind`
12. `request_status`
13. `adapter_blocker`
14. `adapter_boundary_summary`
15. `request_preview`
16. `next_step_recommendation`
17. `next_action`

当前明确不属于 `ahfl.durable-store-import-review.v1` 稳定承诺的是：

1. 独立 durable store / recovery 状态机
2. durable mutation plan
3. operator payload、host-side explanation chain
4. 真实导入命令、launcher ABI、store execution payload 或 import receipt

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.14 store-import-facing artifact
   - 明确只接受 V0.14 已冻结 artifact
2. 若 consumer 需要 V0.15 durable-store-import-facing machine state
   - 显式检查 `ahfl.durable-store-import-request.v1`
3. 若 consumer 需要 reviewer-facing durable store import summary
   - 同时显式检查 `ahfl.durable-store-import-review.v1` 与 `source_durable_store_import_request_format_version`
4. 不允许通过未知 `request_status` / `adapter_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 store import review、export review、persistence review、checkpoint review、scheduler review、trace 或 host log 回填 durable-store-import-facing artifact 缺失语义

换句话说：

1. store-import-facing compatibility 不等于 durable-store-import-facing 自动兼容
2. durable store import review compatibility 不等于可以绕过 durable store import request compatibility
3. local durable store import prototype 不等于当前版本已经承诺真实 durable store import / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 durable-store-import-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 durable store import request identity、requested artifact set、request boundary、adapter readiness、adapter blocker、request preview 或 next-step recommendation 的稳定含义
3. 改变 durable store import review 对 request 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local durable store import prototype 升级为真实 durable store import / recovery ABI

当前 V0.15 最可能触发 version bump 的变化包括：

1. `DurableStoreImportRequest` 新增 durable checkpoint id / resume token / store location / import receipt 语义
2. `DurableStoreImportReviewSummary` 从 projection 改成独立 durable store / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `StoreImportDescriptor` 不再是 durable store import request 的直接上游 machine artifact

## 文档、代码与 Golden 同步规则

只要触及 V0.15 durable-store-import-facing 稳定边界，至少要同步检查下列锚点：

1. request model / validation / bootstrap
   - `include/ahfl/durable_store_import/request.hpp`
   - `src/durable_store_import/request.cpp`
   - `tests/durable_store_import/request.cpp`
2. review model / validation / projection
   - `include/ahfl/durable_store_import/review.hpp`
   - `src/durable_store_import/review.cpp`
   - `tests/durable_store_import/request.cpp`
3. CLI / backend 输出
   - `src/cli/ahflc.cpp`
   - `src/backends/durable_store_import_request.cpp`
   - `src/backends/durable_store_import_review.cpp`
4. golden / label / CI
   - `tests/durable_store_import/`
   - `tests/cmake/SingleFileCliTests.cmake`
   - `tests/cmake/ProjectTests.cmake`
   - `tests/cmake/LabelTests.cmake`
   - `.github/workflows/ci.yml`
5. consumer-facing 文档
   - `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md`
   - `docs/reference/native-consumer-matrix-v0.15.zh.md`
   - `docs/reference/contributor-guide-v0.15.zh.md`
   - `docs/plan/roadmap-v0.15.zh.md`
   - `docs/plan/issue-backlog-v0.15.zh.md`

当前推荐的最小回归切片是：

```bash
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-request-model
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-request-bootstrap
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-review-model
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-emission
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-golden
ctest --preset test-dev --output-on-failure -L ahfl-v0.15
```

这表示：

1. 改 request 稳定字段时，必须同步更新 request backend、golden 与 compatibility 字段清单
2. 改 review projection 时，必须同步更新 review backend、golden 与 consumer guidance
3. 改 layering / source version chain 时，必须同步更新 compatibility、consumer matrix 与 contributor guide
4. 改标签切片时，必须同步更新 `LabelTests.cmake` 与 CI 中的 V0.15 显式回归步骤

## Non-Breaking Extension 候选

下列变化通常可以作为兼容扩展处理，但仍应补 regression，并确认旧 consumer 可以安全忽略：

1. 在 request / review 中新增可选、可忽略的说明性字段，且不改变现有字段含义
2. 新增 reviewer-facing text 行，但不改变 `format_version`、source version chain、status / next-action 语义
3. 扩展 diagnostics 文本覆盖更多无效输入，但不放宽当前 validation gate
4. 增加 golden 覆盖路径，但不改变已存在 golden 的稳定字段语义

下列变化不应伪装成兼容扩展：

1. 更改 `durable_store_import_request_identity` / `planned_durable_identity` 的构造含义
2. 更改 `requested_artifact_preview` / `next_required_adapter_artifact_kind` / `adapter_blocker` 的边界含义
3. 新增必须由旧 consumer 理解的新 `request_status` 或 `next_action`

## 当前冻结结论

截至 V0.15 当前 M3：

1. `DurableStoreImportRequest` 的正式版本入口为 `ahfl.durable-store-import-request.v1`
2. `DurableStoreImportReviewSummary` 的正式版本入口为 `ahfl.durable-store-import-review.v1`
3. `StoreImportDescriptor` 仍是 durable store import request 的直接 machine-facing 上游
4. review 仍然只是 projection，不得承接真实 durable store / recovery ABI
5. `emit-durable-store-import-request` 与 `emit-durable-store-import-review` 已成为当前正式 CLI 输出入口
6. `ahfl-v0.15`、`v0.15-durable-store-import-*` 标签已成为当前 durable store import prototype 的最小回归入口
7. future durable store adapter / reviewer tooling 必须先显式过版本检查，再消费 durable-store-import-facing artifact
