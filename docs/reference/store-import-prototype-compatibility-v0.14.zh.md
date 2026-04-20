# AHFL Store Import Prototype Compatibility Contract V0.14

本文正式冻结 AHFL V0.14 中 `StoreImportDescriptor` 与 `StoreImportReviewSummary` 的 compatibility / versioning contract。它承接 V0.13 已冻结的 `PersistenceExportManifest` / `PersistenceExportReviewSummary` 边界，以及 V0.14 Issue 01-03 已落地的 store import prototype scope、descriptor / review model 边界与 layering 规则，明确 future durable store adapter、store-import-facing explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [export-package-prototype-compatibility-v0.13.zh.md](./export-package-prototype-compatibility-v0.13.zh.md)
- [native-store-import-prototype-bootstrap-v0.14.zh.md](../design/native-store-import-prototype-bootstrap-v0.14.zh.md)
- [issue-backlog-v0.14.zh.md](../plan/issue-backlog-v0.14.zh.md)

## 目标

本文主要回答六个问题：

1. V0.14 store-import-facing artifact 的正式版本入口冻结为什么。
2. `StoreImportDescriptor` 与 `StoreImportReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 store-import-facing 最小稳定输入。
5. future durable store adapter / recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 store import prototype 的稳定依赖方向是：

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
  -> StoreImportReviewSummary
```

其中：

1. `PersistenceExportManifest`
   - export package identity、artifact bundle、manifest readiness / blocker、store-import-adjacent boundary 的第一事实来源
2. `StoreImportDescriptor`
   - store import candidate identity、staging artifact set、import readiness / blocker、adapter-facing boundary 的第一事实来源
3. `StoreImportReviewSummary`
   - reviewer-facing projection，不是 store import state 第一事实来源
4. future durable store adapter / recovery protocol
   - 只能建立在 store import descriptor / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `PersistenceExportReviewSummary` 不得跳过 descriptor 自创私有 store import state machine
2. `StoreImportReviewSummary` 不得跳过 descriptor 自创 durable store / recovery 状态机
3. future prototype 不得回退依赖 AST、trace、host log、checkpoint review、persistence review、export review、scheduler review、audit report 或 provider payload
4. durable checkpoint id、resume token、store URI、object path、database key 与 import receipt 仍不属于当前版本

## Artifact 1: StoreImportDescriptor

### 正式版本入口

`StoreImportDescriptor` 当前唯一稳定版本入口是：

- `format_version = "ahfl.store-import-descriptor.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`
3. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
4. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
5. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
6. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
7. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
8. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
9. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
10. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 store import consumer 的主校验入口
2. source version 字段是 store import descriptor 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 staging artifact set / blocker / candidate identity 字段还在”就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.store-import-descriptor.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor / export package / store import candidate 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status` 与 store-import-facing 顶层状态
3. `export_package_identity`
4. `store_import_candidate_identity`
5. `planned_durable_identity`
6. `descriptor_boundary_kind`
7. `staging_artifact_set.entry_count`
8. `staging_artifact_set.entries`
9. `staging_artifact_set.entries[*].artifact_kind`
10. `staging_artifact_set.entries[*].logical_artifact_name`
11. `staging_artifact_set.entries[*].source_format_version`
12. `staging_artifact_set.entries[*].required_for_import`
13. `import_ready`
14. `next_required_staging_artifact_kind`
15. `descriptor_status`
16. `staging_blocker`
17. source artifact format version 链

当前明确不属于 `ahfl.store-import-descriptor.v1` 稳定承诺的是：

1. 真实 durable checkpoint id
2. recovery handle / resume token / import receipt
3. store URI、object path、database key、replication metadata、GC / retention policy
4. host telemetry、deployment metadata、provider payload、connector credential
5. distributed restart plan、recovery daemon state、crash recovery protocol、operator command ABI 或 transaction commit protocol

### 当前实现入口与同步要求

V0.14 当前已冻结 `StoreImportDescriptor` contract 的实现落点：

1. model / validation / bootstrap
   - `include/ahfl/store_import/descriptor.hpp`
   - `src/store_import/descriptor.cpp`
2. machine-facing JSON 输出
   - `include/ahfl/backends/store_import_descriptor.hpp`
   - `src/backends/store_import_descriptor.cpp`
3. CLI 输出命令
   - `ahflc emit-store-import-descriptor`
4. 回归标签
   - direct model / validation：`v0.14-store-import-descriptor-model`
   - bootstrap：`v0.14-store-import-descriptor-bootstrap`
   - emission / golden：`v0.14-store-import-emission`、`v0.14-store-import-golden`

因此，任何触碰 source version chain、descriptor boundary、staging artifact set、import readiness、descriptor status、next required staging artifact 或 staging blocker 的改动，都应同步更新：

1. model / validator / builder
2. backend 输出
3. CLI regression / golden / CI 标签
4. 本 compatibility 文档
5. roadmap / issue backlog 的当前状态

## Artifact 2: StoreImportReviewSummary

### 正式版本入口

`StoreImportReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.store-import-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v1"`
3. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`

这意味着：

1. `StoreImportReviewSummary` 不是独立于 descriptor 的另一套 durable store taxonomy
2. review consumer 若不支持 `ahfl.store-import-descriptor.v1`，也不应宣称支持 `ahfl.store-import-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.store-import-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor / export package / store import candidate 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status`、`manifest_status` 与 store-import-facing 顶层状态
3. `export_package_identity`
4. `store_import_candidate_identity`
5. `planned_durable_identity`
6. `descriptor_boundary_kind`
7. `staging_artifact_entry_count`
8. `staging_artifact_preview`
9. `import_ready`
10. `next_required_staging_artifact_kind`
11. `descriptor_status`
12. `staging_blocker`
13. `store_boundary_summary`
14. `staging_preview`
15. `next_step_recommendation`
16. `next_action`

当前明确不属于 `ahfl.store-import-review.v1` 稳定承诺的是：

1. 独立 durable store / recovery 状态机
2. durable store mutation plan
3. operator payload、host-side explanation chain
4. 真实导入命令、launcher ABI、store execution payload 或 import receipt

### 当前实现入口与同步要求

V0.14 当前已冻结 `StoreImportReviewSummary` contract 的实现落点：

1. model / validation / projection
   - `include/ahfl/store_import/review.hpp`
   - `src/store_import/review.cpp`
2. reviewer-facing text 输出
   - `include/ahfl/backends/store_import_review.hpp`
   - `src/backends/store_import_review.cpp`
3. CLI 输出命令
   - `ahflc emit-store-import-review`
4. 回归标签
   - direct model / validation：`v0.14-store-import-review-model`
   - emission / golden：`v0.14-store-import-emission`、`v0.14-store-import-golden`

因此，任何触碰 staging artifact preview、store boundary、staging preview、next-step recommendation、next action 或 source descriptor format version 的改动，都应同步更新：

1. model / validator / builder
2. backend 输出
3. CLI regression / golden / CI 标签
4. 本 compatibility 文档
5. roadmap / issue backlog 的当前状态

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.13 export-package-facing artifact
   - 明确只接受 V0.13 已冻结 artifact
2. 若 consumer 需要 V0.14 store-import-facing machine state
   - 显式检查 `ahfl.store-import-descriptor.v1`
3. 若 consumer 需要 reviewer-facing store import summary
   - 同时显式检查 `ahfl.store-import-review.v1` 与 `source_store_import_descriptor_format_version`
4. 不允许通过未知 `descriptor_status` / `staging_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 export review / persistence review / checkpoint review / scheduler review / trace / host log 回填 store-import-facing artifact 缺失语义

