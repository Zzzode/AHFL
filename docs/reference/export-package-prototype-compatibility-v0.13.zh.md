# AHFL Export Package Prototype Compatibility Contract V0.13

本文正式冻结 AHFL V0.13 中 `PersistenceExportManifest` 与 `PersistenceExportReviewSummary` 的 compatibility / versioning contract。它承接 V0.12 已冻结的 `CheckpointPersistenceDescriptor` / `PersistenceReviewSummary` 边界，以及 V0.13 Issue 01-09 已落地的 export package prototype scope、manifest / review model、CLI emission 与 golden regression，明确 future durable store prototype、store-import-adjacent explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [persistence-prototype-compatibility-v0.12.zh.md](./persistence-prototype-compatibility-v0.12.zh.md)
- [native-export-package-prototype-bootstrap-v0.13.zh.md](../design/native-export-package-prototype-bootstrap-v0.13.zh.md)
- [issue-backlog-v0.13.zh.md](../plan/issue-backlog-v0.13.zh.md)

## 目标

本文主要回答六个问题：

1. V0.13 export-package-facing artifact 的正式版本入口冻结为什么。
2. `PersistenceExportManifest` 与 `PersistenceExportReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 export-package-facing 最小稳定输入。
5. future durable store prototype / adapter 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 export package prototype 的稳定依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord
  -> CheckpointPersistenceDescriptor
  -> PersistenceExportManifest
  -> PersistenceExportReviewSummary
```

其中：

1. `CheckpointPersistenceDescriptor`
   - planned durable identity、exportable prefix、persistence basis / blocker 的第一事实来源
2. `PersistenceExportManifest`
   - export package identity、artifact bundle、manifest readiness / blocker、store-import-adjacent boundary 的第一事实来源
3. `PersistenceExportReviewSummary`
   - reviewer-facing projection，不是 export package state 第一事实来源
4. future durable store prototype / adapter
   - 只能建立在 export manifest / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `PersistenceReviewSummary` 不得跳过 descriptor 自创私有 export manifest 状态机
2. `PersistenceExportReviewSummary` 不得跳过 manifest 自创 durable store / recovery 状态机
3. future prototype 不得回退依赖 AST、trace、host log、checkpoint review、persistence review、scheduler review、audit report 或 provider payload
4. durable checkpoint id、resume token、store URI、object path 仍不属于当前版本

## Artifact 1: PersistenceExportManifest

### 正式版本入口

`PersistenceExportManifest` 当前唯一稳定版本入口是：

- `format_version = "ahfl.persistence-export-manifest.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
3. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
4. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
5. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
6. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
7. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
8. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
9. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 export package consumer 的主校验入口
2. source version 字段是 export manifest 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 artifact bundle / blocker / package identity 字段还在”就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.persistence-export-manifest.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status` 与 export-package-facing 顶层状态
3. `export_package_identity`
4. `planned_durable_identity`
5. `manifest_boundary_kind`
6. `artifact_bundle.entry_count`
7. `artifact_bundle.entries`
8. `artifact_bundle.entries[*].artifact_kind`
9. `artifact_bundle.entries[*].logical_artifact_name`
10. `artifact_bundle.entries[*].source_format_version`
11. `manifest_ready`
12. `next_required_artifact_kind`
13. `manifest_status`
14. `store_import_blocker`
15. source artifact format version 链

当前明确不属于 `ahfl.persistence-export-manifest.v1` 稳定承诺的是：

1. 真实 durable checkpoint id
2. recovery handle / resume token
3. store URI、object path、database key、replication metadata、GC / retention policy
4. host telemetry、deployment metadata、provider payload、connector credential
5. distributed restart plan、recovery daemon state、crash recovery protocol 或 operator command ABI

### 实现入口与同步要求

V0.13 当前已把 `PersistenceExportManifest` contract 落到下列实现入口：

