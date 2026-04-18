# AHFL Native Consumer Matrix V0.6

本文冻结 AHFL V0.6 中 runtime-adjacent consumer 的当前矩阵，重点覆盖 handoff package、execution plan、local dry-run runner 与 dry-run trace 各自稳定依赖什么，以及 future scheduler / audit consumer 应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-execution-plan-architecture-v0.6.zh.md](../design/native-execution-plan-architecture-v0.6.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](../design/native-dry-run-bootstrap-v0.6.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前明确不承诺 |
|----------|----------|--------------|----------------|
| Package Reader Summary | package identity / entry / export / binding / policy 预检 | `metadata`、`capability_binding_slots`、`policy_obligations`、`formal_observations` | deployment 配置、connector 配置、JSON 字段顺序 |
| Execution Planner Bootstrap | workflow DAG 与最小 lifecycle 承接 | workflow `execution_graph`、node `lifecycle`、node `input_summary`、workflow `return_summary` | scheduler、retry、timeout、state persistence |
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | mock 输入、真实 runtime host、provider payload |
| Local Dry-Run Runner | 本地 deterministic DAG / binding / value-flow 审查 | `handoff::ExecutionPlan`、`CapabilityMockSet`、`DryRunRequest` | 真实 connector、真实 agent state transition、deployment |
| Dry-Run Trace Consumer | review / audit / golden / regression 消费 | `emit-dry-run-trace`、`dry_run::DryRunTrace` | 生产 runtime log、performance metrics、host id |
| Package Review Output | package-aware contributor review/debug | `handoff::Package` + reader/planner helper | runtime 计划、host side effect |
| Native JSON Consumer | 机器消费完整 handoff package | 全量 `emit-native-json` 输出 | 运行时行为本身 |

## 当前共享入口

V0.6 当前 runtime-adjacent consumer 共享入口是：

1. `handoff::build_package_reader_summary(...)`
2. `handoff::build_execution_planner_bootstrap(...)`
3. `handoff::build_execution_plan(...)`
4. `handoff::validate_execution_plan(...)`
5. `dry_run::parse_capability_mock_set_json(...)`
6. `dry_run::run_local_dry_run(...)`
7. `ahflc emit-package-review`
8. `ahflc emit-execution-plan`
9. `ahflc emit-dry-run-trace`

这表示：

1. planner / runner / trace 都不应自己重建 package 语义
2. future runtime-adjacent consumer prototype 可直接复用 plan / dry-run 公共模型
3. consumer 不需要重新扫描 AST、raw source、project descriptor 或 authoring descriptor

## 当前扩展模板

当前建议复用下面六层模板：

1. authoring 输入层
   - `ahfl.package.json`
2. handoff 共享模型层
   - `handoff::Package`
3. planning artifact 层
   - `handoff::ExecutionPlan`
4. dry-run 输入层
   - `CapabilityMockSet`
   - `DryRunRequest`
5. direct helper / runner 层
   - `build_execution_plan(...)`
   - `validate_execution_plan(...)`
   - `run_local_dry_run(...)`
6. backend / CLI 输出层
   - `emit-execution-plan`
   - `emit-dry-run-trace`
   - `emit-package-review`

新增 consumer prototype 时，不应跳过第 2-5 层直接在 CLI 或 JSON parser 内实现私有语义。

## Future Scheduler / Audit Consumer 应落在哪里

当前建议：

1. future scheduler prototype
   - 先消费 `ExecutionPlan`
   - 不直接从 package / AST 倒推 DAG
2. future dry-run / simulation audit
   - 先消费 `DryRunTrace`
   - 若缺 planning 语义，再回看 `ExecutionPlan`
3. future deployment / launcher
   - 不属于 V0.6 current stable consumer matrix
   - 不应把 secret、endpoint、tenant、region 塞回 plan / trace

## 当前状态

截至当前实现：

1. execution plan 已成为 local dry-run runner 的正式输入
2. capability mock set 已成为 dry-run 的正式 deterministic 输入
3. dry-run trace 已成为 contributor-facing golden / review 输出
