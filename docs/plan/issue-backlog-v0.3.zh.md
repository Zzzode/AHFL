# AHFL Core V0.3 Issue Backlog

本文将 V0.3 roadmap 拆成可直接复制为 GitHub issue 的任务文本。

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
冻结 AHFL Core V0.3 的 scope、成功标准与 Native handoff 边界

背景：
V0.2 已完整收口到 project-aware Core，但下一阶段如果没有明确 scope，很容易把“Core 继续深化”和“开始做 Native/runtime”混在一起。

目标：

1. 明确 V0.3 仍然属于 Core 路线
2. 明确 V0.3 的成功标准
3. 明确哪些能力继续留在 Native/runtime 方向

验收标准：

1. V0.3 roadmap 与 backlog 使用同一组术语
2. 文档显式区分 Core 深化与 Native handoff
3. 后续 issue 不再把 runtime 目标混入 V0.3

当前实现备注：

1. `docs/design/core-scope-v0.1.en.md` 已明确 Core 与 Native/runtime 的边界
2. `docs/plan/roadmap-v0.3.zh.md` 与 `docs/plan/issue-backlog-v0.3.zh.md` 已统一使用 Core 深化、project-aware、formal/export 等术语
3. V0.3 backlog 已不再把 runtime / Native 实现目标混入当前阶段

依赖：
无

主要涉及文件：

- `docs/plan/roadmap-v0.3.zh.md`
- `docs/plan/issue-backlog-v0.3.zh.md`
- `docs/design/core-scope-v0.1.en.md`

## [x] Issue 02

标题：
冻结 manifest / workspace 的最小 project model

背景：
V0.2 的 project-aware 装载依赖 entry file 和 search root，已经可用，但对于更稳定的项目入口和贡献者心智模型仍然偏底层。

目标：

1. 明确 manifest 是否存在、长什么样、表达哪些字段
2. 明确 workspace 与单 project 的关系
3. 明确 manifest 与当前 module-path/search-root 规则如何衔接

验收标准：

1. 存在单独的 V0.3 project descriptor 设计文档
2. manifest/workspace 不会绕开现有 module loading 规则
3. CLI、frontend、loader 对 project descriptor 的责任边界清晰

当前实现备注：

1. 已新增 `docs/design/project-descriptor-architecture-v0.3.zh.md`
2. 文档已明确 `manifest / workspace -> ProjectInput -> SourceGraph` 的分层链路
3. 文档已保留 V0.2 `--search-root` 兼容入口，并明确 descriptor 不覆盖 module loading / resolver 规则

依赖：

- Issue 01

主要涉及文件：

- `docs/plan/roadmap-v0.3.zh.md`
- `docs/design/module-loading-v0.2.zh.md`
- `docs/design/source-graph-v0.2.zh.md`
- `docs/design/repository-layout-v0.2.zh.md`

## [x] Issue 03

标题：
冻结 V0.3 formal semantic/export 的下一层边界

背景：
当前 `emit-smv` 已具备受限 contract export，但 flow statement semantics、workflow 值流和更细粒度执行语义仍保持抽象。

目标：

1. 选择 V0.3 formal 深化的下一层目标
2. 显式区分新增的保留语义与抽象语义
3. 明确 IR 和 backend 需要承接哪些新边界

验收标准：

1. formal backend 设计文档新增 V0.3 freeze 段落
2. 文档显式写出不承诺的语义
3. 后续实现任务可以直接对齐到该边界

当前实现备注：

1. `docs/design/formal-backend-v0.2.zh.md` 已新增 V0.3 freeze 段落
2. 文档已明确 flow summary 进入稳定 IR，但 statement/value execution semantics 仍保持抽象
3. `src/backends/smv.cpp` 当前只消费 `goto_targets` / `may_return` / `may_fallthrough` 控制摘要，未把赋值/调用/断言直接编码进 transition relation

依赖：

- Issue 01

主要涉及文件：

- `docs/design/formal-backend-v0.2.zh.md`
- `docs/design/ir-backend-architecture-v0.2.zh.md`
- `docs/plan/roadmap-v0.3.zh.md`

## [x] Issue 04

标题：
引入 manifest / workspace 数据模型

背景：
如果 project-aware 入口继续只围绕“入口文件 + search root”展开，项目结构和编译意图只能隐含在命令行里，难以扩展和回归。

