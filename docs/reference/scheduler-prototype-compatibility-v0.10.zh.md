# AHFL Scheduler Snapshot And Review Compatibility Contract V0.10

本文正式冻结 AHFL V0.10 中 `SchedulerSnapshot` 与 `SchedulerDecisionSummary` 的 compatibility / versioning contract，作为 Issue 10 的逐 artifact 参考基线。它承接 V0.10 Issue 01-09 已落地的 scheduler prototype、snapshot、review surface 与 golden regression，明确 future prototype consumer、review tooling 与 persistence explorer 可以稳定依赖哪些版本入口、source artifact 关系与演进规则。

关联文档：

- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)
- [issue-backlog-v0.10.zh.md](../plan/issue-backlog-v0.10.zh.md)

## 目标

本文主要回答六个问题：

1. V0.10 scheduler-facing artifact 的正式版本入口是什么。
2. `SchedulerSnapshot` 与 `SchedulerDecisionSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些变化属于兼容扩展，哪些变化必须 bump version。
5. future persistence explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 后续改字段时 docs / code / golden / tests 应如何同步。

## 当前冻结的分层契约

当前 scheduler prototype 的稳定依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot / Cursor
  -> SchedulerDecisionSummary
```

其中：

1. `RuntimeSession`
   - workflow / node 当前状态的第一事实来源
2. `ExecutionJournal`
   - event ordering / failure progression 的第一事实来源
3. `ReplayView`
   - consistency projection，不是 scheduler state 第一事实来源
4. `SchedulerSnapshot / Cursor`
   - ready set / blocked frontier / executed prefix 的第一事实来源
5. `SchedulerDecisionSummary`
   - reviewer-facing projection，不是 scheduler state 第一事实来源
6. future persistence explorer
   - 只能建立在 scheduler snapshot 边界之上

这意味着：

1. `SchedulerDecisionSummary` 不得跳过 snapshot 自创私有状态机
2. CLI / golden / CI 不得绕过 helper / validator 直接拼接 next-step 结论
3. future prototype 不得回退依赖 AST、trace、host log 或 provider payload
4. durable checkpoint / resume token / recovery ABI 仍不属于当前版本

## Artifact 1: SchedulerSnapshot

### 正式版本入口

`SchedulerSnapshot` 当前唯一稳定版本入口是：

- `format_version = "ahfl.scheduler-snapshot.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
3. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
4. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
5. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
6. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 snapshot consumer 的主校验入口
2. source version 字段是 snapshot 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 ready / blocked / cursor 字段还在”就跳过版本校验

### 当前稳定字段边界

在 `ahfl.scheduler-snapshot.v1` 中，当前稳定承诺的是：

1. workflow / session / run / input 的标识字段
2. `workflow_status` 与 `snapshot_status`
3. `workflow_failure_summary`
4. `execution_order`
5. `ready_nodes`
6. `blocked_nodes`
7. `cursor.completed_prefix_size`
8. `cursor.completed_prefix`
9. `cursor.next_candidate_node_name`
10. `cursor.checkpoint_friendly`

当前明确不属于 `ahfl.scheduler-snapshot.v1` 稳定承诺的是：

1. durable checkpoint id
2. resume token / recovery cursor ABI
3. host telemetry / deployment metadata
4. provider request/response payload
5. wall clock / queue / retry / timeout runtime policy

## Artifact 2: SchedulerDecisionSummary

### 正式版本入口

`SchedulerDecisionSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.scheduler-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`

这意味着：

1. `SchedulerDecisionSummary` 不是独立于 snapshot 的另一套 state taxonomy
2. review consumer 若不支持 `ahfl.scheduler-snapshot.v1`，也不应宣称支持 `ahfl.scheduler-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前稳定字段边界

在 `ahfl.scheduler-review.v1` 中，当前稳定承诺的是：

1. workflow / session / run / input 的标识字段
2. `workflow_status` 与 `snapshot_status`
3. `workflow_failure_summary`
4. `completed_prefix_size`
5. `completed_prefix`
6. `next_candidate_node_name`
7. `checkpoint_friendly`
8. `ready_nodes`
9. `blocked_nodes`
10. `terminal_reason`
11. `next_action`
12. `next_step_recommendation`

