# AHFL Native Consumer Matrix V0.5

本文冻结 AHFL V0.5 中 runtime-adjacent reference consumer 的当前矩阵，重点覆盖 package authoring 进入正式输入边界之后，Package Reader、Execution Planner Bootstrap 与 review/debug 路径各自稳定依赖什么。

关联文档：

- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前明确不承诺 |
|----------|----------|--------------|----------------|
| Package Reader Summary | package identity / entry / export / binding / policy 预检 | `metadata`、`capability_binding_slots`、`policy_obligations`、`formal_observations` | deployment 配置、connector 配置、JSON 字段顺序 |
| Execution Planner Bootstrap | workflow DAG 与最小 lifecycle 承接 | workflow `execution_graph`、node `lifecycle`、node `input_summary`、workflow `return_summary` | scheduler、retry、timeout、state persistence |
| Package Review Output | 面向作者与贡献者的 review/debug 输出 | `handoff::Package` + 上述两个 summary helper | runtime 计划、host side effect |
| Native JSON Consumer | 机器消费完整 handoff package | 全量 `emit-native-json` 输出 | 运行时行为本身 |

## 当前共享入口

V0.5 当前 reference consumer 共享入口是：

1. `handoff::build_package_reader_summary(...)`
2. `handoff::build_execution_planner_bootstrap(...)`
3. `handoff::build_entry_execution_planner_bootstrap(...)`
4. `ahflc emit-package-review`

这表示：

1. CLI review/debug 不再自己重建 package 语义
2. future runtime-adjacent consumer prototype 可直接复用 handoff helper
3. consumer 不需要重新扫描 AST、raw source 或 authoring descriptor

## 最小扩展规则

若后续新增新的 runtime-adjacent consumer：

1. 先确认它依赖的是 reader summary、planner bootstrap，还是完整 native package
2. 若需要新增共享字段，先扩 `handoff::Package`
3. 再扩 summary helper / backend / golden / docs
4. 不要先写 JSON-specific parser 再回头抽象公共模型

## 当前扩展模板

当前建议复用下面四层模板：

1. authoring 输入层
   - `ahfl.package.json`
   - `PackageAuthoringDescriptor`
2. handoff 共享模型层
   - `handoff::Package`
3. direct helper 层
   - `build_package_reader_summary(...)`
   - `build_execution_planner_bootstrap(...)`
4. backend / CLI 输出层
   - `emit-native-json`
   - `emit-package-review`

新增 consumer prototype 时，不应跳过第 2、3 层直接在 CLI 或 JSON parser 内实现私有语义。

## 当前状态

截至当前实现：

1. package authoring 输入已正式进入 compiler / handoff 边界
2. package review/debug 已通过 `emit-package-review` 提供稳定输出
3. reference Package Reader Summary 与 Execution Planner Bootstrap 已有 direct handoff helper 与回归覆盖
