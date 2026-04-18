# AHFL V0.6 Issue Backlog

本文将 V0.6 roadmap 拆成可直接复制为 GitHub issue 的任务文本。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

每个 issue 都包含：

1. 标题
2. 背景
3. 目标
4. 验收标准
5. 依赖
6. 主要涉及文件

## [x] Issue 01

标题：
冻结 V0.6 的 execution planning / local dry-run scope 与 runtime 非目标

背景：
V0.5 已完成 package authoring 与 reference consumer bootstrap，但下一阶段如果直接写 launcher / scheduler，容易把 production runtime 目标混进 compiler / handoff 仓库。V0.6 需要先明确“execution plan + local dry-run bootstrap”才是当前阶段边界。

目标：

1. 明确 V0.6 不交付生产 runtime launcher、真实 connector 或 deployment
2. 明确 V0.6 的成功标准是 execution plan artifact 与 deterministic local dry-run trace
3. 明确 handoff package、execution plan、dry-run trace 与 future runtime scheduler 的职责关系

验收标准：

1. roadmap 与 backlog 使用同一组 execution plan、dry-run、trace 术语
2. 文档显式区分 local dry-run bootstrap 与 production runtime implementation
3. 后续 issue 不把 connector、secret、deployment 或 scheduler 私有策略混入 V0.6

当前实现备注：

1. `docs/plan/roadmap-v0.6.zh.md` 与 `docs/plan/issue-backlog-v0.6.zh.md` 已统一把 V0.6 定义为 execution plan 与 local dry-run bootstrap，而不是 production runtime
2. `docs/design/native-execution-plan-architecture-v0.6.zh.md` 与 `docs/design/native-dry-run-bootstrap-v0.6.zh.md` 已显式区分 handoff package、execution plan、dry-run trace 与 future runtime 的职责
3. 当前 backlog 已把真实 connector、deployment、scheduler 优化与 persistence 保持在非目标之外

依赖：
无

主要涉及文件：

- `docs/plan/roadmap-v0.6.zh.md`
- `docs/plan/issue-backlog-v0.6.zh.md`
- `docs/design/native-consumer-bootstrap-v0.5.zh.md`
- `docs/reference/native-consumer-matrix-v0.5.zh.md`

## [x] Issue 02

标题：
冻结 Execution Plan artifact 的最小模型与版本边界

背景：
V0.5 的 `ExecutionPlannerBootstrap` 已能表示 workflow DAG 与 node lifecycle，但它仍是 helper 返回对象，不是可序列化、可 golden、可被 dry-run runner 消费的正式 artifact。

目标：

1. 定义 execution plan 的顶层 identity、format version 与 source package provenance
2. 定义 workflow plan、node plan、dependency edge、capability binding reference、input / return summary 的最小字段
3. 明确 execution plan 与 emitted handoff package 的兼容关系

验收标准：

1. 存在单独的 V0.6 execution plan 设计文档
2. 文档显式列出稳定字段与当前不承诺字段
3. 后续 model / emitter / runner issue 可直接按该边界实现

当前实现备注：

1. 已新增 `docs/design/native-execution-plan-architecture-v0.6.zh.md`
2. 文档已冻结 execution plan 的顶层 identity、workflow plan、node plan、dependency edge、binding reference 与 value summary 的最小边界
3. 文档已明确 execution plan 只从 `handoff::Package` / planner bootstrap helper 投影，不回退读取 AST、raw source 或 authoring descriptor

依赖：

- Issue 01

主要涉及文件：

- `docs/design/`
- `docs/reference/native-consumer-matrix-v0.5.zh.md`
- `include/ahfl/handoff/package.hpp`

## [x] Issue 03

标题：
冻结 dry-run input、mock capability binding 与 trace 输出边界

背景：
local dry-run 需要输入 workflow 初始值和 capability mock 结果，但这些输入不能被误解为真实 deployment、secret 或 connector 配置。

目标：

1. 定义 dry-run input descriptor 的最小形态
2. 定义 mock capability binding 与 deterministic result 的最小形态
3. 定义 dry-run trace 需要呈现的 node order、mock usage、dependency 与 return summary

