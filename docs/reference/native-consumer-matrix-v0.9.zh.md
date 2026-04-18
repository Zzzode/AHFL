# AHFL Native Consumer Matrix V0.9

本文冻结 AHFL V0.9 中 runtime-adjacent consumer 的当前矩阵，重点覆盖 execution plan、runtime session、execution journal、dry-run trace、replay view、audit report 各自稳定依赖什么，以及 future scheduler prototype / replay verifier / audit tooling / contributor review 应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.8.zh.md](./native-consumer-matrix-v0.8.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 failure-path 边界 | 当前明确不承诺 |
|----------|----------|--------------|------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | 无 runtime failure 语义；只提供 planning DAG 与 return contract | mock 输入、真实 runtime host、provider payload |
| Runtime Session Consumer | scheduler-ready state snapshot 输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node `Blocked / Ready / Completed / Failed / Skipped`、`failure_summary` | host metrics、deployment、真实 connector |
| Execution Journal Consumer | replay / audit / scheduler prototype event 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | `mock_missing`、`node_failed`、`workflow_failed` event family、prefix execution ordering | host log payload、timestamp、machine id |
| Replay View Consumer | replay consistency / state progression 投影输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | `ReplayStatus = Consistent / RuntimeFailed / Partial`、`workflow_failure_summary`、node `saw_node_failed` | 真实 failure host payload、recovery ABI、deployment telemetry |
| Audit Report Consumer | review / CI / archival 的稳定 aggregate 审查输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | `AuditConclusion = Passed / RuntimeFailed / Partial`、journal failed-event 计数、prefix consistency summary | 生产 observability payload、SIEM schema、外部审计签名 |
| Local Dry-Run Runner | 本地 deterministic DAG / binding / value-flow 审查 | `handoff::ExecutionPlan`、`CapabilityMockSet`、`DryRunRequest` | 仅提供 deterministic mock 输入，不等于正式 runtime failure artifact | 真实 connector、真实 agent state transition、deployment |
| Dry-Run Trace Consumer | contributor-facing review / golden / regression 消费 | `emit-dry-run-trace`、`dry_run::DryRunTrace` | 可表达 local dry-run completed / pending，但不是正式 runtime failed / partial ABI | 正式 replay state、长期 event ABI、host metrics |
| Package Review Output | package-aware contributor review/debug | `handoff::Package` + reader/planner helper | 无 runtime failure 语义 | runtime 计划、host side effect |
| Native JSON Consumer | 机器消费完整 handoff package | 全量 `emit-native-json` 输出 | 无 runtime failure 语义 | 运行时行为本身 |

## 当前共享入口

V0.9 当前 runtime-adjacent consumer 共享入口是：

1. `handoff::build_execution_plan(...)`
2. `handoff::validate_execution_plan(...)`
3. `dry_run::parse_capability_mock_set_json(...)`
4. `runtime_session::build_runtime_session(...)`
5. `runtime_session::validate_runtime_session(...)`
6. `execution_journal::build_execution_journal(...)`
7. `execution_journal::validate_execution_journal(...)`
8. `replay_view::build_replay_view(...)`
9. `replay_view::validate_replay_view(...)`
10. `audit_report::build_audit_report(...)`
11. `audit_report::validate_audit_report(...)`
12. `ahflc emit-execution-plan`
13. `ahflc emit-runtime-session`
14. `ahflc emit-execution-journal`
15. `ahflc emit-replay-view`
16. `ahflc emit-audit-report`
17. `ahflc emit-dry-run-trace`

这表示：

1. future scheduler prototype / replay / audit consumer 不应自己重建 plan / session / journal / replay 语义。
2. consumer 不需要重新扫描 AST、raw source、project descriptor、workspace descriptor 或 authoring descriptor。
3. runtime-adjacent prototype 应沿着 `plan -> session -> journal -> replay -> audit` 层次前进，而不是跳层。

## 当前扩展模板

当前建议复用下面十层模板：

1. authoring 输入层
   - `ahfl.package.json`
2. handoff 共享模型层
   - `handoff::Package`
3. planning artifact 层
   - `handoff::ExecutionPlan`
4. deterministic input 层
   - `CapabilityMockSet`
   - `DryRunRequest`
5. state snapshot 层
   - `runtime_session::RuntimeSession`
6. event sequence 层
   - `execution_journal::ExecutionJournal`
7. replay projection 层
   - `replay_view::ReplayView`
8. aggregate audit 层
   - `audit_report::AuditReport`
9. direct helper / validator 层
   - `build_execution_plan(...)`
   - `build_runtime_session(...)`
   - `validate_runtime_session(...)`
   - `build_execution_journal(...)`
   - `validate_execution_journal(...)`
   - `build_replay_view(...)`
   - `validate_replay_view(...)`
   - `build_audit_report(...)`
   - `validate_audit_report(...)`
10. backend / CLI 输出层
   - `emit-execution-plan`
   - `emit-runtime-session`
   - `emit-execution-journal`
   - `emit-replay-view`
   - `emit-audit-report`
   - `emit-dry-run-trace`

新增 runtime-adjacent consumer prototype 时，不应跳过第 2-9 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

## 推荐消费顺序

当前推荐依赖顺序是：

1. 若要做 planning / runner bootstrap
   - 优先消费 `ExecutionPlan`
2. 若要做 final state reasoning
   - 优先消费 `RuntimeSession`
3. 若要做 event ordering / failure progression / richer scheduler prototype
   - 优先消费 `ExecutionJournal`
4. 若要做 deterministic replay consistency
   - 优先消费 `ReplayView`
5. 若要做 review / CI / archival aggregate summary
   - 优先消费 `AuditReport`
6. 若要做 contributor-facing readable review
   - 优先消费 `DryRunTrace`

换句话说：

- `RuntimeSession` 适合回答“现在停在哪、哪些 node 已失败 / 跳过 / 未完成”；
- `ExecutionJournal` 适合回答“为什么停在这里、失败是怎样演进到 terminal event 的”；
- `ReplayView` 适合回答“session / journal 的已执行前缀是否能稳定投影为 replay state”；
- `AuditReport` 适合回答“这次运行最终应如何被 review / CI / archive 归档”；
- `DryRunTrace` 仍适合“看懂这次本地 dry-run 做了什么”，但不是正式 failure ABI。

## Future Scheduler Prototype 应落在哪里

当前 richer scheduler prototype 的推荐依赖顺序是：

1. 先消费 `ExecutionPlan`
   - 获取 planning DAG、dependency、binding 与 return contract
2. 再消费 `RuntimeSession`
   - 获取当前 workflow / node 稳定状态快照
3. 再消费 `ExecutionJournal`
   - 获取 deterministic event ordering 与 failure progression
4. 若需要 consistency / replay verification 视角
   - 再消费 `ReplayView`
5. 若需要 reviewer-facing aggregate 结论
   - 最后消费 `AuditReport`

这意味着：

1. richer scheduler prototype 不应直接从 `ReplayView` 或 `AuditReport` 倒推下一步调度决策。
2. richer scheduler prototype 不应直接从 `DryRunTrace` 推导 runtime terminal failure。
3. `ReplayView` / `AuditReport` 是 consumer-facing projection，不是 scheduler state machine 的第一事实来源。

## 当前反模式

当前明确不建议：

1. 把 `DryRunTrace` 当作 future replay / audit tooling 或 scheduler prototype 的唯一正式输入
2. 跳过 `RuntimeSession`，直接从 journal / trace / AST 猜测 node 当前状态
3. 跳过 `ExecutionJournal`，直接从 trace 或 AST 重建 failure progression
4. 跳过 `ReplayView`，在 audit tooling 内重新实现 journal -> state projection
5. 在 replay / audit / CLI / 外部脚本层私造失败 taxonomy，而不先进入 session / journal
6. 为了 failure-path 先引入真实 host log、provider payload、deployment telemetry
7. 在未更新 compatibility / matrix / contributor docs 的情况下静默扩张 failure-path 稳定边界

## Future Consumer 应落在哪里

当前建议：

1. future scheduler prototype
   - 先消费 `RuntimeSession`
   - 再消费 `ExecutionJournal`
   - 若需要 consistency 视角，再消费 `ReplayView`
   - 不直接从 package / AST / trace 倒推 DAG 或 event 语义
2. future replay verifier
   - 优先消费 `ReplayView`
   - 若需要追根到 event 输入，再回看 `ExecutionJournal`
   - 不直接把 `DryRunTrace` 当正式 replay ABI
3. future audit tooling / CI archive
   - 优先消费 `AuditReport`
   - 若需要 replay consistency 细节，再回看 `ReplayView`
   - 若需要原始 event / state，再回看 `ExecutionJournal` / `RuntimeSession`
4. future contributor review / debugging
   - 优先消费 `DryRunTrace`
   - 若缺 event / state / consistency / terminal failure 语义，再回看 `ExecutionJournal` / `RuntimeSession` / `ReplayView`
5. future deployment / launcher
   - 不属于 V0.9 current stable consumer matrix
   - 不应把 secret、endpoint、tenant、region 塞回 plan / session / journal / replay / audit / trace

## 当前状态

截至当前实现：

1. execution plan 已成为 runtime session 的正式上游输入
2. runtime session 已成为 execution journal 的正式上游输入
3. execution journal 已成为 replay view 的正式 event 输入
4. replay view 已成为 audit report 的正式 consistency 输入之一
5. audit report 已成为 review / CI / golden 的稳定 aggregate 输入
6. dry-run trace 仍保留为 contributor-facing review 输出，而不是正式 replay / audit / scheduler failure ABI
7. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> audit`
