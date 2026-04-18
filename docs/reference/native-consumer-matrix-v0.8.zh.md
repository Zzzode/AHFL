# AHFL Native Consumer Matrix V0.8

本文冻结 AHFL V0.8 中 runtime-adjacent consumer 的当前矩阵，重点覆盖 execution plan、runtime session、execution journal、dry-run trace、replay view、audit report 各自稳定依赖什么，以及 future scheduler / audit tooling / contributor review 应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.7.zh.md](./native-consumer-matrix-v0.7.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./execution-journal-compatibility-v0.7.zh.md)
- [replay-view-compatibility-v0.8.zh.md](./replay-view-compatibility-v0.8.zh.md)
- [audit-report-compatibility-v0.8.zh.md](./audit-report-compatibility-v0.8.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](../design/native-replay-audit-bootstrap-v0.8.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前明确不承诺 |
|----------|----------|--------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | mock 输入、真实 runtime host、provider payload |
| Runtime Session Consumer | scheduler-ready state snapshot 输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | host metrics、deployment、真实 connector |
| Execution Journal Consumer | replay / audit / scheduler prototype event 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | host log payload、timestamp、machine id |
| Replay View Consumer | replay consistency / state progression 投影输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | 真实 failure host payload、recovery ABI、deployment telemetry |
| Audit Report Consumer | review / CI / archival 的稳定 aggregate 审查输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | 生产 observability payload、SIEM schema、外部审计签名 |
| Local Dry-Run Runner | 本地 deterministic DAG / binding / value-flow 审查 | `handoff::ExecutionPlan`、`CapabilityMockSet`、`DryRunRequest` | 真实 connector、真实 agent state transition、deployment |
| Dry-Run Trace Consumer | contributor-facing review / golden / regression 消费 | `emit-dry-run-trace`、`dry_run::DryRunTrace` | 正式 replay state、长期 event ABI、host metrics |
| Package Review Output | package-aware contributor review/debug | `handoff::Package` + reader/planner helper | runtime 计划、host side effect |
| Native JSON Consumer | 机器消费完整 handoff package | 全量 `emit-native-json` 输出 | 运行时行为本身 |

## 当前共享入口

V0.8 当前 runtime-adjacent consumer 共享入口是：

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

1. future scheduler / replay / audit consumer 不应自己重建 plan / session / journal / replay 语义。
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
3. 若要做 event ordering / replay bootstrap
   - 优先消费 `ExecutionJournal`
4. 若要做 deterministic replay consistency
   - 优先消费 `ReplayView`
5. 若要做 review / CI / archival aggregate summary
   - 优先消费 `AuditReport`
6. 若要做 contributor-facing readable review
   - 优先消费 `DryRunTrace`

换句话说：

- `DryRunTrace` 仍适合“看懂这次本地 dry-run 做了什么”；
- `ReplayView` 适合“验证 journal / session 是否能稳定投影为 replay state”；
- `AuditReport` 适合“归档这次运行的审查结论与一致性摘要”。

## 当前反模式

当前明确不建议：

1. 把 `DryRunTrace` 当作 future replay / audit tooling 的唯一正式输入
2. 跳过 `ExecutionJournal`，直接从 trace 或 AST 重建 replay progression
3. 跳过 `ReplayView`，在 audit tooling 内重新实现 journal -> state projection
4. 为了 failure-path 先引入真实 host log、provider payload、deployment telemetry
5. 在未更新 compatibility / matrix / contributor docs 的情况下静默扩张 replay / audit 稳定边界

## Future Scheduler / Replay / Audit Consumer 应落在哪里

当前建议：

1. future scheduler prototype
   - 先消费 `RuntimeSession`
   - 再消费 `ExecutionJournal`
   - 若需要 consistency 视角，再消费 `ReplayView`
   - 不直接从 package / AST 倒推 DAG 或 event 语义
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
   - 若缺 event / state / consistency 语义，再回看 `ExecutionJournal` / `RuntimeSession` / `ReplayView`
5. future deployment / launcher
   - 不属于 V0.8 current stable consumer matrix
   - 不应把 secret、endpoint、tenant、region 塞回 plan / session / journal / replay / audit / trace

## 当前状态

截至当前实现：

1. execution plan 已成为 runtime session 的正式上游输入
2. runtime session 已成为 execution journal 的正式上游输入
3. execution journal 已成为 replay view 的正式 event 输入
4. replay view 已成为 audit report 的正式 consistency 输入之一
5. audit report 已成为 review / CI / golden 的稳定 aggregate 输入
6. dry-run trace 仍保留为 contributor-facing review 输出，而不是正式 replay / audit ABI
