# AHFL Execution Plan Compatibility And Versioning V0.6

本文冻结 `emit-execution-plan` / `handoff::ExecutionPlan` 在 V0.6 阶段的版本化与兼容性契约，作为 future Native scheduler、local dry-run runner、audit/review consumer、golden 与回归测试的共同约束。

关联文档：

- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)
- [native-execution-plan-architecture-v0.6.zh.md](../design/native-execution-plan-architecture-v0.6.zh.md)
- [issue-backlog-v0.6.zh.md](../plan/issue-backlog-v0.6.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 execution plan 的版本标识是什么，出现在什么位置。
2. 哪些变化可视为兼容扩展，哪些变化必须 bump 版本。
3. execution plan 与 handoff package、dry-run trace 的兼容层次关系是什么。
4. 后续扩 execution plan 时，文档、代码、golden 与测试要如何同步。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.execution-plan.v1"`

当前实现中的单一来源位于：

- `include/ahfl/handoff/package.hpp`
  - `ahfl::handoff::kExecutionPlanFormatVersion`

当前该版本号会出现在：

1. `ExecutionPlan.format_version`
2. `emit-execution-plan` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 consumer 的主校验入口。
2. 仓库内所有 execution plan 输出都应共享同一个 `kExecutionPlanFormatVersion`。
3. future consumer 不应通过字段集合猜版本，而应显式检查 `format_version`。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- package-aware execution planning artifact 的结构化语义边界

包括：

1. 顶层 `source_package_identity`
2. 顶层 `entry_workflow_canonical_name`
3. `workflows[]`
4. workflow `entry_nodes`
5. workflow `dependency_edges`
6. workflow node `lifecycle`
7. workflow node `input_summary`
8. workflow node `capability_bindings`
9. workflow `return_summary`

当前不冻结为长期 ABI 的内容包括：

1. JSON 对象字段的物理输出顺序
2. 空数组之间的排版细节
3. future scheduler 私有执行策略
4. 真实 connector、timeout、retry、parallelism 或 persistence 字段

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 顶层 `source_package_identity`
3. 顶层 `entry_workflow_canonical_name`
4. workflow DAG 的 `entry_nodes` 与 `dependency_edges`
5. node `name`、`target`、`after`
6. node `lifecycle` 的最小摘要
7. node `capability_bindings`
8. node `input_summary`
9. workflow `return_summary`

当前 consumer 不应依赖：

1. workflow / node 的物理数组顺序具备额外语义之外的 ABI 含义
2. plan 已包含 mock、runtime host、deployment 或 connector 配置
3. plan 会表达真实 agent state machine 执行结果
4. plan 会替 scheduler 决定全部 future runtime policy

换句话说：

- consumer 应把 execution plan 当作“稳定 planning artifact”，而不是“完整 runtime execution ABI”。

## 与 handoff package / dry-run trace 的关系

三层兼容层次当前是：

1. handoff package
   - runtime-facing structured contract
2. execution plan
   - 从 handoff package 投影出的 planning artifact
3. dry-run trace
   - 基于 execution plan 与 mock/input 生成的 deterministic review artifact

当前契约要求：

1. execution plan consumer 不应回退读取 AST、raw source 或 project descriptor 补语义。
2. dry-run trace consumer 若需要 DAG / binding 语义，应先信任 execution plan，而不是反推 package。
3. 若 handoff package 的 breaking change 影响到 plan 语义，应分别评估 package 与 execution plan 是否都需要 bump 版本。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在 workflow / node 上新增可忽略辅助字段
3. 在 `lifecycle`、`input_summary`、`return_summary` 中新增可选辅助信息
4. 补充更明确的文档说明，而不改变旧字段语义

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 仍可通过读取 `format_version` 和忽略未知字段安全工作
4. 文档已同步声明新增字段的可忽略性

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变 `dependency_edges`、`after`、`lifecycle`、`input_summary`、`return_summary` 的结构语义
4. 改变 `capability_bindings` 的含义
5. 改变 `entry_workflow_canonical_name` 的选择规则或含义
6. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `dependency_edges` 从 DAG 边改为 scheduler hint
2. 把 `capability_bindings.binding_key` 改成 provider endpoint
3. 把 node `input_summary` 从 value-read summary 改成真实 payload snapshot

## 当前稳定性承诺

`emit-execution-plan` 当前必须满足：

1. 共享单一 `ahfl::handoff::kExecutionPlanFormatVersion`
2. 表达同一个 `handoff::ExecutionPlan` 结构化语义模型
3. 不要求 consumer 回看 AST、raw source 或 authoring descriptor 补语义
4. 与 `validate_execution_plan(...)` 共享同一组结构一致性前提

这意味着：

1. 若 execution plan 发生 breaking change，必须 bump `kExecutionPlanFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 execution plan 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/handoff/package.hpp`、`src/handoff/package.cpp` 与相关 backend / CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/plan/` golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::handoff::kExecutionPlanFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.execution-plan.v1`
2. `emit-execution-plan` 已覆盖 single-file / project / workspace golden
3. execution plan validation 已进入 direct regression 与 CLI failure regression