1. model / validation / bootstrap
   - `include/ahfl/persistence_export/manifest.hpp`
   - `src/persistence_export/manifest.cpp`
2. machine-facing JSON 输出
   - `include/ahfl/backends/persistence_export_manifest.hpp`
   - `src/backends/persistence_export_manifest.cpp`
3. CLI 输出命令
   - `ahflc emit-export-manifest`
4. 回归标签
   - direct model / validation：`v0.13-persistence-export-manifest-model`
   - bootstrap：`v0.13-persistence-export-manifest-bootstrap`
   - emission：`v0.13-persistence-export-manifest-emission`

因此，任何触碰 source version chain、manifest boundary、artifact bundle、manifest readiness、manifest status、next required artifact 或 store-import blocker 的改动，都应同步更新：

1. model / validator / builder
2. backend 输出
3. CLI regression / golden
4. 本 compatibility 文档
5. roadmap / issue backlog 的当前状态

## Artifact 2: PersistenceExportReviewSummary

### 正式版本入口

`PersistenceExportReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.persistence-export-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_export_manifest_format_version = "ahfl.persistence-export-manifest.v1"`
3. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
4. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`

这意味着：

1. `PersistenceExportReviewSummary` 不是独立于 manifest 的另一套 store import taxonomy
2. review consumer 若不支持 `ahfl.persistence-export-manifest.v1`，也不应宣称支持 `ahfl.persistence-export-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.persistence-export-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint / persistence descriptor / export package 的标识字段
2. `workflow_status`、`checkpoint_status`、`persistence_status` 与 export-package-facing 顶层状态
3. `export_package_identity`
4. `planned_durable_identity`
5. `manifest_boundary_kind`
6. `artifact_bundle_entry_count`
7. `artifact_bundle_preview`
8. `manifest_ready`
9. `next_required_artifact_kind`
10. `manifest_status`
11. `store_import_blocker`
12. `store_boundary_summary`
13. `import_preview`
14. `next_step_recommendation`
15. `next_action`

当前明确不属于 `ahfl.persistence-export-review.v1` 稳定承诺的是：

1. 独立 durable store / recovery 状态机
2. durable store mutation plan
3. operator payload、host-side explanation chain
4. 真实导入命令、launcher ABI、store execution payload

### 实现入口与同步要求

V0.13 当前已把 `PersistenceExportReviewSummary` contract 落到下列实现入口：

1. model / validation / projection
   - `include/ahfl/persistence_export/review.hpp`
   - `src/persistence_export/review.cpp`
2. reviewer-facing text 输出
   - `include/ahfl/backends/persistence_export_review.hpp`
   - `src/backends/persistence_export_review.cpp`
3. CLI 输出命令
   - `ahflc emit-export-review`
4. 回归标签
   - direct model / validation：`v0.13-persistence-export-review-model`
   - emission：`v0.13-persistence-export-review-emission`

因此，任何触碰 artifact bundle preview、store boundary、import preview、next-step recommendation、next action 或 source manifest format version 的改动，都应同步更新：

1. model / validator / builder
2. backend 输出
3. CLI regression / golden
4. 本 compatibility 文档
5. roadmap / issue backlog 的当前状态

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.12 persistence-facing artifact
   - 明确只接受 V0.12 已冻结 artifact
2. 若 consumer 需要 V0.13 export-package-facing machine state
   - 显式检查 `ahfl.persistence-export-manifest.v1`
3. 若 consumer 需要 reviewer-facing export package summary
   - 同时显式检查 `ahfl.persistence-export-review.v1` 与 `source_export_manifest_format_version`
