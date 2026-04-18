# AHFL Native Consumer Matrix V0.7

本文冻结 AHFL V0.7 中 runtime-adjacent consumer 的当前矩阵，重点覆盖 execution plan、runtime session、execution journal、dry-run trace 各自稳定依赖什么，以及 future scheduler / replay / audit consumer 应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.6.zh.md](./native-consumer-matrix-v0.6.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./execution-journal-compatibility-v0.7.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-runtime-session-bootstrap-v0.7.zh.md](../design/native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](../design/native-execution-journal-v0.7.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前明确不承诺 |
|----------|----------|--------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | mock 输入、真实 runtime host、provider payload |
| Runtime Session Consumer | scheduler-ready state snapshot 输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | host metrics、deployment、真实 connector |
| Execution Journal Consumer | replay / audit / scheduler prototype event 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | host log payload、timestamp、machine id |
| Local Dry-Run Runner | 本地 deterministic DAG / binding / value-flow 审查 | `handoff::ExecutionPlan`、`CapabilityMockSet`、`DryRunRequest` | 真实 connector、真实 agent state transition、deployment |
| Dry-Run Trace Consumer | review / audit / golden / regression 消费 | `emit-dry-run-trace`、`dry_run::DryRunTrace` | 生产 runtime log、performance metrics、host id |
| Package Review Output | package-aware contributor review/debug | `handoff::Package` + reader/planner helper | runtime 计划、host side effect |
| Native JSON Consumer | 机器消费完整 handoff package | 全量 `emit-native-json` 输出 | 运行时行为本身 |

## 当前共享入口

V0.7 当前 runtime-adjacent consumer 共享入口是：

1. `handoff::build_execution_plan(...)`
2. `handoff::validate_execution_plan(...)`
3. `dry_run::parse_capability_mock_set_json(...)`
4. `runtime_session::build_runtime_session(...)`
5. `runtime_session::validate_runtime_session(...)`
6. `execution_journal::build_execution_journal(...)`
7. `execution_journal::validate_execution_journal(...)`
8. `ahflc emit-execution-plan`
9. `ahflc emit-runtime-session`
10. `ahflc emit-execution-journal`
11. `ahflc emit-dry-run-trace`

这表示：

1. future scheduler / replay / audit consumer 不应自己重建 plan / session / journal 语义。
2. consumer 不需要重新扫描 AST、raw source、project descriptor 或 authoring descriptor。
3. runtime-adjacent prototype 应沿着 `plan -> session -> journal -> trace` 层次前进，而不是跳层。

## 当前扩展模板

当前建议复用下面八层模板：

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
7. direct helper / validator 层
   - `build_execution_plan(...)`
   - `build_runtime_session(...)`
   - `validate_runtime_session(...)`
   - `build_execution_journal(...)`
   - `validate_execution_journal(...)`
8. backend / CLI 输出层
   - `emit-execution-plan`
   - `emit-runtime-session`
   - `emit-execution-journal`
   - `emit-dry-run-trace`

新增 runtime-adjacent consumer prototype 时，不应跳过第 2-7 层直接在 CLI 或 JSON parser 内实现私有语义。

## Future Scheduler / Replay / Audit Consumer 应落在哪里

当前建议：

1. future scheduler prototype
   - 先消费 `RuntimeSession`
   - 再消费 `ExecutionJournal`
   - 不直接从 package / AST 倒推 DAG 或 event 语义
2. future replay / audit consumer
   - 优先消费 `ExecutionJournal`
   - 若需要 state snapshot，再回看 `RuntimeSession`
3. future review / contributor audit
   - 优先消费 `DryRunTrace`
   - 若缺 event / state 语义，再回看 `ExecutionJournal` / `RuntimeSession`
4. future deployment / launcher
   - 不属于 V0.7 current stable consumer matrix
   - 不应把 secret、endpoint、tenant、region 塞回 plan / session / journal / trace

## 当前状态

截至当前实现：

1. execution plan 已成为 runtime session 的正式上游输入
2. runtime session 已成为 execution journal 的正式上游输入
3. execution journal 已成为 replay / audit / scheduler prototype 的稳定 event 输入
4. dry-run trace 仍保留为 contributor-facing review 输出
