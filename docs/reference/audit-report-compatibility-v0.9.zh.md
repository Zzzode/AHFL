# AHFL Audit Report Compatibility And Versioning V0.9

本文冻结 `emit-audit-report` / `audit_report::AuditReport` 在 V0.9 阶段的版本化与兼容性契约，作为 future audit tooling、CI review、artifact archival、golden 与 regression 的共同约束。

关联文档：

- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [issue-backlog-v0.9.zh.md](../plan/issue-backlog-v0.9.zh.md)

## 目标

本文主要回答五个问题：

1. 当前 audit report 的稳定版本标识是什么，出现在什么位置。
2. V0.8 success-path baseline 与 V0.9 failure-path baseline 的关系是什么。
3. 哪些 audit 字段是当前稳定边界。
4. 哪些变化可视为兼容扩展，哪些必须 bump 版本。
5. audit report 与 plan / session / journal / trace / replay 的兼容层次关系是什么。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.audit-report.v2"`

当前实现中的单一来源位于：

- `include/ahfl/audit_report/report.hpp`
  - `ahfl::audit_report::kAuditReportFormatVersion`

当前该版本号会出现在：

1. `AuditReport.format_version`
2. `emit-audit-report` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 audit consumer 的主校验入口。
2. audit consumer 不应通过 plan / session / journal / trace summary 的字段组合猜版本。
3. 仓库内所有 audit 输出都应共享同一个 `kAuditReportFormatVersion`。

## V0.8 与 V0.9 的关系

历史基线是：

1. `ahfl.audit-report.v1`
   - 冻结 V0.8 success-path aggregate 审查语义
2. `ahfl.audit-report.v2`
   - 冻结 V0.9 failure-aware aggregate 审查语义

这意味着：

1. `v1` 仍表示 historical success baseline。
2. 需要消费 `runtime_failed` / `partial` 结论、failure-aware journal summary 的 consumer，必须显式接受 `v2`。
3. 不允许把 `v2` 的新 conclusion / summary 语义伪装成 `v1` 的兼容扩展。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- deterministic reviewer-facing aggregate summary 的 failure-aware 结构化语义边界

包括：

1. 顶层 `source_package_identity`
2. 顶层 `workflow_canonical_name`
3. 顶层 `session_id`
4. 顶层 `run_id`
5. 顶层 `input_fixture`
6. 顶层 `conclusion`
7. 顶层 `plan_summary`
8. 顶层 `session_summary`
9. 顶层 `journal_summary`
10. 顶层 `trace_summary`
11. 顶层 `replay_consistency`
12. 顶层 `audit_consistency`
13. `plan_summary.execution_order`
14. `plan_summary.nodes[]`
15. `plan_summary.return_summary`
16. `session_summary.workflow_status`
17. `session_summary.nodes[]`
18. `journal_summary.total_events`
19. `journal_summary.node_failed_events`
20. `journal_summary.workflow_completed_events`
21. `journal_summary.workflow_failed_events`
22. `journal_summary.completed_node_order`
23. `trace_summary.execution_order`
24. `trace_summary.nodes[]`
25. `trace_summary.return_summary`

当前稳定结论集合包括：

1. `AuditConclusion = Passed / RuntimeFailed / Partial`
2. `AuditJournalSummary.node_failed_events`
3. `AuditJournalSummary.workflow_failed_events`

当前不冻结为长期 ABI 的内容包括：

1. 原始 trace node payload 的完整镜像
2. host metrics、timestamp、duration、machine id、worker id
3. connector secret、endpoint、tenant、region、deployment 或 provider payload
4. 真实生产审计系统需要的签名、归档、外部引用 id
5. host-side remediation 或 incident workflow 数据

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 顶层 `workflow_canonical_name`
3. 顶层 `session_id`
4. 顶层 `conclusion`
5. 四个 summary 的 source/version 对齐关系
6. `plan_summary.execution_order`
7. `session_summary.nodes[]`
8. `journal_summary.total_events`
9. `journal_summary.node_failed_events`
10. `journal_summary.workflow_failed_events`
11. `trace_summary.execution_order`
12. `replay_consistency`
13. `audit_consistency`

当前 consumer 不应依赖：

1. audit report 会承诺真实 host telemetry 或 provider payload
2. audit report 可替代原始 plan / session / journal / trace 作为唯一调试输入
3. audit report 的物理 JSON 字段顺序
4. audit report 会直接承担生产 runtime incident forensics ABI

换句话说：

- consumer 应把 audit report 当作“稳定审查汇总 artifact”，而不是“生产 observability / SIEM 事件协议”。

## 与 plan / session / journal / trace / replay 的关系

当前兼容层次是：

1. execution plan
   - planning artifact
2. runtime session
   - final state snapshot artifact
3. execution journal
   - deterministic event sequence artifact
4. dry-run trace
   - contributor-facing review artifact
5. replay view
   - session + journal consistency projection
6. audit report
   - plan / session / journal / trace / replay summary aggregate

当前契约要求：

1. audit consumer 不应跳过上游 artifact 自行从 AST 或 project descriptor 重建语义。
2. audit report 的 replay consistency 应复用 replay 稳定边界，而不是在 audit 层重新定义另一套投影规则。
3. audit report 的 failure 结论必须来自 session / journal / replay 已冻结的稳定边界。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在顶层 object 中新增可选字段，且不改变旧字段含义
2. 在各类 summary 中新增可忽略统计字段
3. 在 `nodes[]` 上新增可忽略辅助字段
4. 为当前 failure-aware 审查输出补充更明确的文档说明，而不改变旧字段语义

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 仍可通过读取 `format_version` 并忽略未知字段安全工作
4. 文档、golden 与 regression 已同步声明新增字段的可忽略性

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变 `conclusion`、四类 summary、`replay_consistency` 或 `audit_consistency` 的结构语义
4. 改变 summary 之间的 source/version 对齐关系
5. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `RuntimeFailed` 从“runtime result failure”改成“任意 audit 组装失败”
2. 把 `journal_summary.node_failed_events` 从 failure 计数改成 host diagnostic 数
3. 把 `trace_summary.nodes[]` 从 deterministic selector 摘要改成真实 provider payload
4. 把 audit report 从 aggregate summary 改成完整 host telemetry mirror

## 当前稳定性承诺

`emit-audit-report` 当前必须满足：

1. 共享单一 `ahfl::audit_report::kAuditReportFormatVersion`
2. 表达同一个 `audit_report::AuditReport` 结构化语义模型
3. 只聚合 plan / session / journal / trace / replay 的稳定边界
4. 不输出真实 connector、secret、tenant、region、deployment 或 host telemetry

这意味着：

1. 若 audit report 发生 breaking change，必须 bump `kAuditReportFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 audit report 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/audit_report/report.hpp`、`src/audit_report/`、`src/backends/audit_report.cpp` 与相关 CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/audit/` golden
4. 更新或新增 direct regression 与 CLI regression

若属于 breaking change，还必须额外完成：

1. bump `ahfl::audit_report::kAuditReportFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.audit-report.v2`
2. `ahfl.audit-report.v1` 继续保留为 success-path historical baseline
3. `emit-audit-report` 已覆盖 success / partial / failed golden
4. audit report model / bootstrap / emission 已进入 direct regression 与 CLI regression
