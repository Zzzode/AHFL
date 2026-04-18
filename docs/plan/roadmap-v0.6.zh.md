# AHFL V0.6 Roadmap

本文给出 AHFL 在 V0.5 package authoring 与 reference consumer bootstrap 完成之后的下一阶段实施路线。V0.6 的重点不是直接交付完整 Native runtime，而是把已经稳定的 handoff package 推进为“可规划、可 dry-run、可生成可审查 trace”的 execution planning 与 local dry-run bootstrap 边界。

基线输入：

- [roadmap-v0.5.zh.md](./roadmap-v0.5.zh.md)（已完成基线）
- [issue-backlog-v0.5.zh.md](./issue-backlog-v0.5.zh.md)（已完成基线）
- [native-execution-plan-architecture-v0.6.zh.md](../design/native-execution-plan-architecture-v0.6.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](../design/native-dry-run-bootstrap-v0.6.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](../reference/native-handoff-usage-v0.5.zh.md)
- [native-consumer-matrix-v0.5.zh.md](../reference/native-consumer-matrix-v0.5.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](../reference/native-package-authoring-compatibility-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [testing-strategy-v0.5.zh.md](../design/testing-strategy-v0.5.zh.md)

当前实现状态：

1. V0.5 已完成 package metadata authoring、`emit-native-json --package`、`emit-package-review`、direct reference consumer helper、compatibility、consumer matrix、CI 与 contributor 文档闭环。
2. 当前 reference Execution Planner Bootstrap 已能从 `handoff::Package` 读取 workflow DAG、entry nodes、node lifecycle、dependency edges、input summary 与 return summary。
3. 当前仓库已新增内部 `ExecutionPlan` 数据模型与 `build_execution_plan(...)` helper，可把 planner bootstrap 结果投影为稳定 planning artifact。
4. 当前仓库已新增 `emit-execution-plan` CLI/backend 输出，并用 single-file / project / workspace golden 固定基础 execution plan artifact。
5. 当前 execution plan 还缺少独立 validation 负例与 failure diagnostic 回归；dry-run runner 不能承担这些诊断责任。
6. 当前仓库仍没有 local dry-run runner；consumer 可以预检 package 和 execution plan，但不能在不接真实 connector 的前提下跑一条 deterministic execution trace。
7. 当前阶段仍未进入生产 runtime launcher、真实 capability connector、deployment、tenant、secret、region、scheduler 优化或 state persistence 实现。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.6 的交付目标是：

1. 冻结最小 Execution Plan artifact，使 V0.5 planner bootstrap 结果可以成为稳定、可审查、可回归的中间输出。
2. 新增 `emit-execution-plan` 或等价 backend / CLI 路径，用于输出 package-aware execution plan。
3. 建立 local dry-run runner bootstrap，在不触发真实 connector / side effect 的前提下生成 deterministic execution trace。
4. 定义 mock capability binding 与 dry-run input 的最小边界，让 runner 可以验证 workflow DAG、node order 与值流引用。
5. 继续保持 Core / handoff / dry-run / future Native runtime 的职责拆分，不把 deployment 或真实 host integration 混入当前阶段。

## 非目标

V0.6 仍然不直接承诺：

1. 生产 runtime launcher、long-running scheduler 或 host daemon
2. 真实 capability connector、provider SDK、secret、endpoint、region 或 tenant 配置
3. deployment、publish、registry、package distribution 或 multi-package build graph
4. retry、timeout、parallel scheduling、state persistence、checkpoint / resume
5. 完整 expression / statement interpreter 或真实 agent state machine execution

## 里程碑

### M0 V0.6 Scope 与 Execution Planning 边界冻结

状态：

- [x] Issue 01 冻结 V0.6 的 execution planning / local dry-run scope 与 runtime 非目标
- [x] Issue 02 冻结 Execution Plan artifact 的最小模型与版本边界
- [x] Issue 03 冻结 dry-run input、mock capability binding 与 trace 输出边界

目标：

1. 明确 V0.6 做的是“execution plan artifact + local dry-run bootstrap”，不是“生产 runtime implementation”。
2. 明确 execution plan 与 handoff package、planner bootstrap、future runtime scheduler 的职责拆分。
3. 明确 dry-run runner 允许消费哪些输入，哪些仍属于 future runtime / host integration。

关键输出：

1. 一致的 V0.6 scope 说明
2. 一致的 Execution Plan artifact 设计说明
3. 一致的 dry-run input / trace 边界说明

退出标准：

1. V0.6 的目标不再与真实 launcher / connector / deployment 混淆
2. 下游 consumer 清楚 handoff package、execution plan、dry-run trace 的层次关系
3. 后续 issue 不把 runtime 私有调度策略直接写进 compiler backlog

### M1 Execution Plan Model 与 CLI 输出

状态：

- [x] Issue 04 引入 Execution Plan 数据模型
- [x] Issue 05 增加 `emit-execution-plan` 输出路径
- [x] Issue 06 增加 Execution Plan validation 与 golden 回归

目标：

1. 把 V0.5 `ExecutionPlannerBootstrap` 提升为可序列化、可审查的稳定 artifact。
2. 让 single-file、`--project`、`--workspace --project-name` 与 `--package` 路径都能生成一致 execution plan。
3. 让 plan 中的 workflow node、dependency、capability binding、input / return summary 错误可以被稳定诊断。

关键输出：

1. 稳定的 Execution Plan 数据模型
2. `emit-execution-plan` 或等价输出
3. 对 plan JSON / review 的 golden 与 validation 回归

退出标准：

1. planner consumer 不再只能依赖 C++ helper 临时对象
2. execution plan 可作为 dry-run runner 的正式输入
3. plan 失败可以区分 handoff package 错误与 plan validation 错误

### M2 Local Dry-Run Runner Bootstrap

状态：

- [x] Issue 07 引入 local dry-run runner 数据模型
- [x] Issue 08 增加 mock capability binding 与 deterministic result 输入
- [x] Issue 09 增加 dry-run trace 输出与 review 面

目标：

1. 让仓库内可以基于 execution plan 运行一条 deterministic dry-run。
2. 让 dry-run 不连接真实 provider，不产生外部 side effect。
3. 让 trace 能审查 workflow node 顺序、依赖满足情况、capability mock 使用情况与 return summary。

关键输出：

1. 最小 dry-run runner / trace 数据模型
2. mock capability binding 输入格式
3. `emit-dry-run-trace` 或等价 review 输出与 golden

退出标准：

1. 新贡献者可在本地跑通 package -> execution plan -> dry-run trace
2. dry-run runner 不需要重新读取 AST、raw source 或 authoring descriptor
3. runner failure 可区分 plan 错误、mock 缺失与 trace consistency 错误

### M3 Compatibility、Consumer Matrix 与扩展路径

状态：

- [x] Issue 10 冻结 Execution Plan / Dry-Run Trace compatibility 契约
- [x] Issue 11 更新 runtime-adjacent consumer matrix 与 contributor 扩展模板

目标：

1. 让 execution plan 与 dry-run trace 的格式演进不依赖隐含约定。
2. 让 future Native scheduler / launcher / audit consumer 清楚哪些字段可稳定依赖。
3. 把新增 plan / dry-run 输出纳入 backend / consumer 扩展规则。

关键输出：

1. execution plan compatibility / versioning 文档
2. dry-run trace compatibility / versioning 文档
3. 更新后的 consumer matrix 与 contributor guidance

退出标准：

1. downstream consumer 清楚 handoff package、execution plan、dry-run trace 的兼容层次
2. 新 runtime-adjacent consumer 不需要绕开 handoff / plan / trace 公共边界
3. future runtime work 有明确升级路径，而不是从 CLI 输出倒推语义

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.6 execution plan / dry-run regression、CI 与 reference 文档闭环

目标：

1. 让 execution plan、dry-run trace、mock capability 与 compatibility 进入持续回归。
2. 让新贡献者能按文档跑通“author package -> emit execution plan -> dry-run trace -> review”的 V0.6 路径。

关键输出：

1. 更新后的 `ctest` / CI 覆盖
2. 更新后的 reference 文档与 contributor guidance

退出标准：

1. V0.6 新增 plan / dry-run 正例、负例、golden 与 compatibility 回归都进入 CI
2. 新贡献者可按文档跑通完整的 V0.6 local dry-run bootstrap 路径

## 关键依赖顺序

1. 先完成 M0，再实现 execution plan 模型
2. 先稳定 execution plan，再增加 dry-run runner
3. 先冻结 mock capability 输入边界，再承诺 deterministic trace
4. 先冻结 compatibility / consumer matrix，再对后续 runtime work 承诺长期消费
5. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 execution plan artifact 未冻结前直接实现 runner 私有结构
2. 不要把真实 connector / secret / endpoint 配置塞进 `ahfl.package.json`
3. 不要让 dry-run runner 回退读取 AST、raw source 或 project descriptor 猜执行顺序
4. 不要把 V0.6 的实现目标偷换成生产 runtime / scheduler / launcher 开发

## 当前状态

V0.6 已完成 M0、M1、M2、M3、M4：当前仓库已建立 execution plan / dry-run 的 direct tests、golden、CLI 路径、compatibility 文档、consumer matrix、contributor guide 与 CI 标签切片。V0.6 的下一步适合切入 V0.7 或回到实现更深层 runtime prototype。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.6.zh.md](./issue-backlog-v0.6.zh.md)
