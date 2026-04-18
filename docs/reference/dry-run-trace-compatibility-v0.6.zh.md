# AHFL Dry-Run Trace Compatibility And Versioning V0.6

本文冻结 `emit-dry-run-trace` / `dry_run::DryRunTrace` 在 V0.6 阶段的版本化与兼容性契约，作为 local review、audit consumer、golden 与 future runtime-adjacent trace tooling 的共同约束。

关联文档：

- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](../design/native-dry-run-bootstrap-v0.6.zh.md)
- [issue-backlog-v0.6.zh.md](../plan/issue-backlog-v0.6.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 dry-run trace 的版本标识是什么，出现在什么位置。
2. 哪些 trace 字段是当前稳定边界。
3. 哪些变化可视为兼容扩展，哪些必须 bump 版本。
4. dry-run trace 与 execution plan / capability mock set 的兼容关系是什么。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.dry-run-trace.v1"`

当前实现中的单一来源位于：

- `include/ahfl/dry_run/runner.hpp`
  - `ahfl::dry_run::kTraceFormatVersion`

当前该版本号会出现在：

1. `DryRunTrace.format_version`
2. `emit-dry-run-trace` 顶层 `format_version`

当前契约要求：

1. 顶层 `format_version` 是 trace consumer 的主校验入口。
2. trace consumer 不应通过 node 字段集合猜版本。
3. trace 输出与 `kTraceFormatVersion` 必须保持单一来源。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- deterministic local dry-run review artifact 的结构化语义边界

包括：

1. 顶层 `source_execution_plan_format_version`
2. 顶层 `source_package_identity`
3. 顶层 `workflow_canonical_name`
4. 顶层 `status`
5. 顶层 `input_fixture`
6. 顶层 `run_id`
7. 顶层 `execution_order`
8. `node_traces[]`
9. node `satisfied_dependencies`
10. node `capability_bindings`
11. node `mock_results`
12. 顶层 `return_summary`

当前不冻结为长期 ABI 的内容包括：

1. 真实 capability payload schema
2. wall clock duration 或 performance metrics
3. host process / machine / thread id
4. connector secret、endpoint、tenant、region 或 deployment 信息

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. 顶层 `source_execution_plan_format_version`
3. 顶层 `workflow_canonical_name`
4. `execution_order`
5. node `execution_index`
6. node `satisfied_dependencies`
7. node `capability_bindings`
8. node `mock_results`
9. 顶层 `return_summary`

当前 consumer 不应依赖：

1. trace 包含真实 capability 响应 payload
2. trace 可证明真实 provider 可用
3. trace 可替代 execution plan 作为 planning artifact
4. trace 的物理 JSON 字段顺序

换句话说：

- consumer 应把 dry-run trace 当作“deterministic local review artifact”，而不是“生产 runtime execution log ABI”。

## 与 execution plan / capability mock set 的关系

dry-run trace 当前依赖两类正式输入：

1. execution plan
2. capability mock set + dry-run request

当前契约要求：

1. 若 execution plan 不兼容，trace consumer 应先在上游 plan 层失败，而不是容忍模糊语义。
2. 若 mock set 缺失、重复或未使用，应在 dry-run 层返回 diagnostics，而不是把问题沉默写入 trace。
3. trace 只记录 mock 元数据与 fixture 标识，不记录真实 connector 配置。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在 `node_traces[]` 上新增可忽略辅助字段
3. 在 `mock_results[]` 中新增可忽略元数据字段
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
3. 改变 `execution_order`、`satisfied_dependencies`、`mock_results` 或 `return_summary` 的结构语义
4. 改变 `status` 的枚举含义
5. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `execution_order` 从 node name 序列改成 scheduler-internal token
2. 把 `mock_results.result_fixture` 从 deterministic fixture 标识改成真实 payload
3. 把 `status = completed` 改成另一套不兼容状态枚举而不保留旧值

## 当前稳定性承诺

`emit-dry-run-trace` 当前必须满足：

1. 共享单一 `ahfl::dry_run::kTraceFormatVersion`
2. 表达同一个 `dry_run::DryRunTrace` 结构化语义模型
3. 不要求 consumer 回看 AST、raw source、project descriptor 或 package authoring descriptor 补语义
4. 不输出真实 connector、secret、tenant、region 或 deployment 信息

这意味着：

1. 若 dry-run trace 发生 breaking change，必须 bump `kTraceFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## 变更流程

后续任何 dry-run trace 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/dry_run/runner.hpp`、`src/dry_run/`、`src/backends/dry_run_trace.cpp` 与相关 CLI
2. 更新本文档及相关 reference/design 文档
3. 更新受影响的 `tests/trace/` golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::dry_run::kTraceFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.dry-run-trace.v1`
2. `emit-dry-run-trace` 已覆盖 single-file / project / workspace golden
3. dry-run mock parser / missing mock / unused mock 已进入 direct regression