验收标准：

1. 文档显式写出 dry-run input 允许和禁止包含的信息
2. mock capability 不包含 endpoint、auth、secret、tenant 或 region
3. trace 输出边界能支撑后续 golden 与 review

当前实现备注：

1. 已新增 `docs/design/native-dry-run-bootstrap-v0.6.zh.md`
2. 文档已冻结 DryRunInput、CapabilityMockSet 与 DryRunTrace 的最小边界，并明确 mock capability 不是 connector config
3. 文档已明确 local dry-run runner 只消费 execution plan、dry-run input 与 mock capability，不读取 AST、project descriptor 或 package authoring descriptor

依赖：

- Issue 01
- Issue 02

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `tests/`

## [x] Issue 04

标题：
引入 Execution Plan 数据模型

背景：
若没有正式数据模型，`emit-execution-plan`、dry-run runner 与 tests 会各自重建 planner bootstrap 语义。

目标：

1. 新增 execution plan object model
2. 从 `handoff::Package` / `ExecutionPlannerBootstrap` 构建 plan
3. 保持 plan model 不读取 AST、raw source 或 authoring descriptor

验收标准：

1. 新增稳定的 execution plan 数据结构
2. plan 构建只依赖 handoff package / bootstrap helper
3. 有 direct model 正例和基础负例回归

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 已新增 `ExecutionPlan`、`WorkflowPlan`、`WorkflowNodePlan` 与 `CapabilityBindingReference` 等数据模型
2. `src/handoff/package.cpp` 已新增 `build_execution_plan(...)`，并复用 `build_package_reader_summary(...)` 与 `build_execution_planner_bootstrap(...)` 构建 plan，而不是重新扫描 AST 或 raw source
3. `tests/handoff/package_model.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 execution plan 的 direct 正例 / 负例回归与 `v0.6-execution-plan-model` 标签

依赖：

- Issue 02
- Issue 03

主要涉及文件：

- `include/ahfl/handoff/`
- `src/handoff/`
- `tests/handoff/`

## [x] Issue 05

标题：
增加 `emit-execution-plan` 输出路径

背景：
execution plan 若只停留在内部对象，贡献者和下游 consumer 仍无法稳定审查或集成。

目标：

1. 新增 CLI/backend 输出 execution plan JSON 或等价稳定文本
2. 支持 single-file、`--project`、`--workspace --project-name` 与 `--package`
3. 复用现有 frontend / driver / handoff 路径，不把 plan 构建塞进 CLI

验收标准：

1. `ahflc emit-execution-plan ...` 或等价命令可输出稳定 plan
2. 输出覆盖 package identity、workflow plan、node dependencies、capability binding 与 value summary
3. 有 single-file / project / workspace golden

当前实现备注：

1. `src/cli/ahflc.cpp` 已新增 `emit-execution-plan` 子命令，并允许其与 `--package <ahfl.package.json>` 组合
2. `include/ahfl/backends/execution_plan.hpp` 与 `src/backends/execution_plan.cpp` 已新增 execution plan JSON 输出 backend，复用 `build_execution_plan(...)` 而不是在 CLI 内重建 plan 语义
3. `src/backends/driver.cpp` 与 `src/backends/CMakeLists.txt` 已把 Execution Plan backend 接入统一 backend driver
4. `tests/plan/ok_workflow_value_flow.with_package.execution-plan.json`、`tests/plan/project_workflow_value_flow.with_package.execution-plan.json`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已固定 single-file / project / workspace 的 execution plan 输出回归与 `v0.6-execution-plan-emission` 标签
5. 已验证 `ctest --preset test-dev --output-on-failure -L 'v0.6-execution-plan-model|v0.6-execution-plan-emission'` 通过，覆盖 2 个 direct model 回归与 3 个 CLI emission golden

依赖：

- Issue 04

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/backends/`
- `src/backends/`
- `tests/`

## [x] Issue 06

标题：
增加 Execution Plan validation 与 golden 回归