当前明确不属于 `ahfl.scheduler-review.v1` 稳定承诺的是：

1. host-side explanation chain
2. trace / log 拼接出的私有推理文本
3. persistence planner 的 checkpoint mutation plan
4. provider payload / tenant metadata

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.9 runtime-adjacent artifact
   - 明确只接受 V0.9 已冻结 artifact
2. 若 consumer 需要 V0.10 scheduler-facing state
   - 显式检查 `ahfl.scheduler-snapshot.v1`
3. 若 consumer 需要 reviewer-facing scheduler summary
   - 同时显式检查 `ahfl.scheduler-review.v1` 与 `source_scheduler_snapshot_format_version`
4. 不允许通过未知 `ready / blocked / next_action / terminal_reason` string 继续猜测语义
5. 不允许通过 AST / trace / host log 回填 scheduler-facing artifact 缺失语义

换句话说：

1. runtime-adjacent compatibility 不等于 scheduler-facing 自动兼容
2. scheduler review compatibility 不等于可以绕过 snapshot compatibility
3. persistence-friendly 探索不等于当前版本已经承诺 durable ABI

## 什么算兼容扩展

在不修改对应 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 新增旧 consumer 可忽略的可选 metadata
2. 为当前字段补充更严格文档说明，而不改变旧字段语义
3. 在不改变 ready / blocked / prefix / next-action 含义的前提下，新增可忽略辅助字段
4. 补充 direct validation、golden、label 与 contributor guidance，而不改变既有字段契约

这些兼容扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 忽略未知字段后仍可安全工作
4. docs / code / golden / tests 同步落地

## 什么算 breaking change

出现以下任一情况时，必须 bump 对应 artifact 的 `format_version`：

1. 新增或重定义 scheduler-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 ready set、blocked reason、executed prefix、next candidate、terminal reason 或 next-step summary 的含义
3. 改变 review summary 对 snapshot 的依赖方向
4. 改变 `checkpoint_friendly` 的稳定语义
5. 让旧 consumer 无法安全忽略新内容
6. 将当前 local checkpoint-friendly state 升级为 durable checkpoint / resume ABI

当前 V0.10 最可能触发 version bump 的变化包括：

1. `SchedulerSnapshot` 新增 durable checkpoint / resume token 语义
2. `SchedulerDecisionSummary` 从 projection 改成独立状态机
3. 新增必须解释的新 `snapshot_status` / `next_action` 稳定枚举值
4. 更改 source artifact 版本依赖方向

## 变更同步流程

后续若改 scheduler-facing 稳定字段，至少必须同步：

1. `include/ahfl/scheduler_snapshot/snapshot.hpp` 或 `include/ahfl/scheduler_snapshot/review.hpp`
2. 对应 validator / builder 实现
3. `tests/scheduler/` golden
4. `tests/scheduler_snapshot/snapshot.cpp` direct regression
5. `tests/cmake/LabelTests.cmake` 中的 scheduler 标签切片
6. 本文档与对应 roadmap / backlog 状态

推荐最小检查顺序：

1. 先改模型常量与 validator
2. 再改 builder / backend / CLI
3. 再更新 golden 与 direct tests
4. 最后更新 compatibility / roadmap / backlog

## 当前回归锚点

当前与 compatibility contract 直接绑定的回归包括：

1. `tests/scheduler_snapshot/snapshot.cpp`
   - snapshot / review 的 direct validation、bootstrap 与 compatibility regression
2. `tests/scheduler/`
   - snapshot / review 的 6 份 golden
3. `v0.10-scheduler-compatibility`
   - scheduler-facing compatibility 直测切片
4. `v0.10-scheduler-golden`
   - 6 条 scheduler golden 切片
5. `v0.10-scheduler-regression`
   - scheduler snapshot / review 的 model + bootstrap + golden 总切片

## 当前状态

截至 Issue 10 完成时：

1. `SchedulerSnapshot` 的正式版本入口已冻结为 `ahfl.scheduler-snapshot.v1`
2. `SchedulerDecisionSummary` 的正式版本入口已冻结为 `ahfl.scheduler-review.v1`
3. source artifact 依赖方向已明确冻结为 `plan -> session -> journal -> replay -> snapshot -> review`
4. compatibility 回归已覆盖 snapshot / review 的关键 version gate 与 summary consistency
