# AHFL V0.5 Issue Backlog

本文将 V0.5 roadmap 拆成可直接复制为 GitHub issue 的任务文本。

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
冻结 V0.5 的 package authoring / consumer bootstrap scope、成功标准与 runtime 非目标

背景：
V0.4 已把 Native handoff contract 收口为稳定 package 输出，但当前仓库还没有说明下一阶段究竟是要“开始写 runtime”，还是要先把 package authoring 与 consumer bootstrap 做成正式边界。

目标：

1. 明确 V0.5 仍不直接进入 runtime implementation
2. 明确 V0.5 的成功标准是 package authoring 与 reference consumer bootstrap
3. 明确 launcher / scheduler / deployment 继续留在后续阶段

验收标准：

1. roadmap 与 backlog 使用同一组 package authoring / consumer bootstrap 术语
2. 文档显式区分 authoring boundary 与 runtime implementation
3. 后续 issue 不再把 runtime 私有实现混入 V0.5

当前实现备注：

1. `docs/plan/roadmap-v0.5.zh.md` 与 `docs/plan/issue-backlog-v0.5.zh.md` 已统一使用 package authoring、consumer bootstrap、runtime-adjacent consumer 等术语
2. V0.5 roadmap 已明确当前阶段不直接进入 runtime launcher、scheduler、connector 或 deployment 实现
3. V0.5 backlog 已把后续实现主线收束到 metadata authoring、debug/review 与 reference consumer bootstrap

依赖：
无

主要涉及文件：

- `docs/plan/roadmap-v0.5.zh.md`
- `docs/plan/issue-backlog-v0.5.zh.md`
- `docs/design/native-handoff-architecture-v0.4.zh.md`

## [x] Issue 02

标题：
冻结 package metadata 的 authoring model 与输入归属

背景：
V0.4 已明确 package metadata 属于 handoff / runtime-facing 边界，但尚未决定这些 metadata 应如何被 author、从哪一层输入、如何与 project descriptor 保持解耦。

目标：

1. 明确 package metadata authoring 是独立 descriptor、受限 project `package` 区段，还是其他统一输入形式
2. 明确 project identity、package identity、deployment identity 的职责拆分
3. 明确 entry/export target 与 capability binding alias 的最小 authoring 形态

验收标准：

1. 存在单独的 V0.5 authoring 设计文档
2. 文档显式写出允许与不允许进入 authoring 边界的信息
3. 后续 frontend / CLI / validator 任务可直接对齐该边界

当前实现备注：

1. 已新增 `docs/design/native-package-authoring-architecture-v0.5.zh.md`
2. 文档已冻结独立 `ahfl.package.json` 作为 V0.5 package metadata authoring 的正式输入来源
3. 文档已明确 `ahfl.project.json` 不内联 package metadata，`ahfl.workspace.json` 不选择 package，也不承载 deployment 配置
4. 文档已冻结 package identity、entry/export target、capability binding key 的最小 authoring 形态，并为 Issue 04+ 的 frontend / CLI / validation 接入提供边界

依赖：

- Issue 01

主要涉及文件：

- `docs/design/native-package-metadata-v0.4.zh.md`
- `docs/design/project-descriptor-architecture-v0.3.zh.md`
- `docs/design/native-package-authoring-architecture-v0.5.zh.md`
- `docs/plan/roadmap-v0.5.zh.md`

## [x] Issue 03

标题：
冻结 Package Reader / Execution Planner 参考 consumer 的最小边界

背景：
当前 `emit-native-json` 已可被未来 consumer 使用，但仓库内尚未冻结“参考 consumer 到底应该验证哪条最小路径”，这会让后续 package review、planner bootstrap 与 consumer prototype 容易各自发展。

目标：

1. 明确仓库内 reference Package Reader 的职责
2. 明确仓库内 reference Execution Planner bootstrap 的职责
3. 明确这两类 consumer 仍不承诺哪些 runtime 语义

验收标准：

1. consumer matrix 或设计文档新增 V0.5 bootstrap 边界
2. 文档显式写出 reference consumer 依赖的稳定字段
3. 后续实现任务可以直接对齐到该边界

