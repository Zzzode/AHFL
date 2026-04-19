# AHFL Checkpoint Prototype Compatibility Contract V0.11

本文正式冻结 AHFL V0.11 中 `CheckpointRecord` 与 `CheckpointReviewSummary` 的 compatibility / versioning contract，作为 Issue 10 的逐 artifact 参考基线。它承接 V0.11 Issue 01-09 已落地的 checkpoint prototype、record、review surface 与 golden regression，明确 future persistence prototype、checkpoint review tooling 与 recovery explorer 可以稳定依赖哪些版本入口、source artifact 关系与演进规则。

关联文档：

- [scheduler-prototype-compatibility-v0.10.zh.md](./scheduler-prototype-compatibility-v0.10.zh.md)
- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](../design/native-checkpoint-prototype-bootstrap-v0.11.zh.md)
- [native-consumer-matrix-v0.11.zh.md](./native-consumer-matrix-v0.11.zh.md)
- [contributor-guide-v0.11.zh.md](./contributor-guide-v0.11.zh.md)
- [issue-backlog-v0.11.zh.md](../plan/issue-backlog-v0.11.zh.md)

## 目标

本文主要回答六个问题：

1. V0.11 checkpoint-facing artifact 的正式版本入口是什么。
2. `CheckpointRecord` 与 `CheckpointReviewSummary` 分别稳定承诺哪些 source artifact 关系。
3. consumer 应如何校验版本，而不是靠字段集合猜测语义。
4. 哪些变化属于兼容扩展，哪些变化必须 bump version。
5. future recovery explorer 可以依赖到哪一层，不能跨过哪些边界。
6. 后续改字段时 docs / code / golden / tests 应如何同步。

## 当前冻结的分层契约

当前 checkpoint prototype 的稳定依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord
  -> CheckpointReviewSummary
```

其中：

1. `RuntimeSession`
   - workflow / node 当前状态的第一事实来源
2. `ExecutionJournal`
   - deterministic event ordering / failure progression 的第一事实来源
3. `ReplayView`
   - consistency projection，不是 checkpoint state 第一事实来源
4. `SchedulerSnapshot`
   - ready set / blocked frontier / executed prefix / checkpoint-friendly local state 的第一事实来源
5. `CheckpointRecord`
   - persistable prefix、checkpoint identity、resume basis / blocker 的第一事实来源
6. `CheckpointReviewSummary`
   - reviewer-facing projection，不是 checkpoint state 第一事实来源
7. future recovery explorer
   - 只能建立在 checkpoint record / review 边界之上，不能倒推新的 machine-facing state

这意味着：

1. `CheckpointReviewSummary` 不得跳过 record 自创私有 recovery 状态机
2. CLI / golden / CI 不得绕过 helper / validator 直接拼接 resume preview 结论
3. future prototype 不得回退依赖 AST、trace、host log 或 provider payload
4. durable checkpoint id / resume token / recovery daemon ABI 仍不属于当前版本

## Artifact 1: CheckpointRecord

### 正式版本入口

`CheckpointRecord` 当前唯一稳定版本入口是：

- `format_version = "ahfl.checkpoint-record.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_execution_plan_format_version = "ahfl.execution-plan.v1"`
3. 校验 `source_runtime_session_format_version = "ahfl.runtime-session.v2"`
4. 校验 `source_execution_journal_format_version = "ahfl.execution-journal.v2"`
5. 校验 `source_replay_view_format_version = "ahfl.replay-view.v2"`
6. 校验 `source_scheduler_snapshot_format_version = "ahfl.scheduler-snapshot.v1"`
7. 若 `source_package_identity` 存在，再校验其 `format_version = "ahfl.native-package.v1"`

换句话说：

1. 顶层 `format_version` 是 checkpoint consumer 的主校验入口
2. source version 字段是 checkpoint 对上游 artifact 分层关系的稳定承诺
3. consumer 不应通过“看到 prefix / blocker / resume_ready 字段还在”就跳过版本校验

### 当前稳定字段边界

在 `ahfl.checkpoint-record.v1` 中，当前稳定承诺的是：

1. workflow / session / run / input 的标识字段
2. `workflow_status`、`snapshot_status` 与 `checkpoint_status`
3. `workflow_failure_summary`
4. `execution_order`
5. `basis_kind`
6. `checkpoint_friendly_source`
7. `cursor.persistable_prefix_size`
8. `cursor.persistable_prefix`
9. `cursor.resume_candidate_node_name`
10. `cursor.resume_ready`
11. `resume_blocker`

当前明确不属于 `ahfl.checkpoint-record.v1` 稳定承诺的是：

1. durable checkpoint id
2. recovery handle / resume token
3. store URI、replication metadata、database key 或 object path
4. host telemetry、deployment metadata、provider payload
5. distributed restart plan、recovery daemon state 或 crash recovery protocol

## Artifact 2: CheckpointReviewSummary

### 正式版本入口

`CheckpointReviewSummary` 当前唯一稳定版本入口是：

- `format_version = "ahfl.checkpoint-review.v1"`

consumer 的最小版本检查顺序是：

1. 校验顶层 `format_version`
2. 校验 `source_checkpoint_record_format_version = "ahfl.checkpoint-record.v1"`

这意味着：

