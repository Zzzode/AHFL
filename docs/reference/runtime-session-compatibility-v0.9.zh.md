# AHFL Runtime Session Compatibility And Versioning V0.9

本文冻结 `emit-runtime-session` / `runtime_session::RuntimeSession` 在 V0.9 阶段的版本化与兼容性契约，作为 future scheduler prototype、execution journal、replay、audit、golden 与 regression 的共同约束。

关联文档：

- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [issue-backlog-v0.9.zh.md](../plan/issue-backlog-v0.9.zh.md)

## 目标

本文主要回答五个问题：

1. 当前 runtime session 的稳定版本标识是什么，出现在什么位置。
2. V0.8 success-path baseline 与 V0.9 failure-path baseline 的关系是什么。
3. 哪些 session 字段是当前稳定边界。
4. 哪些变化可视为兼容扩展，哪些必须 bump 版本。
5. session 与 execution plan / execution journal / replay / audit 的兼容层次关系是什么。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.runtime-session.v2"`

当前实现中的单一来源位于：

- `include/ahfl/runtime_session/session.hpp`
  - `ahfl::runtime_session::kRuntimeSessionFormatVersion`

当前该版本号会出现在：

1. `RuntimeSession.format_version`
2. `emit-runtime-session` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 session consumer 的主校验入口。
2. 仓库内所有 session 输出都应共享同一个 `kRuntimeSessionFormatVersion`。
3. future consumer 不应通过字段集合或 status 字符串猜版本，而应显式检查 `format_version`。

## V0.8 与 V0.9 的关系

历史基线是：

1. `ahfl.runtime-session.v1`
   - 冻结 V0.7/V0.8 的 success-path-only session 语义
2. `ahfl.runtime-session.v2`
   - 冻结 V0.9 的 partial / failed session 语义

这意味着：

1. `v1` 仍可视为 historical success baseline。
2. 需要消费 `Failed` / `Partial` workflow 或 `Failed` / `Skipped` node 的 consumer，必须显式接受 `v2`。
3. 不允许把 `v2` 的新状态集合伪装成 `v1` 的“可忽略字符串扩展”。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- scheduler-ready runtime session snapshot 的结构化 failure-aware state 语义边界

包括：

1. 顶层 `source_execution_plan_format_version`
2. 顶层 `source_package_identity`
3. 顶层 `workflow_canonical_name`
4. 顶层 `session_id`
5. 顶层 `run_id`
6. 顶层 `workflow_status`
7. 顶层 `failure_summary`
8. 顶层 `input_fixture`
9. 顶层 `options`
10. 顶层 `execution_order`
11. `nodes[]`
12. node `status`
13. node `execution_index`
14. node `failure_summary`
15. node `satisfied_dependencies`
16. node `capability_bindings`
17. node `used_mocks`
18. 顶层 `return_summary`

当前稳定状态集合包括：

1. workflow `Completed / Failed / Partial`
2. node `Blocked / Ready / Completed / Failed / Skipped`
3. `RuntimeFailureSummary.kind = MockMissing / NodeFailed / WorkflowFailed`

当前不冻结为长期 ABI 的内容包括：

1. host worker id、pid、tid
2. wall clock timestamp 或 duration
3. memory / thread / process 指标
4. connector secret、endpoint、tenant、region 或 deployment 信息
5. provider request / response payload

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 顶层 `source_execution_plan_format_version`
3. 顶层 `workflow_canonical_name`
4. 顶层 `session_id`
5. 顶层 `workflow_status`
6. 顶层 `failure_summary`
7. 顶层 `execution_order`
8. node `status`
9. node `execution_index`
10. node `failure_summary`
11. node `satisfied_dependencies`
12. node `used_mocks`
13. 顶层 `return_summary`

当前 consumer 不应依赖：

1. session 可证明真实 provider 可用
2. session 会表达 host metrics 或 deployment 配置
3. session 的物理 JSON 字段顺序
4. session 可替代 execution journal 作为 event log

换句话说：

- consumer 应把 runtime session 当作“稳定 state snapshot”，而不是“生产 runtime host ABI”。

## 与 execution plan / execution journal / replay / audit 的关系

当前兼容层次是：

1. execution plan
   - planning artifact
2. runtime session
   - failure-aware state snapshot artifact
3. execution journal
   - event sequence artifact
4. replay view
   - session + journal consistency projection
5. audit report
   - reviewer-facing aggregate artifact

当前契约要求：

1. runtime session consumer 不应回退读取 AST、raw source、project descriptor 或 package authoring descriptor 补语义。
2. execution journal consumer 若需要 state surface，应先信任 runtime session，而不是反推 trace。
3. replay / audit 不得绕过 `RuntimeSession` 私造 workflow 或 node failure taxonomy。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在 `nodes[]` 上新增可忽略辅助字段
3. 在 `failure_summary` 中新增旧 consumer 可忽略的辅助 metadata
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
3. 改变 `workflow_status`、`failure_summary`、`execution_order`、`nodes[]`、`used_mocks[]` 或 `return_summary` 的结构语义
4. 改变 `RuntimeFailureSummary.kind` 的稳定枚举集合或含义
5. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `Partial` 从“已执行前缀稳定快照”改成“任意未完成中间态”
2. 把 `Skipped` 从“未执行但已确定不会执行”改成另一套不兼容调度语义
3. 把 `used_mocks.result_fixture` 从 deterministic fixture 标识改成真实 provider payload
4. 把 `RuntimeFailureSummary.message` 从稳定摘要改成 host-internal blob

## 当前稳定性承诺

`emit-runtime-session` 当前必须满足：

1. 共享单一 `ahfl::runtime_session::kRuntimeSessionFormatVersion`
2. 表达同一个 `runtime_session::RuntimeSession` 结构化语义模型
3. 不要求 consumer 回看 AST、raw source、project descriptor 或 package authoring descriptor 补语义
4. 不输出真实 connector、secret、tenant、region、deployment 或 provider payload

这意味着：

1. 若 runtime session 发生 breaking change，必须 bump `kRuntimeSessionFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 runtime session 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/runtime_session/session.hpp`、`src/runtime_session/`、`src/backends/runtime_session.cpp` 与相关 CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/session/` golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::runtime_session::kRuntimeSessionFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.runtime-session.v2`
2. `ahfl.runtime-session.v1` 继续保留为 success-path historical baseline
3. `emit-runtime-session` 已覆盖 success / partial / failed golden
4. runtime session model / bootstrap / emission 已进入 direct regression 与 CLI regression
