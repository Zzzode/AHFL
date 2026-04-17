# AHFL V0.4 Issue Backlog

本文将 V0.4 roadmap 拆成可直接复制为 GitHub issue 的任务文本。

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
冻结 V0.4 的 Native handoff scope、成功标准与 runtime 非目标

背景：
V0.3 已把 Core 内部边界收口得足够稳定。如果没有新的 scope 文档，下一阶段很容易把“handoff contract”直接写成“runtime implementation backlog”。

目标：

1. 明确 V0.4 的主线是 Native handoff，而不是 runtime 落地
2. 明确 V0.4 的成功标准
3. 明确 runtime / host integration 继续留在后续阶段

验收标准：

1. roadmap 与 backlog 使用同一组 Native handoff 术语
2. 文档显式区分 handoff contract 与 runtime implementation
3. 后续 issue 不再把 launcher / scheduler / connector 实现混入 V0.4

当前实现备注：

1. `docs/plan/roadmap-v0.4.zh.md` 与 `docs/plan/issue-backlog-v0.4.zh.md` 已统一使用 Native handoff、handoff package、runtime-facing metadata 等术语
2. `docs/design/native-handoff-architecture-v0.4.zh.md` 已显式把 V0.4 定义为 handoff contract，而不是 runtime implementation
3. V0.4 backlog 已不再把 launcher / scheduler / connector 实现混入当前阶段

依赖：
无

主要涉及文件：

- `docs/plan/roadmap-v0.4.zh.md`
- `docs/plan/issue-backlog-v0.4.zh.md`
- `docs/design/native-handoff-architecture-v0.4.zh.md`

## [x] Issue 02

标题：
冻结 Native handoff package / execution descriptor 架构

背景：
当前 IR 已稳定，但它更像 compiler/backend consumer contract，而不是 runtime-facing handoff package。若不先冻结这层架构，后续 Native consumer 很容易直接绑死在当前 IR 私有细节上。

目标：

1. 明确 handoff package 与 IR 的关系
2. 明确 handoff package 至少应保留哪些结构
3. 明确 handoff package 不承诺哪些运行时细节

验收标准：

1. 存在单独的 V0.4 handoff 架构文档
2. 文档显式写出 handoff package 的保留与抽象边界
3. 后续实现任务可以直接对齐到该边界

当前实现备注：

1. 已新增 `docs/design/native-handoff-architecture-v0.4.zh.md`
2. 文档已明确 handoff package 建立在 validate 后的稳定 IR 之上，而不是 parser / raw source 之上
3. 文档已把 package identity、execution graph、capability surface、policy surface、restricted summary 等保留边界与 runtime 私有细节区分开

依赖：

- Issue 01

主要涉及文件：

- `docs/design/native-handoff-architecture-v0.4.zh.md`
- `docs/design/ir-backend-architecture-v0.2.zh.md`
- `docs/plan/roadmap-v0.4.zh.md`

## [x] Issue 03

标题：
冻结 capability binding 与 runtime-facing metadata 的最小边界

背景：
未来 Native runtime 一定需要承接 capability surface、entry/export target 与 package identity，但这些信息目前既不能散落在 CLI 局部变量里，也不能直接偷换成 deployment 配置。

目标：

1. 明确 capability binding metadata 属于哪一层
2. 明确 entry/export target 如何进入 handoff 边界
3. 明确 project identity、package identity、workspace selection 的关系

验收标准：

1. capability / package metadata 的责任边界清晰
2. project descriptor 不会重新吸收 runtime deployment 语义
3. 后续 package model 和 CLI 入口可直接复用该术语体系

当前实现备注：

1. 已新增 `docs/design/native-package-metadata-v0.4.zh.md`
2. 文档已明确 project identity、package identity、deployment identity 三层边界
3. 文档已冻结 entry/export target 与 capability binding slot 的最小 metadata 约束，并明确这些信息不应退化为 deployment 配置
4. `docs/design/native-handoff-architecture-v0.4.zh.md` 已链接该文档，形成 V0.4 M0 的统一设计入口

依赖：

- Issue 01
- Issue 02

主要涉及文件：