背景：
plan 输出必须能区分 handoff package 错误、planner bootstrap 错误与 plan artifact 自身不一致，否则 dry-run runner 会在后续阶段承担过多诊断责任。

目标：

1. 校验 workflow entry、node target、dependency edge、binding key 与 value summary 一致性
2. 固定 plan artifact 的正例与负例 golden
3. 让 diagnostics 明确指向 plan construction / validation 层

验收标准：

1. plan validation 有 direct unit regression
2. CLI failure regression 能稳定匹配 diagnostic
3. 不合法 plan 不会被 dry-run runner 静默接受

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 与 `src/handoff/package.cpp` 已新增 `validate_execution_plan(...)`，覆盖 entry workflow、workflow/node 重复项、dependency edge / `after` 一致性、value summary 引用、capability binding 基本完整性与 cycle 检查
2. `build_execution_plan(...)` 现在会在产出 artifact 前执行 plan validation，把 diagnostics 明确归因到 execution plan validation 层
3. `src/cli/ahflc.cpp` 对 `emit-execution-plan --package ...` 路径已改为显式渲染 execution plan diagnostics，并在失败时返回非零退出码；同时修正 `emit-execution-plan` 未计入 action 互斥计数的问题
4. `tests/handoff/package_model.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 direct validation 正例、missing entry workflow / unknown value read 负例，以及 CLI agent-entry failure regression，并加入 `v0.6-execution-plan-validation` 标签
5. 已验证 `ctest --preset test-dev --output-on-failure -L 'v0.6-execution-plan-validation|v0.6-execution-plan-model|v0.6-execution-plan-emission'` 通过

依赖：

- Issue 04
- Issue 05

主要涉及文件：

- `include/ahfl/handoff/`
- `src/handoff/`
- `tests/handoff/`
- `tests/plan/`
- `tests/cmake/`

## [x] Issue 07

标题：
引入 local dry-run runner 数据模型

背景：
V0.6 需要证明 execution plan 可被最小 consumer 承接，但不应直接进入生产 runtime。local dry-run runner 应只消费 execution plan、mock input 与 deterministic mock results。

目标：

1. 新增 dry-run request / result / trace 数据结构
2. 定义 workflow node execution order 的 deterministic bootstrap 规则
3. 保持 runner 不执行真实 agent state machine 或 connector side effect

验收标准：

1. runner 数据模型不依赖 AST、raw source 或 project descriptor
2. runner 可对单 workflow plan 生成最小 trace
3. 有 direct runner 正例回归

当前实现备注：

1. `include/ahfl/dry_run/runner.hpp` 与 `src/dry_run/runner.cpp` 已新增 `DryRunRequest`、`DryRunNodeTrace`、`DryRunTrace`、`DryRunResult` 与 `run_local_dry_run(...)`
2. local dry-run 只消费 `handoff::ExecutionPlan` 与 request fixture；执行顺序按 workflow node 声明顺序上的 deterministic topological bootstrap 生成，不读取 AST、raw source 或 project descriptor
3. runner 在执行前会复用 `validate_execution_plan(...)`，并在 workflow 缺失或无法调度所有 node 时返回 dry-run 层 diagnostics
4. `tests/dry_run/runner.cpp`、`tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 project 正例与 missing workflow 负例，并加入 `v0.6-dry-run-model` 标签
5. 已验证 `ctest --preset test-dev --output-on-failure -L 'v0.6-dry-run-model'` 通过

依赖：

- Issue 06

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [x] Issue 08

标题：
增加 mock capability binding 与 deterministic result 输入

背景：
dry-run runner 需要模拟 capability result，但 mock 输入必须保持 deterministic，并且不能承诺真实 provider 行为。

目标：

1. 定义 mock capability descriptor / object model
2. 支持按 binding key 或 canonical capability name 提供 deterministic result
3. 诊断 missing mock、duplicate mock 与 unused mock

验收标准：

1. mock 输入有 parser / model 正例和负例
2. runner 缺少必要 mock 时失败
3. mock 输入不包含 secret、endpoint、tenant、region 或 provider SDK 配置

当前实现备注：