目标：

1. 建模 project manifest 与 workspace
2. 为 frontend 提供比当前裸参数更稳定的 project 输入
3. 保持单文件与 V0.2 project-aware 兼容路径可共存

验收标准：

1. 新增稳定的 project descriptor 数据结构
2. 前端后续阶段不直接依赖 manifest 解析细节
3. 现有 source graph 模型仍可作为后续统一输入

当前实现备注：

1. `include/ahfl/frontend/frontend.hpp` 已新增 `ProjectDescriptor`、`WorkspaceDescriptor` 及其 parse result 对象
2. `src/frontend/frontend.cpp` 已实现最小 JSON descriptor 解析、路径规范化和 descriptor diagnostics
3. `parse_project(ProjectInput)` 仍然保持为 source graph 的统一下沉入口，未被 descriptor 直接绕过

依赖：

- Issue 02

主要涉及文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `include/ahfl/support/source.hpp`
- `docs/design/source-graph-v0.2.zh.md`

## [x] Issue 05

标题：
扩展 frontend 与 CLI 入口消费 manifest / workspace

背景：
仅有数据模型还不够；如果 CLI 仍然只暴露底层 search-root 参数，那么 V0.3 的 project model 仍然停留在内部对象。

目标：

1. 为 `ahflc` 增加 manifest/workspace 驱动的 project-aware 入口
2. 保持现有单文件与 V0.2 兼容参数可继续工作
3. 不让 CLI 直接吸收 loader 或 manifest 解析实现细节

验收标准：

1. `ahflc` 可在 manifest/workspace 模式下完成 `check` / `emit-*`
2. 现有 `--search-root` 路径继续保留或有明确迁移策略
3. CLI help、reference 和 diagnostics 与新入口一致

当前实现备注：

1. `src/cli/ahflc.cpp` 已支持 `--project <ahfl.project.json>`
2. `src/cli/ahflc.cpp` 已支持 `--workspace <ahfl.workspace.json> --project-name <name>`
3. 现有 `--search-root ... <entry.ahfl>` 兼容入口仍保留，新增 regression 已覆盖 `--project` 和 `--workspace`

依赖：

- Issue 04

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `docs/reference/cli-commands-v0.3.zh.md`

## [x] Issue 06

标题：
增加 project-aware AST / debug 输出

背景：
当前 project-aware 模式下只能 `dump-project`、`dump-types` 或看最终 IR/backend 输出；缺少面向 frontend 结构的项目级调试面。

目标：

1. 提供 project-aware `dump-ast` 或等价 AST/debug 输出
2. 让多文件 AST ownership、module/source 归属和 import 关系可视化
3. 为 project-aware frontend 回归增加更直接的检查面

验收标准：

1. 新输出能表达 declaration 所属 module/source
2. 不要求使用者阅读 parse tree 或内部调试日志
3. 有正式 regression 覆盖其稳定输出边界

当前实现备注：

1. `src/frontend/frontend.cpp` 已新增 `dump_project_ast_outline(const SourceGraph &, ...)`
2. `src/cli/ahflc.cpp` 已支持 `dump-ast` 与 `--search-root`、`--project`、`--workspace --project-name` 组合
3. `tests/project/project_check_ok.ast` 与对应 CLI regression 已固定 project-aware AST 输出边界

依赖：