- `docs/design/native-handoff-architecture-v0.4.zh.md`
- `docs/design/native-package-metadata-v0.4.zh.md`
- `docs/design/project-descriptor-architecture-v0.3.zh.md`
- `docs/plan/roadmap-v0.4.zh.md`

## [x] Issue 04

标题：
引入 handoff package / package metadata 数据模型

背景：
只有边界说明还不够；如果 handoff package 不落成稳定对象模型，后续 emission、compatibility 和测试都无法形成统一输入。

目标：

1. 建模 handoff package
2. 建模最小 package metadata
3. 保持 project-aware 编译路径与未来 handoff export 可组合

验收标准：

1. 新增稳定的 handoff package 数据结构
2. project descriptor 与 handoff package 的关系清晰
3. 后续 backend / CLI 不直接依赖 metadata 解析细节

当前实现备注：

1. 已新增独立 `handoff` 模块：`include/ahfl/handoff/package.hpp`、`include/ahfl/handoff.hpp`、`src/handoff/package.cpp`
2. `src/CMakeLists.txt` 已把 `handoff` 作为独立层接入，避免继续把 runtime-facing package model 挤进 `ir` 或 `backends`
3. `ahfl::handoff::Package`、`PackageMetadata`、`PackageIdentity`、`ExecutableRef`、`CapabilityBindingSlot` 等稳定对象已落成公共数据模型
4. `ahfl::handoff::lower_package(const ir::Program &, PackageMetadata)` 已把 handoff package lowering 与后续 emitter/CLI 路径解耦
5. 已新增 `tests/handoff/package_model.cpp` 与 `ahfl.handoff.package.project_workflow_value_flow`，固定该对象模型的最小 regression 边界

依赖：

- Issue 02
- Issue 03

主要涉及文件：

- `include/ahfl/handoff/`
- `src/handoff/`
- `src/CMakeLists.txt`
- `tests/handoff/`
- `docs/design/native-handoff-architecture-v0.4.zh.md`

## [x] Issue 05

标题：
扩展 IR/backend，生成 runtime-facing handoff package

背景：
若 handoff package 只停留在内部对象，future Native consumer 仍没有正式编译输出可以对接。

目标：

1. 新增 runtime-facing emission backend 或等价导出路径
2. 让 handoff package 建立在 validate 后的统一边界上
3. 不让 CLI 直接承担 handoff object 拼装逻辑

验收标准：

1. 存在可运行的 handoff emission 命令
2. 该命令通过 driver / project-aware pipeline 接入
3. 输出边界具备 golden tests

当前实现备注：

1. 已新增 `emit-native-json` 命令，并接入 `BackendKind::NativeJson`
2. 已新增独立 backend 实现：`include/ahfl/backends/native_json.hpp`、`src/backends/native_json.cpp`
3. runtime-facing JSON 序列化继续建立在 `ahfl::handoff::lower_package(...)` 之上，没有把 handoff object 拼装逻辑塞回 CLI
4. 已新增 `tests/native/` golden，覆盖单文件 handoff emission

依赖：

- Issue 04

主要涉及文件：

- `include/ahfl/backends/`
- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

## [x] Issue 06

标题：
增加 project-aware handoff debug / emission 入口

背景：
V0.4 handoff 路径必须继承 V0.3 的 project-aware 能力，否则 descriptor/workspace 边界会在 runtime-facing 导出时断开。

目标：

1. 让 handoff export 支持 `--search-root`
2. 让 handoff export 支持 `--project`
3. 让 handoff export 支持 `--workspace --project-name`

验收标准：

1. 三条 project-aware 输入路径都能导出同一 handoff family
2. handoff debug 输出可进入 regression
3. 单文件与 project-aware 路径不会分叉成两套实现

当前实现备注：

1. `emit-native-json` 已支持 `--search-root`
2. `emit-native-json` 已支持 `--project`
3. `emit-native-json` 已支持 `--workspace --project-name`
4. `tests/CMakeLists.txt` 已新增对应 regression，固定单文件、project manifest、workspace 三条路径的统一输出边界

依赖：

- Issue 04
- Issue 05

