# AHFL Persistence Prototype Compatibility Contract V0.12

本文正式冻结 AHFL V0.12 中 `CheckpointPersistenceDescriptor` 与 `PersistenceReviewSummary` 的 compatibility / versioning contract。它承接 V0.11 已冻结的 `CheckpointRecord` / `CheckpointReviewSummary` 边界，以及 V0.12 Issue 01-09 已落地的 persistence prototype scope、descriptor / review model、CLI emission 与 golden regression，明确 future persistence prototype、store-adjacent explorer 与 reviewer tooling 当前可以稳定依赖哪些版本入口、source artifact 关系、最小稳定字段边界与 breaking-change 规则。

关联文档：

- [checkpoint-prototype-compatibility-v0.11.zh.md](./checkpoint-prototype-compatibility-v0.11.zh.md)
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)
- [issue-backlog-v0.12.zh.md](../plan/issue-backlog-v0.12.zh.md)

## 目标

本文主要回答六个问题：

1. V0.12 persistence-facing artifact 的正式版本入口冻结为什么。
2. `CheckpointPersistenceDescriptor` 与 `PersistenceReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些字段已可作为 persistence-facing 最小稳定输入。
5. future durable store / recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 哪些变化最可能触发 breaking change / version bump。

## 当前冻结的分层契约

当前 persistence prototype 的稳定依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord
  -> CheckpointPersistenceDescriptor
  -> PersistenceReviewSummary
```

其中：

1. `CheckpointRecord`
   - persistable prefix、checkpoint identity、resume basis / blocker 的第一事实来源
2. `CheckpointPersistenceDescriptor`
   - planned durable identity、exportable prefix、persistence basis / blocker 的第一事实来源
3. `PersistenceReviewSummary`
   - reviewer-facing projection，不是 persistence state 第一事实来源
4. future durable store / recovery explorer
   - 只能建立在 persistence descriptor / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `PersistenceReviewSummary` 不得跳过 descriptor 自创私有 durable store / recovery 状态机
2. future prototype 不得回退依赖 AST、trace、host log、checkpoint review 或 provider payload
3. durable checkpoint id / resume token / recovery daemon ABI 仍不属于当前版本

## Artifact 1: CheckpointPersistenceDescriptor

### 正式版本入口

`CheckpointPersistenceDescriptor` 当前唯一稳定版本入口是：

- `format_version = "ahfl.persistence-descriptor.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`
3. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
4. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
5. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
6. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
7. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
8. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 persistence consumer 的主校验入口
2. source version 字段是 persistence descriptor 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 exportable prefix / persistence blocker / planned identity 字段还在”就跳过版本校验

### 当前最小稳定字段边界

在 `ahfl.persistence-descriptor.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint 的标识字段
2. `workflow_status`、`checkpoint_status` 与 persistence-facing顶层状态
3. `planned_durable_identity`
4. `export_basis_kind`
5. `cursor.exportable_prefix_size`
6. `cursor.exportable_prefix`
7. `cursor.export_ready`
8. `cursor.next_export_candidate_node_name`
9. `persistence_blocker`
10. source artifact format version 链

当前明确不属于 `ahfl.persistence-descriptor.v1` 稳定承诺的是：

1. 真实 durable checkpoint id
2. recovery handle / resume token
3. store URI、replication metadata、database key、object path 或 GC policy
4. host telemetry、deployment metadata、provider payload
5. distributed restart plan、recovery daemon state 或 crash recovery protocol

## Artifact 2: PersistenceReviewSummary

### 正式版本入口

`PersistenceReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.persistence-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_persistence_descriptor_format_version = "ahfl.persistence-descriptor.v1"`
3. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`

这意味着：

1. `PersistenceReviewSummary` 不是独立于 descriptor 的另一套 persistence taxonomy
2. review consumer 若不支持 `ahfl.persistence-descriptor.v1`，也不应宣称支持 `ahfl.persistence-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前最小稳定字段边界

在 `ahfl.persistence-review.v1` 中，当前冻结为最小稳定输入的是：

1. workflow / session / run / input / checkpoint 的标识字段
2. `workflow_status`、`checkpoint_status` 与 persistence-facing顶层状态
3. `planned_durable_identity`
4. `export_basis_kind`
5. `exportable_prefix_size`
6. `exportable_prefix`
7. `export_ready`
8. `next_export_candidate_node_name`
9. `persistence_blocker`
10. `store_boundary_summary`
11. `export_preview`
12. `next_step_recommendation`
13. `next_action`

当前明确不属于 `ahfl.persistence-review.v1` 稳定承诺的是：

1. 独立 durable store / recovery 状态机
2. durable store mutation plan
3. operator payload、host-side explanation chain
4. 真实恢复命令、launcher ABI 或 store execution payload

## 实现入口与同步要求

V0.12 当前已把 compatibility contract 落到下列实现入口：

1. `CheckpointPersistenceDescriptor`
   - model / validation / bootstrap 入口位于 `include/ahfl/persistence_descriptor/descriptor.hpp` 与 `src/persistence_descriptor/descriptor.cpp`
   - machine-facing JSON 输出入口位于 `include/ahfl/backends/persistence_descriptor.hpp` 与 `src/backends/persistence_descriptor.cpp`
   - CLI 输出命令是 `ahflc emit-persistence-descriptor`
2. `PersistenceReviewSummary`
   - model / validation / projection 入口位于 `include/ahfl/persistence_descriptor/review.hpp` 与 `src/persistence_descriptor/review.cpp`
   - reviewer-facing text 输出入口位于 `include/ahfl/backends/persistence_review.hpp` 与 `src/backends/persistence_review.cpp`
   - CLI 输出命令是 `ahflc emit-persistence-review`
3. 回归标签
   - direct model / validation：`v0.12-persistence-descriptor-model`
   - bootstrap：`v0.12-persistence-descriptor-bootstrap`
   - descriptor emission：`v0.12-persistence-descriptor-emission`
   - review model / validation：`v0.12-persistence-review-model`
   - review emission：`v0.12-persistence-review-emission`
   - golden 总切片：`v0.12-persistence-golden`
   - umbrella：`ahfl-v0.12`

因此，任何触碰 persistence-facing stable fields、source version chain、status mapping、planned durable identity、exportable prefix、persistence blocker、store boundary、export preview 或 next-step guidance 的改动，都应同步更新：

1. model / validator / builder
2. backend 输出
3. CLI regression / golden
4. 本 compatibility 文档
5. roadmap / issue backlog 的当前状态

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.11 checkpoint-facing artifact
   - 明确只接受 V0.11 已冻结 artifact
2. 若 consumer 需要 V0.12 persistence-facing machine state
   - 显式检查 `ahfl.persistence-descriptor.v1`
3. 若 consumer 需要 reviewer-facing persistence summary
   - 同时显式检查 `ahfl.persistence-review.v1` 与 `source_persistence_descriptor_format_version`
4. 不允许通过未知 `persistence_status` / `persistence_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 checkpoint review / scheduler review / trace / host log 回填 persistence-facing artifact 缺失语义