- Issue 04
- Issue 05

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/frontend/ast.hpp`
- `src/frontend/frontend.cpp`
- `tests/`

## [x] Issue 07

标题：
扩展 flow statement 语义边界到下一层稳定 IR

背景：
当前 flow 中的 `let`、赋值、`assert` 等语句已被 typecheck/validate，但 formal/backend 侧仍主要消费受限控制结构与 observation。

目标：

1. 选择并冻结 V0.3 要保留的 flow statement 语义
2. 把相关信息纳入稳定 IR / JSON
3. 保持“不是完整执行语义”的整体立场

验收标准：

1. IR 节点与 JSON 输出具备新增稳定字段
2. backend 对新增信息的消费边界有单独文档
3. golden tests 覆盖正例和受限边界

当前实现备注：

1. `include/ahfl/ir/ir.hpp` 已为 `StateHandler` 新增稳定 `summary` 字段
2. `src/ir/ir.cpp` / `src/ir/ir_json.cpp` 已输出 `goto_targets`、`may_return`、`may_fallthrough`、`assigned_paths`、`called_targets`、`assert_count`
3. `src/backends/smv.cpp` 已改为消费 flow summary 的控制摘要，不再在 backend 内部重新推导 flow 控制流
4. `tests/ir/*`、`tests/formal/*` 与 project-aware golden 已覆盖该边界

依赖：

- Issue 03

主要涉及文件：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `src/backends/smv.cpp`

## [x] Issue 08

标题：
扩展 workflow 数据流 / 返回值相关边界

背景：
当前 workflow 的结构正确性和类型兼容性已可检查，但对输入表达式、返回值表达式和节点间值流的稳定表达仍然偏弱。

目标：

1. 冻结 workflow 数据流下一层正式边界
2. 让 typecheck / validate / IR 对该边界形成一致模型
3. 为 formal 或其他 backend 保留统一消费入口

验收标准：

1. workflow 输入与返回值相关边界有单独设计说明
2. project-aware regression 覆盖跨文件 workflow 数据流场景
3. 下游 backend 不需要重新解释 workflow value semantics 的来源

当前实现备注：

1. `include/ahfl/ir/ir.hpp` 已新增 `WorkflowValueRead`、`WorkflowExprSummary`，并为 `WorkflowNode::input_summary`、`WorkflowDecl::return_summary` 建立稳定边界
2. `src/ir/ir.cpp` / `src/ir/ir_json.cpp` 已稳定导出 workflow input / return 的 value-read 来源，显式区分 `workflow_input` 与 `workflow_node_output`
3. `src/backends/smv.cpp` 当前仍不直接消费 workflow value summary，但后续 backend/tooling 已不需要重新从 raw expr 猜测 value-source
4. 已新增 `tests/ir/ok_workflow_value_flow.ahfl`，并补齐对应 IR / JSON regression
5. 已新增 `tests/project/workflow_value_flow/` 与 `ahflc.emit_ir_json.project_manifest.workflow_value_flow`，覆盖 project-aware cross-file workflow value-flow 场景

依赖：

- Issue 03
- Issue 07

主要涉及文件：

- `include/ahfl/semantics/typecheck.hpp`
- `src/semantics/typecheck.cpp`
- `include/ahfl/semantics/validate.hpp`
- `src/semantics/validate.cpp`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`

## [x] Issue 09

标题：
冻结 IR 兼容性与版本化契约

背景：
V0.2 的 IR / JSON 已可被下游消费，但当前对版本变化、兼容字段和扩展策略仍缺少单独文档。

目标：

1. 明确 textual IR 与 JSON IR 的版本化边界
2. 明确哪些字段属于稳定契约，哪些是可扩展字段
3. 为后续 backend/tooling consumer 提供正式兼容说明

验收标准：

1. 存在单独的 IR compatibility / versioning 文档
2. `emit-ir` 与 `emit-ir-json` 的稳定性承诺一致
3. 后续字段扩展有明确流程和测试要求

当前实现备注：

1. 已新增 `docs/reference/ir-compatibility-v0.3.zh.md`，单独冻结 IR compatibility / versioning 契约
2. `docs/reference/ir-format-v0.3.zh.md` 已显式链接该契约文档，避免格式参考与兼容规则分离
3. `include/ahfl/ir/ir.hpp` 已引入 `ahfl::ir::kFormatVersion` 作为当前 `format_version` 的单一来源
4. 现有 textual IR / JSON IR golden 继续共享同一 `ahfl.ir.v1` 版本边界

依赖：

- Issue 07
- Issue 08

主要涉及文件：

- `docs/reference/ir-format-v0.3.zh.md`
- `docs/design/ir-backend-architecture-v0.2.zh.md`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir_json.cpp`

## [x] Issue 10

标题：
验证 backend 扩展路径与能力矩阵

背景：
当前仓库中的 backend 设计主要通过 `emit-smv` 得到验证，但这并不足以证明 backend driver、IR 边界和 extension guide 对新增 backend 同样成立。

目标：

1. 用一条受控的新增 backend 路径验证当前扩展模型
2. 建立 backend capability matrix
3. 固化 backend extension 的最小实现流程

验收标准：

1. 存在参考 backend 骨架或最小实现
2. backend capability matrix 可说明各 backend 消费哪些 IR 能力
3. backend extension 文档、实现和测试彼此一致

当前实现备注：

1. 已新增 `Summary` backend，代码位于 `include/ahfl/backends/summary.hpp` 与 `src/backends/summary.cpp`
2. `BackendKind`、driver、CLI、project-aware 路径与 golden 已完整接入 `emit-summary`
3. 已新增 `docs/reference/backend-capability-matrix-v0.3.zh.md`，明确 `emit-ir` / `emit-ir-json` / `emit-summary` / `emit-smv` 的能力差异
4. `docs/design/backend-extension-guide-v0.2.zh.md` 已把 `Summary` backend 作为当前最小参考实现
5. `tests/summary/*` 与 project-aware `emit-summary` regression 已固定该扩展路径

依赖：

- Issue 09

主要涉及文件：

- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `src/backends/`
- `docs/design/backend-extension-guide-v0.2.zh.md`

## [x] Issue 11

标题：
建立 V0.3 新能力的回归与 CI 覆盖

背景：
manifest/project/debug/formal 新能力一旦进入仓库，如果没有明确的测试形态和 CI 覆盖，很快会破坏现有稳定边界。

目标：

1. 增加 V0.3 新增能力对应的正例、负例和 golden
2. 让 manifest/workspace、project-aware debug、IR/backend 新边界都进入 `ctest`
3. 保持单文件与 V0.2 兼容路径回归可见

验收标准：

1. `tests/` 中存在明确的 V0.3 regression 组
2. CI 会执行新增测试
3. 失败时可区分 project model、semantics、IR 和 backend 层级

当前实现备注：

1. `tests/CMakeLists.txt` 已为 V0.3 主线能力增加 `ahfl-v0.3`、`v0.3-project-model`、`v0.3-project-debug`、`v0.3-semantics`、`v0.3-ir`、`v0.3-backend`、`v0.3-compat` 标签
2. `.github/workflows/ci.yml` 已显式执行 project / IR / backend 三组 V0.3 regression slice，然后继续执行全量 `ctest --preset test-dev --output-on-failure`
3. `docs/design/testing-strategy-v0.3.zh.md` 已把 V0.3 标签切片、CI 执行方式和目录级测试边界写成正式约定
4. `README.md` 与 `docs/README.md` 已同步暴露 V0.3 focused regression 的本地执行方式

依赖：

- Issue 05
- Issue 06
- Issue 07
- Issue 08
- Issue 10

主要涉及文件：

- `tests/`
- `tests/CMakeLists.txt`
- `.github/workflows/ci.yml`
- `docs/design/testing-strategy-v0.3.zh.md`

## [x] Issue 12

标题：
补齐 V0.3 的 reference 与贡献者文档

背景：
V0.3 一旦引入 manifest/workspace、project-aware debug 和 backend extension 路径，现有 reference 文档将不足以支撑新贡献者理解和使用。

目标：

1. 更新 project usage、CLI、IR reference 文档
2. 新增或更新贡献者导向的扩展指南
3. 保持 docs index、README 和 design/reference 文档一致

验收标准：

1. 新贡献者可按文档跑通典型 V0.3 用法
2. project usage / CLI / IR / backend extension 文档彼此不冲突
3. `docs/README.md` 与 `README.md` 已更新索引

当前实现备注：

1. 已新增 `docs/reference/project-usage-v0.3.zh.md`、`docs/reference/cli-commands-v0.3.zh.md`、`docs/reference/ir-format-v0.3.zh.md`，把当前 project-aware / descriptor / IR 边界统一到 V0.3 版本命名
2. 已新增 `docs/reference/contributor-guide-v0.3.zh.md`，把常见改动入口、文件落点和最小验证流程整理成贡献者指引
3. `docs/README.md`、`README.md`、`project-descriptor` / `CLI pipeline` / `IR backend` / `backend capability matrix` / `IR compatibility` 等交叉引用已统一切到当前 V0.3 reference 文档
4. `docs/design/backend-extension-guide-v0.2.zh.md` 继续保留为现有扩展设计说明，并与新的 V0.3 reference / contributor 文档共同构成当前 contributor 文档集

依赖：

- Issue 05
- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `docs/reference/`
- `docs/design/backend-extension-guide-v0.2.zh.md`
- `docs/README.md`
- `README.md`