1. `CheckpointReviewSummary` 不是独立于 record 的另一套 checkpoint taxonomy
2. review consumer 若不支持 `ahfl.checkpoint-record.v1`，也不应宣称支持 `ahfl.checkpoint-review.v1`
3. future reviewer tooling 不允许通过文本布局或字段组合反推隐含版本

### 当前稳定字段边界

在 `ahfl.checkpoint-review.v1` 中，当前稳定承诺的是：

1. workflow / session / run / input 的标识字段
2. `workflow_status`、`snapshot_status` 与 `checkpoint_status`
3. `workflow_failure_summary`
4. `basis_kind`
5. `checkpoint_friendly_source`
6. `persistable_prefix_size`
7. `persistable_prefix`
8. `resume_candidate_node_name`
9. `resume_ready`
10. `resume_blocker`
11. `terminal_reason`
12. `next_action`
13. `checkpoint_boundary_summary`
14. `resume_preview`
15. `next_step_recommendation`

当前明确不属于 `ahfl.checkpoint-review.v1` 稳定承诺的是：

1. 独立 recovery 状态机
2. durable store mutation plan
3. host-side explanation chain
4. 真实恢复命令、launcher ABI 或 operator payload

## 当前 consumer 迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.10 scheduler-facing artifact
   - 明确只接受 V0.10 已冻结 artifact
2. 若 consumer 需要 V0.11 checkpoint-facing machine state
   - 显式检查 `ahfl.checkpoint-record.v1`
3. 若 consumer 需要 reviewer-facing checkpoint summary
   - 同时显式检查 `ahfl.checkpoint-review.v1` 与 `source_checkpoint_record_format_version`
4. 不允许通过未知 `checkpoint_status` / `resume_blocker` / `next_action` string 继续猜测语义
5. 不允许通过 scheduler review / trace / host log 回填 checkpoint-facing artifact 缺失语义

换句话说：

1. scheduler-facing compatibility 不等于 checkpoint-facing 自动兼容
2. checkpoint review compatibility 不等于可以绕过 checkpoint record compatibility
3. local checkpoint prototype 不等于当前版本已经承诺 durable recovery ABI

## 什么算兼容扩展

在不修改对应 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 新增旧 consumer 可忽略的可选 metadata
2. 为当前字段补充更严格文档说明，而不改变旧字段语义
3. 在不改变 prefix / blocker / boundary / next-step 含义的前提下，新增可忽略辅助字段
4. 补充 direct validation、golden、label 与 contributor guidance，而不改变既有字段契约

这些兼容扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 忽略未知字段后仍可安全工作
4. docs / code / golden / tests 同步落地

## 什么算 breaking change

出现以下任一情况时，必须 bump 对应 artifact 的 `format_version`：

1. 新增或重定义 checkpoint-facing 顶层状态，导致旧状态集合语义被扩张
2. 改变 persistable prefix、resume blocker、checkpoint boundary、resume preview 或 next-step recommendation 的稳定含义
3. 改变 review summary 对 record 的依赖方向
4. 改变 `basis_kind` / `checkpoint_friendly_source` 的稳定语义
5. 让旧 consumer 无法安全忽略新内容
6. 将当前 local checkpoint prototype 升级为 durable checkpoint / resume ABI

当前 V0.11 最可能触发 version bump 的变化包括：

1. `CheckpointRecord` 新增 durable checkpoint id / resume token 语义
2. `CheckpointReviewSummary` 从 projection 改成独立 recovery 状态机
3. 新增必须解释的新 `checkpoint_status` / `next_action` 稳定枚举值
4. 更改 source artifact 版本依赖方向

## 变更同步流程

后续若改 checkpoint-facing 稳定字段，至少必须同步：

1. `include/ahfl/checkpoint_record/record.hpp` 或 `include/ahfl/checkpoint_record/review.hpp`
2. 对应 validator / builder 实现
3. `tests/checkpoint/` golden
4. `tests/checkpoint_record/record.cpp` direct regression
5. `tests/cmake/LabelTests.cmake` 中的 checkpoint 标签切片
6. 本文档与对应 roadmap / backlog 状态

推荐最小检查顺序：

1. 先改模型常量与 validator
2. 再改 builder / backend / CLI
3. 再更新 golden 与 direct tests
4. 最后更新 compatibility / roadmap / backlog

## 当前回归锚点

当前与 compatibility contract 直接绑定的回归包括：

1. `tests/checkpoint_record/record.cpp`
   - record / review 的 direct validation、bootstrap 与 compatibility regression
2. `tests/checkpoint/`
   - record / review 的 6 份 golden
3. `v0.11-checkpoint-record-model`
   - checkpoint record model / validation 切片
4. `v0.11-checkpoint-record-bootstrap`
   - checkpoint record bootstrap 切片
5. `v0.11-checkpoint-review-model`
   - checkpoint review model / validation 切片
6. `v0.11-checkpoint-golden`
   - checkpoint record / review 的 6 条 golden 切片

## 当前状态

截至 Issue 10 完成时：

1. `CheckpointRecord` 的正式版本入口已冻结为 `ahfl.checkpoint-record.v1`
2. `CheckpointReviewSummary` 的正式版本入口已冻结为 `ahfl.checkpoint-review.v1`
3. source artifact 依赖方向已明确冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> checkpoint-review`
4. compatibility 回归已覆盖 record / review 的关键 version gate、bootstrap consistency 与 golden diff 入口