4. 不允许通过未知 `manifest_status` / `store_import_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 persistence review / checkpoint review / scheduler review / trace / host log 回填 export-package-facing artifact 缺失语义

换句话说：

1. persistence-facing compatibility 不等于 export-package-facing 自动兼容
2. export review compatibility 不等于可以绕过 export manifest compatibility
3. local export package prototype 不等于当前版本已经承诺 durable store import / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 export-package-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 export package identity、artifact bundle、manifest boundary、manifest readiness、store-import blocker、import preview 或 next-step recommendation 的稳定含义
3. 改变 export review 对 manifest 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local export package prototype 升级为真实 durable store import / recovery ABI

当前 V0.13 最可能触发 version bump 的变化包括：

1. `PersistenceExportManifest` 新增 durable checkpoint id / resume token / store location 语义
2. `PersistenceExportReviewSummary` 从 projection 改成独立 durable store / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `CheckpointPersistenceDescriptor` 不再是 export manifest 的直接上游 machine artifact

## Non-Breaking Extension 候选

下列变化通常可以作为兼容扩展处理，但仍应补 regression，并确认旧 consumer 可以安全忽略：

1. 在 manifest / review 中新增可选、可忽略的说明性字段，且不改变现有字段含义
2. 新增 reviewer-facing text 行，但不改变 `format_version`、source version chain、status / next-action 语义
3. 扩展 diagnostics 文本覆盖更多无效输入，但不放宽当前 validation gate
4. 增加 golden 覆盖路径，但不改变已存在 golden 的稳定字段语义

下列变化不应伪装成兼容扩展：

1. 更改 `export_package_identity` / `planned_durable_identity` 的构造含义
2. 更改 `artifact_bundle_preview` / `next_required_artifact_kind` / `store_import_blocker` 的边界含义
3. 新增必须由旧 consumer 理解的新 `manifest_status` 或 `next_action`
4. 让 review 输出成为新的 machine-facing 第一事实来源

## Layering-Sensitive Breaking Change 规则

除字段变化外，V0.13 当前还明确把下面几类 layering 变化视为高风险 breaking-change 候选：

1. 让 `PersistenceReviewSummary`、`CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 `PersistenceExportManifest` 的正式第一输入
2. 让 `PersistenceExportManifest` 直接承诺 durable store mutation、store import executor、store location、recovery handle、resume token 或 operator command ABI
3. 让 `PersistenceExportReviewSummary` 从 readable projection 变成独立 machine-facing export package / recovery taxonomy
4. 改写当前依赖方向，使 `CheckpointPersistenceDescriptor` 不再是 export manifest 的直接 machine-facing上游

换句话说：

1. review / trace / audit 的可读输出扩张，不等于 export package ABI 自动兼容扩张
2. durable store / recovery protocol 的设计探索，不等于可以提前把字段塞进当前版本
3. 只要改变“谁是第一事实来源”，通常就应视为 versioning 事件，而不是普通兼容补丁

## 当前非目标边界

V0.13 当前明确不通过本 compatibility contract 承诺：

1. artifact 最终落在哪个 object path、bucket、table 或 filesystem path
2. store import 由谁执行、何时执行、失败如何重试
3. recovery token、crash recovery launcher、distributed restart、retention / GC policy
4. host telemetry、connector credential、provider request/response payload

如果未来需要把上述任一项升级为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `PersistenceExportManifest` / `PersistenceExportReviewSummary`。

## 当前状态

截至 Issue 10 完成时，V0.13 当前已冻结的是：

1. `PersistenceExportManifest` 的正式版本入口为 `ahfl.persistence-export-manifest.v1`
2. `PersistenceExportReviewSummary` 的正式版本入口为 `ahfl.persistence-export-review.v1`
3. source artifact 依赖方向已明确收口为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence-descriptor -> export-manifest -> export-review`
4. 当前最小稳定输入已限定在 export package identity、artifact bundle、manifest boundary、manifest readiness、store-import blocker、import preview、next-step recommendation 与 source version chain，不提前承诺 durable checkpoint id、resume token、store URI 或 object path
5. `v0.13-export-golden` 已把 single-file success、project failed、workspace partial 的 manifest / review 输出固定为独立可运行 regression