主要涉及文件：

- `src/cli/ahflc.cpp`
- `tests/CMakeLists.txt`
- `tests/project/`

## [x] Issue 07

标题：
导出 workflow execution graph 与节点生命周期摘要

背景：
Native runtime-facing consumer 不应重新从 raw workflow AST 猜 DAG、启动依赖和节点生命周期边界。

目标：

1. 明确 workflow graph 在 handoff package 中的稳定形态
2. 导出节点依赖与生命周期摘要
3. 保持 execution graph 与现有 validate / IR 边界一致

验收标准：

1. handoff package 可直接表达 workflow DAG 与关键 lifecycle 信息
2. project-aware workflow 场景进入 golden
3. runtime-facing consumer 不需要重新回看 AST 猜依赖图

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 已新增 `WorkflowExecutionGraph`、`WorkflowDependencyEdge` 与 `WorkflowNodeLifecycleSummary`
2. `src/handoff/package.cpp` 已从 validated IR 稳定 lower 出 `entry_nodes`、`dependency_edges` 与每个 workflow node 的最小 lifecycle 摘要
3. `src/backends/native_json.cpp` 已把这些字段导出到 `emit-native-json`
4. `tests/handoff/package_model.cpp` 与 `tests/native/` goldens 已固定 project-aware workflow 场景的 graph/lifecycle 边界

依赖：

- Issue 05
- Issue 06

主要涉及文件：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `src/backends/`
- `tests/project/`

## [x] Issue 08

标题：
导出 contract obligation、capability dependency 与受限值流摘要

背景：
若 handoff package 不能表达 policy surface、capability surface 与最关键的值流摘要，future Native 仍需要自行拼接多套 consumer 逻辑。

目标：

1. 导出 runtime-facing contract / observation 边界
2. 导出 capability dependency surface
3. 导出受限 workflow value summary

验收标准：

1. handoff package 可稳定表达 policy / capability / value summary 三类信息
2. 文档显式写出哪些语义仍保持抽象
3. 对应 golden 覆盖正例与受限边界

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 已新增 package 级 `PolicyObligation`
2. `src/handoff/package.cpp` 已从 `ContractDecl`、workflow `safety` / `liveness` 与 `formal_observations` 稳定 lower 出 `policy_obligations`
3. capability dependency surface 继续由 `capability_binding_slots` 表达，且 `required_by_targets` 已覆盖 agent/workflow 两侧依赖
4. restricted workflow value summary 继续由 node `input_summary` 与 workflow `return_summary` 表达
5. `tests/handoff/package_model.cpp` 与 `tests/native/ok_expr_temporal.native.json` 已固定 policy / capability / value summary 三类边界

依赖：

- Issue 05
- Issue 07

主要涉及文件：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir_json.cpp`
- `src/backends/`
- `docs/design/native-handoff-architecture-v0.4.zh.md`

## [x] Issue 09

标题：
冻结 handoff package 的 compatibility / versioning 契约

背景：
runtime-facing package 一旦被 future Native/tooling 消费，就不能继续只靠“当前实现就是这样”维持稳定性。

目标：

1. 明确 handoff package 的版本标识
2. 明确兼容扩展与 breaking change 规则
3. 为后续 consumer 提供正式契约

验收标准：

1. 存在单独的 compatibility / versioning 文档
2. handoff emission 的稳定性承诺一致
3. 后续字段扩展有明确流程和测试要求

当前实现备注：

1. 已新增 `docs/reference/native-package-compatibility-v0.4.zh.md`
2. 文档已冻结 `ahfl.native-package.v1` 的版本标识、兼容扩展规则、breaking-change 规则与变更流程
3. `src/handoff/package.cpp` 当前会规范化 `PackageIdentity.format_version`，保证 package identity 与顶层 handoff 版本字段不分叉
4. `tests/handoff/package_model.cpp` 已固定 package identity 版本字段会被收敛到 `ahfl.native-package.v1`

依赖：

- Issue 05
- Issue 08

主要涉及文件：

- `docs/reference/`
- `include/ahfl/`
- `src/backends/`

## [x] Issue 10

标题：
验证 runtime-facing backend 扩展路径与 consumer matrix

背景：
handoff package 的出现会把 backend 架构从“compiler-oriented consumer”推向“runtime-facing consumer”。需要单独验证这条扩展路径没有绕开现有 driver / compatibility / docs 体系。

目标：

1. 建立 runtime-facing consumer matrix
2. 验证 handoff export 的扩展路径
3. 固化最小实现流程

验收标准：

1. 存在 runtime-facing capability / consumer matrix
2. docs、实现和测试彼此一致
3. 后续新增 handoff-adjacent consumer 时有可复用模板

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.4.zh.md`
2. 文档已把 runtime-facing consumer 冻结为 Package Reader、Execution Planner、Policy / Audit Consumer 三类
3. `docs/design/backend-extension-guide-v0.2.zh.md` 已补 runtime-facing backend 扩展路径说明，并把 `emit-native-json` 纳入参考实现
4. `docs/reference/backend-capability-matrix-v0.3.zh.md`、`docs/reference/contributor-guide-v0.3.zh.md` 与 `docs/README.md` 已同步接入 consumer matrix 入口

