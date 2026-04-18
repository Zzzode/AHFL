# AHFL Replay View Compatibility And Versioning V0.8

本文冻结 `emit-replay-view` / `replay_view::ReplayView` 在 V0.8 阶段的版本化与兼容性契约，作为 future replay verifier、scheduler prototype、audit consumer、golden 与 regression 的共同约束。

关联文档：

- [audit-report-compatibility-v0.8.zh.md](./audit-report-compatibility-v0.8.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./execution-journal-compatibility-v0.7.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](../design/native-replay-audit-bootstrap-v0.8.zh.md)
- [issue-backlog-v0.8.zh.md](../plan/issue-backlog-v0.8.zh.md)

## 目标

本文主要回答五个问题：

1. 当前 replay view 的版本标识是什么，出现在什么位置。
2. 哪些 replay 字段是当前稳定边界。
3. 哪些变化可视为兼容扩展，哪些必须 bump 版本。
4. replay 与 execution plan / runtime session / execution journal / audit report 的兼容层次关系是什么。
5. success-path 向 failure-path 扩展时，哪些变化仍兼容，哪些必须通过版本迁移处理。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.replay-view.v1"`

当前实现中的单一来源位于：

- `include/ahfl/replay_view/replay.hpp`
  - `ahfl::replay_view::kReplayViewFormatVersion`

当前该版本号会出现在：

1. `ReplayView.format_version`
2. `emit-replay-view` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 replay consumer 的主校验入口。
2. replay consumer 不应通过 `nodes[]` 结构或 `replay_status` 文本猜版本。
3. 仓库内所有 replay 输出都应共享同一个 `kReplayViewFormatVersion`。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- deterministic replay state progression 的结构化语义边界

包括：

1. 顶层 `source_execution_plan_format_version`
2. 顶层 `source_runtime_session_format_version`
3. 顶层 `source_execution_journal_format_version`
4. 顶层 `source_package_identity`
5. 顶层 `workflow_canonical_name`
6. 顶层 `session_id`
7. 顶层 `run_id`
8. 顶层 `input_fixture`
9. 顶层 `workflow_status`
10. 顶层 `replay_status`
11. 顶层 `execution_order`
12. `nodes[]`
13. node `node_name`
14. node `target`
15. node `execution_index`
16. node `planned_dependencies`
17. node `satisfied_dependencies`
18. node `saw_node_became_ready`
19. node `saw_node_started`
20. node `saw_node_completed`
21. node `used_mock_selectors`
22. node `final_status`
23. 顶层 `consistency`

当前不冻结为长期 ABI 的内容包括：

1. 原始 journal event payload 的逐条镜像
2. dry-run trace 的 contributor-facing 排版细节
3. host timestamp、duration、worker id、machine id
4. connector secret、endpoint、tenant、region、deployment 或 provider payload
5. 未来 failure-path 下更细粒度的 host-internal scheduler 状态机

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 三个 source format version 字段
3. 顶层 `workflow_canonical_name`
4. 顶层 `session_id`
5. 顶层 `replay_status`
6. 顶层 `execution_order`
7. node progression 三个布尔位
8. node `execution_index`
9. node `used_mock_selectors`
10. 顶层 `consistency`

当前 consumer 不应依赖：

1. replay 会承诺真实 runtime failure taxonomy
2. replay 会回退消费 AST、raw source、project descriptor 或 workspace descriptor
3. replay 的物理 JSON 字段顺序
4. replay 可替代 execution journal 作为长期 event log

换句话说：

- consumer 应把 replay view 当作“稳定 replay projection”，而不是“生产 runtime recovery ABI”。

## 与 execution plan / runtime session / execution journal / audit report 的关系

当前兼容层次是：

1. execution plan
   - planning artifact
2. runtime session
   - final state snapshot artifact
3. execution journal
   - deterministic event sequence artifact
4. replay view
   - journal + session 的一致性投影视图
5. audit report
   - reviewer-facing aggregate artifact

当前契约要求：

1. replay consumer 不应跳过 runtime session / execution journal 自行从 plan 重建状态。
2. audit consumer 若需要 replay 一致性，应优先依赖 replay 的稳定 summary，而不是重新实现 journal -> state projection。
3. replay 不应倒灌真实 host / deployment 语义。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在顶层 object 中新增可选字段，且不改变旧字段含义
2. 在 `nodes[]` 中新增可忽略辅助字段
3. 在 `consistency` 中新增可忽略布尔摘要字段
4. 为当前 success-path 输出补充更明确的文档说明，而不改变旧字段语义

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 仍可通过读取 `format_version` 并忽略未知字段安全工作
4. 文档、golden 与 direct regression 已同步声明新增字段的可忽略性

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变 `replay_status`、`execution_order`、`nodes[]` 或 `consistency` 的结构语义
4. 改变 node progression 三个布尔位的语义
5. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `execution_order` 从 node replay 序列改成 host log offset
2. 把 `used_mock_selectors` 从 deterministic selector 摘要改成真实 provider payload
3. 把 `replay_status = consistent` 的含义改成“仅表示 journal parse 成功”
4. 用新的强制枚举替换现有 `final_status` / `workflow_status` 语义而不保留旧解释

## Success-Path 到 Failure-Path 的演进规则

当前 `ahfl.replay-view.v1` 冻结的是 success-path replay 语义。

当前兼容规则要求：

1. failure-path 的第一落点必须是 `ExecutionJournal` / `RuntimeSession` 的兼容扩展，而不是先改 replay 输出。
2. replay 只有在上游 journal / session 已给出稳定 failure 语义后，才可新增 reviewer-facing failure summary。
3. 若 failure-path 只是新增可选摘要字段，且当前 success-path 字段语义不变，可保持 `ahfl.replay-view.v1`。
4. 若 failure-path 需要改变 `replay_status`、`workflow_status`、node progression 布尔位或 `consistency` 的判定语义，必须 bump `kReplayViewFormatVersion`。

这意味着：

1. 不能为了表达 failure-path 而把 success-path `consistent` 静默改成另一套不兼容状态集合。
2. 不能为了 failure-path 回退引入真实 host log、provider payload 或 deployment telemetry。
3. 不能让 replay consumer 重新扫描 AST / project descriptor 补足 failure 解释。

## 当前稳定性承诺

`emit-replay-view` 当前必须满足：

1. 共享单一 `ahfl::replay_view::kReplayViewFormatVersion`
2. 表达同一个 `replay_view::ReplayView` 结构化语义模型
3. 只消费 execution plan / runtime session / execution journal 的稳定边界
4. 不输出真实 connector、secret、tenant、region、deployment 或 host telemetry

这意味着：

1. 若 replay view 发生 breaking change，必须 bump `kReplayViewFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 replay view 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/replay_view/replay.hpp`、`src/replay_view/`、`src/backends/replay_view.cpp` 与相关 CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/replay/` golden
4. 更新或新增 direct regression 与 CLI regression

若属于 breaking change，还必须额外完成：

1. bump `ahfl::replay_view::kReplayViewFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.replay-view.v1`
2. `emit-replay-view` 已覆盖 single-file / project / workspace golden
3. replay model / validation / emission 已进入 direct regression 与 CLI regression
4. failure-path 仍未正式进入 replay v1 稳定语义，后续扩展需先经过 journal / session compatibility 流程
