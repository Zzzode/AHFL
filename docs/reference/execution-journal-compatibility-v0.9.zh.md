# AHFL Execution Journal Compatibility And Versioning V0.9

本文冻结 `emit-execution-journal` / `execution_journal::ExecutionJournal` 在 V0.9 阶段的版本化与兼容性契约，作为 future replay、audit、scheduler prototype、golden 与 regression 的共同约束。

关联文档：

- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [issue-backlog-v0.9.zh.md](../plan/issue-backlog-v0.9.zh.md)

## 目标

本文主要回答五个问题：

1. 当前 execution journal 的稳定版本标识是什么，出现在什么位置。
2. V0.7/V0.8 success-path baseline 与 V0.9 failure-path baseline 的关系是什么。
3. 哪些 journal 字段是当前稳定边界。
4. 哪些变化可视为兼容扩展，哪些必须 bump 版本。
5. journal 与 runtime session / replay / audit / dry-run trace 的兼容层次关系是什么。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.execution-journal.v2"`

当前实现中的单一来源位于：

- `include/ahfl/execution_journal/journal.hpp`
  - `ahfl::execution_journal::kExecutionJournalFormatVersion`

当前该版本号会出现在：

1. `ExecutionJournal.format_version`
2. `emit-execution-journal` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 journal consumer 的主校验入口。
2. journal consumer 不应通过 event kind 集合猜版本。
3. journal 输出与 `kExecutionJournalFormatVersion` 必须保持单一来源。

## V0.8 与 V0.9 的关系

历史基线是：

1. `ahfl.execution-journal.v1`
   - 冻结 success-path event family
2. `ahfl.execution-journal.v2`
   - 冻结 V0.9 failure-aware journal event family

这意味着：

1. `v1` 仍表示 historical success baseline。
2. 需要消费 `mock_missing`、`node_failed`、`workflow_failed` 的 consumer，必须显式接受 `v2`。
3. 不允许通过“未知 event kind 先忽略再猜语义”的方式伪兼容。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- deterministic failure-aware execution event sequence 的结构化语义边界

包括：

1. 顶层 `source_runtime_session_format_version`
2. 顶层 `source_execution_plan_format_version`
3. 顶层 `source_package_identity`
4. 顶层 `workflow_canonical_name`
5. 顶层 `session_id`
6. 顶层 `run_id`
7. 顶层 `events[]`
8. event `kind`
9. event `node_name`
10. event `execution_index`
11. event `failure_summary`
12. event `satisfied_dependencies`
13. event `used_mock_selectors`

当前稳定事件族包括：

1. `session_started`
2. `node_became_ready`
3. `node_started`
4. `node_completed`
5. `mock_missing`
6. `node_failed`
7. `workflow_completed`
8. `workflow_failed`

当前不冻结为长期 ABI 的内容包括：

1. monotonic timestamp、duration 或 host metrics
2. provider request / response payload
3. host machine id、worker id、pid、tid
4. connector secret、endpoint、tenant、region 或 deployment 信息

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 顶层 `source_runtime_session_format_version`
3. 顶层 `workflow_canonical_name`
4. 顶层 `session_id`
5. `events[]`
6. event `kind`
7. event `node_name`
8. event `execution_index`
9. event `failure_summary`
10. event `satisfied_dependencies`
11. event `used_mock_selectors`

当前 consumer 不应依赖：

1. journal 包含真实 provider payload
2. journal 可替代 runtime session 作为完整 state snapshot
3. journal 的物理 JSON 字段顺序
4. journal 会承担全部 contributor-facing review 排版

换句话说：

- consumer 应把 execution journal 当作“稳定 event artifact”，而不是“生产 runtime host log ABI”。

## 与 runtime session / replay / audit / dry-run trace 的关系

当前兼容层次是：

1. runtime session
   - state snapshot artifact
2. execution journal
   - failure-aware event sequence artifact
3. replay view
   - journal + session 的一致性投影视图
4. audit report
   - reviewer-facing aggregate artifact
5. dry-run trace
   - contributor-facing review artifact，不是 journal 替代品

当前契约要求：

1. execution journal consumer 若需要 node state，应先回看 runtime session，而不是反推事件。
2. replay / audit 若需要 failure 结论，应先信任 journal + session，而不是从 trace summary 倒推。
3. journal 不应倒灌真实 host / deployment 语义。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在 `events[]` 上新增可忽略辅助字段
3. 在 event 中新增可忽略 metadata 字段
4. 补充更明确的文档说明，而不改变旧字段语义

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 仍可通过读取 `format_version` 和忽略未知字段安全工作
4. 文档、golden 与 regression 已同步声明新增字段的可忽略性

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变 `events[]`、`kind`、`failure_summary`、`execution_index`、`satisfied_dependencies` 或 `used_mock_selectors` 的结构语义
4. 改变 event ordering 规则
5. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `mock_missing` 变成普通 `node_failed` 而不保留可区分语义
2. 把 `workflow_failed` 从 terminal event 改成可重复事件
3. 把 `execution_index` 从 node execution 序号改成 host log offset
4. 把 `used_mock_selectors` 从 deterministic selector 摘要改成真实 payload

## 当前稳定性承诺

`emit-execution-journal` 当前必须满足：

1. 共享单一 `ahfl::execution_journal::kExecutionJournalFormatVersion`
2. 表达同一个 `execution_journal::ExecutionJournal` 结构化语义模型
3. 不要求 consumer 回看 AST、raw source、project descriptor 或 package authoring descriptor 补语义
4. 不输出真实 connector、secret、tenant、region、deployment 或 host payload

这意味着：

1. 若 execution journal 发生 breaking change，必须 bump `kExecutionJournalFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 execution journal 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/execution_journal/journal.hpp`、`src/execution_journal/`、`src/backends/execution_journal.cpp` 与相关 CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/journal/` golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::execution_journal::kExecutionJournalFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.execution-journal.v2`
2. `ahfl.execution-journal.v1` 继续保留为 success-path historical baseline
3. `emit-execution-journal` 已覆盖 success / failed golden
4. execution journal model / bootstrap / emission 已进入 direct regression 与 CLI regression