当前实现备注：

1. 已新增 `docs/design/native-consumer-bootstrap-v0.5.zh.md`
2. 文档已冻结 reference Package Reader 与 reference Execution Planner Bootstrap 的职责边界
3. 文档已明确两类 reference consumer 只能消费 emitted handoff package 或 `handoff::Package` 对象，不能回退读取 AST、raw source、project descriptor 或 `ahfl.package.json`
4. 文档已列出 Package Reader / Execution Planner Bootstrap 可稳定依赖的 handoff package 字段，并明确不承诺 scheduler、connector、deployment、retry、timeout 或完整执行语义

依赖：

- Issue 01
- Issue 02

主要涉及文件：

- `docs/reference/native-consumer-matrix-v0.4.zh.md`
- `docs/design/native-handoff-architecture-v0.4.zh.md`
- `docs/design/native-consumer-bootstrap-v0.5.zh.md`
- `docs/plan/roadmap-v0.5.zh.md`

## [x] Issue 04

标题：
引入 package metadata descriptor / authoring 数据模型

背景：
如果 V0.5 只停留在 authoring 设计说明而没有正式对象模型，后续 CLI、frontend、validation 与 regression 仍会各自拼接 metadata。

目标：

1. 建模 package metadata authoring 输入
2. 建模 entry/export target 与 capability binding alias 的最小 authoring 对象
3. 保持 single-file、project、workspace 路径可复用同一输入模型

验收标准：

1. 新增稳定的 authoring 数据结构
2. frontend / handoff 后续阶段不直接依赖 authoring 解析细节
3. authoring 输入与 emitted package metadata 的关系清晰

当前实现备注：

1. `include/ahfl/frontend/frontend.hpp` 已新增 `PackageAuthoringDescriptor`、`PackageAuthoringTarget`、`PackageAuthoringCapabilityBinding` 及其 parse result 对象
2. `src/frontend/project.cpp` 已新增 `load_package_authoring_descriptor(...)`，并落地 `ahfl.package.json` 的最小 JSON 解析与结构校验
3. 当前 authoring 数据模型已明确区分 package identity、entry target、export targets 与 capability binding key
4. `tests/project/project_parse.cpp` 与 `tests/project/package_authoring_*/ahfl.package.json` 已固定该对象模型的正例与负例边界
5. 该阶段仍停在 descriptor 解析与模型冻结，不直接依赖 semantic model 做 target / capability 引用校验

依赖：

- Issue 02

主要涉及文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `docs/design/native-package-authoring-architecture-v0.5.zh.md`

## [x] Issue 05

标题：
扩展 frontend 与 CLI 消费 package metadata 输入

背景：
只有数据模型还不够；如果 `ahflc` 不能通过正式入口消费 package metadata，V0.5 的 authoring 路径仍然停留在内部对象。

目标：

1. 为 `ahflc` 增加 package metadata 输入路径
2. 保持现有单文件、`--project`、`--workspace --project-name` 路径可继续工作
3. 不让 CLI 直接吸收 metadata 解析与 handoff 拼装细节

验收标准：

1. CLI 可在 project-aware 模式下驱动 package metadata authoring 输入
2. CLI help、reference 与 diagnostics 与新入口一致
3. handoff emission 继续通过统一 driver / frontend / lowering 路径工作

当前实现备注：