1. `include/ahfl/dry_run/runner.hpp`、`src/dry_run/mock.cpp` 与 `src/dry_run/runner.cpp` 已新增 `CapabilityMock`、`CapabilityMockSet`、`CapabilityMockSetParseResult` 与 `parse_capability_mock_set_json(...)`
2. mock descriptor 当前仅支持 `format_version` 与 `mocks[]`，单个 mock 仅允许 `capability_name` / `binding_key` 二选一，再加 `result_fixture` 与可选 `invocation_label`；其余字段会被拒绝，从而避免 secret、endpoint、tenant、region 或 provider SDK 配置渗入
3. local dry-run 现在要求 workflow 所需 capability binding 都能命中 mock，并会诊断 missing mock、duplicate selector 与 unused mock
4. `tests/dry_run/runner.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 mock set parser 正例、duplicate selector 负例，以及 dry-run missing/unused mock 回归，并加入 `v0.6-dry-run-mock-input` 标签
5. 已验证 `ctest --preset test-dev --output-on-failure -L 'v0.6-dry-run-model|v0.6-dry-run-mock-input'` 通过

依赖：

- Issue 03
- Issue 07

主要涉及文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/project.cpp`
- `include/ahfl/`
- `src/`
- `tests/`

## [x] Issue 09

标题：
增加 dry-run trace 输出与 review 面

背景：
dry-run 若只返回内部对象，就无法作为 contributor-facing 调试面，也无法被 regression/golden 稳定审查。

目标：

1. 新增 `emit-dry-run-trace` 或等价 CLI/backend 输出
2. 输出 node order、dependency satisfaction、mock usage、input / return summary
3. 让 trace 与 execution plan / package metadata 的对应关系可审查

验收标准：

1. trace 输出有稳定 golden
2. trace 能覆盖 project / workspace / package authoring 路径
3. trace 不包含真实 connector、secret 或 deployment 信息

当前实现备注：

1. `include/ahfl/backends/dry_run_trace.hpp` 与 `src/backends/dry_run_trace.cpp` 已新增 `DryRunTrace` JSON printer，输出 package identity、workflow、status、node order、binding/mock usage、input / return summary
2. `src/cli/ahflc.cpp` 已新增 `emit-dry-run-trace` 子命令，以及 `--capability-mocks`、`--input-fixture`、`--workflow`、`--run-id` 参数；CLI 会先构建/校验 execution plan，再执行 local dry-run 并输出 trace JSON
3. `tests/dry_run/*.mocks.json`、`tests/trace/*.dry-run-trace.json`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已固定 single-file / project / workspace 的 trace golden，并加入 `v0.6-dry-run-trace` 标签
4. trace 输出当前只允许 deterministic fixture 与 capability mock 元数据，不包含真实 connector、secret、tenant、region 或 deployment 信息
5. 已验证 `ctest --preset test-dev --output-on-failure -L 'v0.6-dry-run-trace'` 通过

依赖：