换句话说：

1. export-package-facing compatibility 不等于 store-import-facing 自动兼容
2. store import review compatibility 不等于可以绕过 store import descriptor compatibility
3. local store import prototype 不等于当前版本已经承诺 durable store import / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 store-import-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 store import candidate identity、staging artifact set、descriptor boundary、import readiness、staging blocker、staging preview 或 next-step recommendation 的稳定含义
3. 改变 store import review 对 descriptor 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local store import prototype 升级为真实 durable store import / recovery ABI

当前 V0.14 最可能触发 version bump 的变化包括：

1. `StoreImportDescriptor` 新增 durable checkpoint id / resume token / store location / import receipt 语义
2. `StoreImportReviewSummary` 从 projection 改成独立 durable store / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `PersistenceExportManifest` 不再是 store import descriptor 的直接上游 machine artifact

## Non-Breaking Extension 候选

下列变化通常可以作为兼容扩展处理，但仍应补 regression，并确认旧 consumer 可以安全忽略：

1. 在 descriptor / review 中新增可选、可忽略的说明性字段，且不改变现有字段含义
2. 新增 reviewer-facing text 行，但不改变 `format_version`、source version chain、status / next-action 语义
3. 扩展 diagnostics 文本覆盖更多无效输入，但不放宽当前 validation gate
4. 增加 golden 覆盖路径，但不改变已存在 golden 的稳定字段语义

下列变化不应伪装成兼容扩展：

1. 更改 `store_import_candidate_identity` / `planned_durable_identity` 的构造含义
2. 更改 `staging_artifact_preview` / `next_required_staging_artifact_kind` / `staging_blocker` 的边界含义
3. 新增必须由旧 consumer 理解的新 `descriptor_status` 或 `next_action`
4. 让 review 输出成为新的 machine-facing 第一事实来源

## Layering-Sensitive Breaking Change 规则

出现以下任一类 layering 变化时，通常应视为 versioning 事件：

1. 让 `PersistenceExportReviewSummary`、`PersistenceReviewSummary`、`CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 `StoreImportDescriptor` 的正式第一输入
2. 让 `StoreImportDescriptor` 直接承诺 durable store mutation、store import executor、store location、recovery handle、resume token、import receipt 或 operator command ABI
3. 让 `StoreImportReviewSummary` 从 readable projection 变成独立 machine-facing store import / recovery taxonomy
4. 让 `PersistenceExportManifest` 不再承担 store import descriptor 的直接上游职责

## 当前不承诺什么

V0.14 当前仍明确不承诺：

1. store import 由谁执行、何时执行、失败如何重试
2. durable store 里的 object layout、bucket / prefix / database key 长什么样
3. 导入是否产生 import receipt、resume token 或 repair handle
4. 如何把 store import 结果接到 crash recovery / distributed recovery daemon
5. host telemetry、provider payload、deployment metadata、operator console 或 secret material

如果未来需要把上述任一项升级为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `StoreImportDescriptor` / `StoreImportReviewSummary`。

## 当前冻结结论

V0.14 当前冻结结论是：

1. `StoreImportDescriptor` 的正式版本入口为 `ahfl.store-import-descriptor.v1`
2. `StoreImportReviewSummary` 的正式版本入口为 `ahfl.store-import-review.v1`
3. `PersistenceExportManifest` 仍是 store import descriptor 的直接 machine-facing 上游
4. 当前最小稳定输入已限定在 store import candidate identity、staging artifact set、descriptor boundary、import readiness、staging blocker、staging preview、next-step recommendation 与 source version chain，不提前承诺 durable checkpoint id、resume token、store URI、object path、database key 或 import receipt
5. future durable store adapter / reviewer tooling 必须先显式过版本检查，再消费 store-import-facing artifact