1. `src/cli/ahflc.cpp` 已新增 `--package <ahfl.package.json>` 参数，并限制其只用于 `emit-native-json`
2. CLI 当前已支持 single-file、`--project`、`--workspace --project-name` 三条路径与 `--package` 组合
3. backend driver 与 native JSON emitter 已支持把 package authoring metadata 透传到 `handoff::lower_package(...)`
4. `tests/native/*.with_package.native.json`、`tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已固定 `emit-native-json --package` 的单文件 / project / workspace 回归
5. 当前阶段仍停在 descriptor-level metadata 透传，不直接基于 semantic model 校验 entry/export target 或 capability 引用

依赖：

- Issue 04

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `docs/reference/cli-commands-v0.5.zh.md`
- `docs/reference/project-usage-v0.5.zh.md`

## [x] Issue 06

标题：
增加 package identity / export target / capability binding authoring 校验

背景：
一旦 metadata 可被 author，就必须保证 package identity 冲突、entry/export target 非法引用、binding alias 冲突等问题能在编译阶段被稳定诊断。

目标：

1. 为 package identity 增加最小合法性校验
2. 为 entry/export target 与 capability binding authoring 增加 resolve / validate 边界
3. 让 diagnostics 能清楚区分 authoring 输入错误与 handoff lowering 错误

验收标准：

1. 新增 authoring 负例回归
2. diagnostics 可稳定区分 metadata 输入层与语义层错误
3. emitted package 不再悄悄吞掉明显的 authoring 冲突

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 与 `src/handoff/package.cpp` 已新增 `validate_package_metadata(...)`，统一承接 package authoring 的语义校验与规范化
2. 校验阶段现已支持把 display name 解析并规范化为 canonical name，同时拒绝 unknown target / capability、wrong kind，以及规范化后的 duplicate export target / capability binding
3. `src/cli/ahflc.cpp` 已把 package metadata 语义校验接入 `emit-native-json --package` 流水线，并在成功后以规范化 metadata 继续发射
4. `tests/handoff/package_model.cpp`、`tests/project/workflow_value_flow/*.package.json` 与 `tests/cmake/*Tests.cmake` 已新增 display-name 正例与语义负例回归，覆盖 handoff 层与 CLI 集成路径

依赖：

- Issue 04
- Issue 05

主要涉及文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `include/ahfl/diagnostics.hpp`
- `src/handoff/package.cpp`
- `tests/`

## [x] Issue 07

标题：
增加 package-aware debug / review 输出

背景：
当前 review `emit-native-json` 主要依赖全量 JSON diff；对于 authoring 结果审阅、字段归因和贡献者调试来说，这个界面仍偏底层。

目标：

1. 提供 package-aware debug / review 命令或等价稳定输出
2. 显式呈现 package metadata、export target、binding slot 与关键 execution graph 摘要
3. 为 package authoring 回归增加更直接的检查面

验收标准：

1. 新输出可表达 authoring 输入与 emitted package 的关键对应关系
2. 不要求使用者阅读完整原始 JSON 才能完成审阅
3. 有正式 regression 覆盖其稳定输出边界

当前实现备注：

1. `src/backends/package_review.cpp` 与 `include/ahfl/backends/package_review.hpp` 已新增 `emit-package-review` 对应的 package-aware review 输出
2. review 输出已同时覆盖 package reader 视角与 workflow execution planner bootstrap 视角，并显式呈现 metadata、binding slot、policy / observation 与 execution graph 摘要
3. `src/cli/ahflc.cpp` 已新增 `emit-package-review` 子命令，并允许其与 `--package <ahfl.package.json>` 组合，复用现有 package metadata semantic validation
4. `tests/review/*.review`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已固定单文件 / project / workspace 的 review 输出回归
5. `docs/reference/cli-commands-v0.5.zh.md` 已补齐 V0.5 CLI 参考，并明确 `emit-package-review` 的边界与用法

依赖：

- Issue 05
- Issue 06

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `include/ahfl/handoff/package.hpp`
- `tests/`
- `docs/reference/cli-commands-v0.5.zh.md`

## [x] Issue 08

标题：
增加 reference Package Reader / Execution Planner bootstrap 路径

背景：
V0.4 已给出 consumer matrix，但仓库仍缺少一条“在本仓库内最小验证 consumer 如何接 package”的参考路径，这会让后续 runtime-adjacent consumer prototype 仍停留在口头层面。

目标：

1. 提供 reference Package Reader 或等价 reader/debug 路径
2. 提供 reference Execution Planner bootstrap 或等价 planner 输入规范化路径
3. 保持这两类 reference consumer 继续依赖 shared handoff model

验收标准：

1. reference consumer 可稳定读取当前 emitted package
2. 不需要重新扫描 AST / raw source / ad-hoc metadata
3. 有至少一组 golden / regression 固定其边界

当前实现备注：

1. `include/ahfl/handoff/package.hpp` 与 `src/handoff/package.cpp` 已新增 `build_package_reader_summary(...)`、`build_execution_planner_bootstrap(...)` 与 `build_entry_execution_planner_bootstrap(...)`
2. 这些 helper 当前直接消费 `handoff::Package`，并对 entry/export target、binding key、workflow node dependency 与 workflow target 类型做最小一致性检查
3. `src/backends/package_review.cpp` 已改为复用这些 direct handoff helper，而不是在 backend 内重复拼装 consumer 语义
4. `tests/handoff/package_model.cpp` 与 `tests/cmake/*Tests.cmake` 已新增 reference consumer 的 direct handoff 正例 / 负例回归
5. `docs/reference/native-handoff-usage-v0.5.zh.md` 与 `docs/reference/native-consumer-matrix-v0.5.zh.md` 已补齐 V0.5 reference consumer 的使用路径与矩阵说明

依赖：

- Issue 03
- Issue 07

主要涉及文件：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `src/backends/`
- `tests/`
- `docs/reference/native-handoff-usage-v0.5.zh.md`
- `docs/reference/native-consumer-matrix-v0.5.zh.md`

## [x] Issue 09

标题：
冻结 package metadata authoring 的 compatibility / migration 契约

背景：
一旦 metadata 有正式输入来源，兼容性问题就不再只发生在 emitted package，还会发生在 authoring 输入本身与“authoring -> emitted package”映射上。

目标：

1. 明确 authoring 输入的版本化与兼容扩展规则
2. 明确 emitted package compatibility 与 authoring compatibility 的关系
3. 为 future metadata 变更提供正式迁移流程

验收标准：

1. 存在单独的 authoring compatibility / migration 文档
2. 文档显式写出 breaking change 与迁移要求
3. 后续字段扩展有明确的 docs / code / golden / tests 同步流程

当前实现备注：

1. `docs/reference/native-package-authoring-compatibility-v0.5.zh.md` 已新增并冻结 authoring descriptor 的版本化、兼容扩展、breaking change 与迁移流程
2. `include/ahfl/frontend/frontend.hpp` 已新增 `ahfl::kPackageAuthoringFormatVersion` 作为 authoring `format_version` 的单一来源
3. `src/frontend/project.cpp` 与 `tests/project/project_parse.cpp` 已改为复用该常量，避免文档与实现分叉
4. `docs/reference/native-package-compatibility-v0.4.zh.md` 已显式区分 authoring compatibility 与 emitted package compatibility 的分层关系
5. `README.md` 与 `docs/README.md` 已更新 V0.5 authoring compatibility reference 入口

依赖：

- Issue 05
- Issue 06

主要涉及文件：

- `docs/reference/native-package-authoring-compatibility-v0.5.zh.md`
- `docs/reference/native-package-compatibility-v0.4.zh.md`
- `include/ahfl/frontend/frontend.hpp`
- `tests/`

## [x] Issue 10

标题：
验证 runtime-adjacent consumer prototype 的扩展路径与矩阵

背景：
V0.5 会引入新的 authoring 输入与 reference consumer；若不更新 consumer matrix 与扩展模板，后续新增 runtime-adjacent consumer 仍可能绕开公共路径。

目标：

1. 更新 runtime-facing consumer matrix
2. 验证 authoring 输入、emitted package 与 reference consumer 之间的受控扩展路径
3. 固化最小实现模板

验收标准：

1. 存在更新后的 consumer matrix
2. docs、实现和测试彼此一致
3. 后续新增 consumer prototype 时有可复用模板

当前实现备注：

1. `docs/reference/native-consumer-matrix-v0.5.zh.md` 已更新为 V0.5 consumer matrix，并显式列出 authoring 输入层、handoff 共享模型层、direct helper 层与 backend / CLI 输出层
2. `docs/design/backend-extension-guide-v0.2.zh.md` 已补充 V0.5 runtime-adjacent 扩展模板，明确 `emit-package-review` 与 direct handoff helper 的位置
3. `docs/reference/contributor-guide-v0.5.zh.md` 已新增，给出 V0.5 authoring / review / reference consumer 的最小落地路径
4. `docs/README.md` 与 `README.md` 已更新这些新 reference 文档入口
5. 当前实现、回归标签与文档入口已对齐 `v0.5-package-review`、`v0.5-reference-consumer` 等现有测试分层

依赖：

- Issue 03
- Issue 08
- Issue 09

主要涉及文件：

- `docs/reference/native-consumer-matrix-v0.5.zh.md`
- `docs/design/backend-extension-guide-v0.2.zh.md`
- `docs/reference/contributor-guide-v0.5.zh.md`
- `src/backends/`

## [x] Issue 11

标题：
建立 V0.5 authoring / consumer bootstrap regression 与 CI 覆盖

背景：
package metadata authoring 与 reference consumer 一旦进入仓库，如果没有分层回归与 CI 覆盖，很快会在输入兼容性、debug 输出和 consumer bootstrap 上产生隐式回退。

目标：

1. 增加 authoring 输入的正例、负例与 golden
2. 让 package-aware debug / review 与 reference consumer bootstrap 进入 `ctest`
3. 保持失败时可区分 authoring、package、consumer bootstrap 层级

验收标准：

1. `tests/` 中存在明确的 V0.5 regression 分层
2. CI 会执行新增测试
3. 失败定位可以区分 authoring / package / consumer 层

当前实现备注：

1. `tests/cmake/LabelTests.cmake` 当前已把 V0.5 回归显式分成 `v0.5-package-authoring-model`、`v0.5-package-authoring-emission`、`v0.5-package-authoring-validation`、`v0.5-package-review`、`v0.5-reference-consumer`
2. `.github/workflows/ci.yml` 已新增 V0.5 authoring / review / reference consumer 切片执行步骤
3. `docs/design/testing-strategy-v0.5.zh.md` 已新增，并把 V0.5 回归层次、标签与 CI 执行方式冻结下来
4. 当前失败已可按 authoring model / validation / review / reference consumer 四层定位，而不是只依赖全量 `ctest`

依赖：

- Issue 06
- Issue 07
- Issue 08
- Issue 10

主要涉及文件：

- `tests/`
- `tests/CMakeLists.txt`
- `.github/workflows/ci.yml`
- `docs/design/testing-strategy-v0.5.zh.md`

## [x] Issue 12

标题：
补齐 V0.5 reference 与 contributor 文档

背景：
一旦引入 package metadata authoring 与 reference consumer bootstrap，现有 V0.4 reference 文档将不足以支撑新贡献者理解“怎么 author package、怎么 review、怎么接 consumer”。

目标：

1. 更新 project usage、CLI、handoff / native usage 文档
2. 更新 contributor 导向的 authoring / consumer bootstrap 指引
3. 保持 docs index、README 与 design/reference 文档一致

验收标准：

1. 新贡献者可按文档跑通典型的 V0.5 package authoring 流程
2. project usage / CLI / handoff / consumer 文档彼此不冲突
3. `docs/README.md` 与 `README.md` 已更新索引

当前实现备注：

1. 已新增 `docs/reference/project-usage-v0.5.zh.md`，把 project / workspace descriptor 与 `ahfl.package.json` 的职责边界、输入矩阵和典型命令串成一条完整 V0.5 路径
2. `docs/reference/cli-commands-v0.5.zh.md`、`docs/reference/native-handoff-usage-v0.5.zh.md`、`docs/reference/native-package-authoring-compatibility-v0.5.zh.md` 与 `docs/reference/contributor-guide-v0.5.zh.md` 已统一交叉引用到 V0.5 project usage 文档，避免继续回落到 V0.3 project 参考
3. `docs/README.md` 与 `README.md` 已更新索引入口，并显式把 V0.5 project / CLI / handoff / contributor 文档组成当前推荐阅读链路
4. 当前新贡献者已可按文档从 `ahfl.project.json` / `ahfl.workspace.json`、`ahfl.package.json`、`emit-native-json`、`emit-package-review` 一直走到 reference consumer helper 的最小接入路径

依赖：

- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `docs/reference/`
- `docs/design/`
- `docs/README.md`
- `README.md`