- Issue 07
- Issue 08

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/trace/`
- `tests/cmake/`

## [x] Issue 10

标题：
冻结 Execution Plan / Dry-Run Trace compatibility 契约

背景：
execution plan 和 dry-run trace 一旦进入仓库，就会被 future Native scheduler、audit consumer 或 integration tests 消费，必须有单独版本与迁移规则。

目标：

1. 明确 execution plan format version 与 breaking change 规则
2. 明确 dry-run trace format version 与 breaking change 规则
3. 明确 handoff package compatibility、plan compatibility、trace compatibility 的关系

验收标准：

1. 存在 V0.6 compatibility / versioning 文档
2. 文档写出兼容扩展、breaking change 与迁移要求
3. 后续字段扩展有 docs / code / golden / tests 同步流程

当前实现备注：

1. `docs/reference/execution-plan-compatibility-v0.6.zh.md` 已冻结 `ahfl.execution-plan.v1` 的版本入口、consumer 依赖边界、兼容扩展、breaking change 与迁移流程
2. `docs/reference/dry-run-trace-compatibility-v0.6.zh.md` 已冻结 `ahfl.dry-run-trace.v1` 的版本入口、trace 稳定边界、与 execution plan/mock set 的关系，以及 breaking-change 规则
3. `docs/README.md` 已把上述两份 compatibility 文档纳入 reference 索引
4. 当前文档已明确 handoff package、execution plan、dry-run trace 三层兼容关系，以及 docs / code / golden / tests 的同步要求

依赖：

- Issue 05
- Issue 09

主要涉及文件：

- `docs/reference/`
- `include/ahfl/`
- `tests/`

## [x] Issue 11

标题：
更新 runtime-adjacent consumer matrix 与 contributor 扩展模板

背景：
新增 execution plan 与 dry-run trace 后，V0.5 consumer matrix 只覆盖 package reader / planner bootstrap / review，不足以指导 future scheduler、runner 或 audit consumer。

目标：

1. 更新 consumer matrix，加入 execution plan consumer 与 dry-run trace consumer
2. 更新 backend extension guide 的 V0.6 runtime-adjacent 模板
3. 更新 contributor guide，说明改 plan / runner / trace 时的文件落点和验证命令

验收标准：

1. matrix 明确每类 consumer 可稳定依赖的字段
2. extension guide 明确不能绕开 handoff / plan / trace 公共层
3. contributor guide 给出最小验证清单

当前实现备注：

1. `docs/reference/native-consumer-matrix-v0.6.zh.md` 已把 execution plan consumer、local dry-run runner 与 dry-run trace consumer 纳入 runtime-adjacent 矩阵
2. `docs/design/backend-extension-guide-v0.2.zh.md` 已新增 V0.6 runtime-adjacent 扩展模板，明确 plan / runner / trace 必须走公共模型层，不能绕开 handoff / plan / trace 公共边界
3. `docs/reference/contributor-guide-v0.6.zh.md` 已新增 plan / runner / trace 的文件落点与最小验证清单
4. `docs/README.md` 已纳入 V0.6 matrix / contributor 文档索引

依赖：

- Issue 10

主要涉及文件：

- `docs/reference/`
- `docs/design/backend-extension-guide-v0.2.zh.md`
- `README.md`
- `docs/README.md`

## [x] Issue 12

标题：
建立 V0.6 execution plan / dry-run regression、CI 与 reference 文档闭环

背景：
execution plan、mock capability 和 dry-run trace 一旦进入仓库，如果没有标签切片和 CI 覆盖，很快会在 plan construction、runner behavior 与 trace output 上产生隐式回退。

目标：

1. 增加 execution plan 正例、负例与 golden
2. 增加 dry-run trace 正例、负例与 golden
3. 让 plan / dry-run / compatibility 进入 `ctest` 与 CI
4. 补齐 reference 文档与 docs index

验收标准：

1. `tests/` 中存在明确的 V0.6 regression 分层
2. CI 会显式执行新增 V0.6 标签
3. `docs/README.md` 与 `README.md` 已更新索引
4. 新贡献者可按文档跑通 V0.6 local dry-run bootstrap 路径

当前实现备注：

1. `tests/handoff/`、`tests/dry_run/`、`tests/plan/`、`tests/trace/` 与 `tests/cmake/LabelTests.cmake` 已形成 execution plan / dry-run 的 direct / golden / CLI 分层
2. `.github/workflows/ci.yml` 已显式新增 `v0.6-(execution-plan-model|execution-plan-emission|execution-plan-validation|dry-run-model|dry-run-mock-input|dry-run-trace)` 切片
3. `README.md`、`docs/README.md`、`docs/reference/contributor-guide-v0.6.zh.md` 与 `docs/reference/native-consumer-matrix-v0.6.zh.md` 已纳入 V0.6 local dry-run bootstrap 路径说明
4. 当前仓库已具备 package -> execution plan -> capability mock -> dry-run trace 的本地闭环

依赖：

- Issue 06
- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `tests/`
- `tests/cmake/`
- `.github/workflows/ci.yml`
- `docs/reference/`
- `docs/design/`
- `docs/README.md`
- `README.md`