依赖：

- Issue 09

主要涉及文件：

- `docs/reference/`
- `docs/design/`
- `src/backends/`

## [x] Issue 11

标题：
建立 V0.4 handoff regression 与 CI 覆盖

背景：
runtime-facing handoff 一旦进入仓库，如果没有显式的测试切片和 CI 覆盖，很快会破坏其稳定 consumer 契约。

目标：

1. 增加 handoff export 的正例、负例和 golden
2. 让 package model、runtime-facing summary、compatibility 都进入 `ctest`
3. 保持失败时可区分 model、semantics、package、compatibility 层级

验收标准：

1. `tests/` 中存在明确的 V0.4 handoff regression 组
2. CI 会执行新增测试
3. 失败定位可以区分 package / compatibility / backend 层

当前实现备注：

1. `tests/CMakeLists.txt` 已把 V0.4 handoff regression 切成 `v0.4-package-model`、`v0.4-package-emission`、`v0.4-package-compat`
2. 已新增 `tests/handoff/package_compat.cpp`，把 compatibility / versioning 守卫从一般 package model 中拆出单独回归层
3. `.github/workflows/ci.yml` 已显式执行三组 V0.4 handoff regression，而不只依赖 full test
4. `docs/design/testing-strategy-v0.3.zh.md` 已同步记录 Native handoff 的测试形态、标签分层与 CI 要求

依赖：

- Issue 06
- Issue 08
- Issue 10

主要涉及文件：

- `tests/`
- `tests/CMakeLists.txt`
- `.github/workflows/`
- `docs/design/testing-strategy-v0.3.zh.md`

## [x] Issue 12

标题：
补齐 V0.4 reference 与 contributor 文档

背景：
handoff package、runtime-facing export 和 compatibility 契约一旦落地，现有 V0.3 reference 文档将不足以支撑新贡献者理解“Core 如何交付给 Native”。

目标：

1. 更新 project usage、CLI、handoff/reference 文档
2. 新增或更新 contributor 导向的 handoff / Native 指引
3. 保持 docs index、README 和 design/reference 文档一致

验收标准：

1. 新贡献者可按文档跑通典型 V0.4 handoff 用法
2. project usage / CLI / handoff / backend extension 文档彼此不冲突
3. `docs/README.md` 与 `README.md` 已更新索引

当前实现备注：

1. 已新增 `docs/reference/native-handoff-usage-v0.4.zh.md`
2. `docs/reference/project-usage-v0.3.zh.md`、`docs/reference/cli-commands-v0.3.zh.md`、`docs/reference/contributor-guide-v0.3.zh.md` 已补齐 V0.4 handoff 入口与贡献路径
3. `docs/design/native-handoff-architecture-v0.4.zh.md`、`docs/README.md` 与仓库根 `README.md` 已同步接入 V0.4 handoff reference 入口

依赖：

- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `docs/reference/`
- `docs/design/native-handoff-architecture-v0.4.zh.md`
- `docs/README.md`
- `README.md`