换句话说：

1. checkpoint-facing compatibility 不等于 persistence-facing 自动兼容
2. persistence review compatibility 不等于可以绕过 persistence descriptor compatibility
3. local persistence prototype 不等于当前版本已经承诺 durable store / recovery ABI

## 什么最可能触发 breaking change

出现以下任一类变化时，通常应视为 `format_version` bump 候选：

1. 新增或重定义 persistence-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 planned durable identity、exportable prefix、persistence blocker、store boundary 或 export preview 的稳定含义
3. 改变 persistence review 对 descriptor 的依赖方向
4. 让旧 consumer 无法安全忽略新内容
5. 将当前 local persistence prototype 升级为真实 durable store / recovery ABI

当前 V0.12 最可能触发 version bump 的变化包括：

1. `CheckpointPersistenceDescriptor` 新增 durable checkpoint id / resume token 语义
2. `PersistenceReviewSummary` 从 projection 改成独立 durable store / recovery 状态机
3. 更改 source artifact 版本依赖方向
4. 让 `CheckpointRecord` 不再是 persistence descriptor 的直接上游 machine artifact

## Non-Breaking Extension 候选

下列变化通常可以作为兼容扩展处理，但仍应补 regression，并确认旧 consumer 可以安全忽略：

1. 在 descriptor / review 中新增可选、可忽略的说明性字段，且不改变现有字段含义
2. 新增 reviewer-facing text 行，但不改变 `format_version`、source version chain、status / next-action 语义
3. 扩展 diagnostics 文本覆盖更多无效输入，但不放宽当前 validation gate
4. 增加 golden 覆盖路径，但不改变已存在 golden 的稳定字段语义

下列变化不应伪装成兼容扩展：

1. 更改 `planned_durable_identity` 的构造含义
2. 更改 `exportable_prefix` / `next_export_candidate_node_name` 的边界含义
3. 新增必须由旧 consumer 理解的新 `persistence_status` 或 `next_action`
4. 让 review 输出成为新的 machine-facing 第一事实来源

## Layering-Sensitive Breaking Change 规则

除字段变化外，V0.12 当前还明确把下面几类 layering 变化视为高风险 breaking-change 候选：

1. 让 `CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 persistence descriptor 的正式第一输入
2. 让 `CheckpointPersistenceDescriptor` 直接承诺 durable store mutation、store location、recovery handle、resume token 或 operator command ABI
3. 让 `PersistenceReviewSummary` 从 readable projection 变成独立 machine-facing persistence taxonomy
4. 改写当前依赖方向，使 `CheckpointRecord` 不再是 persistence descriptor 的直接 machine-facing上游

换句话说：

1. review / trace / audit 的可读输出扩张，不等于 persistence ABI 自动兼容扩张
2. durable store / recovery protocol 的设计探索，不等于可以提前把字段塞进当前版本
3. 只要改变“谁是第一事实来源”，通常就应视为 versioning 事件，而不是普通兼容补丁

## 当前状态

截至 Issue 10 完成时，V0.12 当前已冻结的是：

1. `CheckpointPersistenceDescriptor` 的正式版本入口为 `ahfl.persistence-descriptor.v1`
2. `PersistenceReviewSummary` 的正式版本入口为 `ahfl.persistence-review.v1`
3. source artifact 依赖方向已明确收口为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> persistence-review`
4. 当前最小稳定输入已限定在 planned durable identity、exportable prefix、persistence blocker、store boundary、export preview、next-step recommendation 与 source version chain，不提前承诺 durable checkpoint id、resume token 或 store metadata
5. `v0.12-persistence-golden` 已把 single-file success、project failed、workspace partial 的 descriptor / review 输出固定为独立可运行 regression
