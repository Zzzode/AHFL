# AHFL V0.7 Issue Backlog

本文给出与 [roadmap-v0.7.zh.md](./roadmap-v0.7.zh.md) 对齐的 issue-ready backlog。目标是把 V0.6 的 execution plan / dry-run 路径推进为 runtime session + execution journal bootstrap，而不是直接进入生产 runtime。

## [x] Issue 01

标题：
冻结 V0.7 runtime session / execution journal scope 与 runtime 非目标

背景：
V0.6 已完成 planning 与 dry-run 边界，如果下一阶段直接写 launcher / scheduler，很容易再次把生产 runtime 目标混进 compiler / handoff 仓库。

目标：

1. 明确 V0.7 不交付生产 launcher、真实 connector 或 deployment
2. 明确 session / journal 与 dry-run trace 的职责关系
3. 明确 future scheduler / replay / audit consumer 的落点

验收标准：

1. roadmap 与 design 文档对 V0.7 scope 一致
2. session / journal 与 future runtime 的边界清晰
3. backlog 不把 deployment / connector / persistence 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. `docs/plan/roadmap-v0.7.zh.md` 已明确 V0.7 聚焦 runtime session + execution journal bootstrap，而不是生产 runtime
2. `docs/design/native-runtime-session-bootstrap-v0.7.zh.md` 已明确 session / journal 与 future launcher / scheduler / replay consumer 的边界
3. 当前 backlog 已把 connector、deployment、parallel scheduling 与 persistence 保持在非目标之外

## [x] Issue 02

标题：
冻结 Runtime Session artifact 的最小模型与版本边界

背景：
若没有正式 session 模型，future scheduler prototype、journal 与 tests 会各自重建 runtime state 语义。

目标：

1. 定义 `RuntimeSession` 顶层结构
2. 定义 session 中 node state、workflow state 与 mock usage 的最小字段
3. 冻结 session format version 的单一来源

验收标准：

1. session artifact 有明确最小字段集合
2. session 与 execution plan / dry-run trace 的关系明确
3. 后续实现有单一 `format_version` 来源

主要涉及文件：

- `docs/design/`
- `include/ahfl/`

当前实现备注：

1. `docs/design/native-runtime-session-bootstrap-v0.7.zh.md` 已冻结 Runtime Session 的最小输入与状态边界
2. 当前文档已明确 session 至少包含 workflow status、node state、dependency satisfaction 与 mock usage 摘要
3. 当前设计已要求后续实现提供单一 `format_version` 来源

## [x] Issue 03

标题：
冻结 Execution Journal 的最小事件集合与 replay 边界

背景：
journal 若没有正式事件集合，future replay / audit consumer 会直接依赖 trace golden，导致责任层次混乱。

目标：

1. 定义最小 event kind 集合
2. 定义 event 中 workflow / node / execution index / mock usage 的最小字段
3. 明确 journal 与 trace 的职责拆分

验收标准：

1. journal 事件集合有单独设计说明
2. replay / audit consumer 的依赖边界明确
3. trace 不再被误用为 runtime event log

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-execution-journal-v0.7.zh.md` 已冻结最小 event kind 集合：`session_started`、`node_became_ready`、`node_started`、`node_completed`、`workflow_completed`
2. 当前文档已明确 journal 与 trace 的职责拆分：journal 面向 replay/audit，trace 面向 review
3. 当前设计已明确 journal 不承诺 host log、真实 payload 或 deployment 信息

## [x] Issue 04

标题：
引入 Runtime Session 数据模型

背景：
V0.7 需要把 execution plan 提升为 scheduler-ready state surface，但当前仓库只有 review-oriented dry-run trace。

目标：

1. 新增 `RuntimeSession`、workflow status、node session state 等数据结构
2. 让 session 只消费 execution plan / mock / request
3. 保持模型不依赖 AST、raw source 或 project descriptor

验收标准：

1. 有 direct session model regression
2. session 模型不回退消费编译期输入
3. session failure 不会静默吞掉 plan validation 错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/runtime_session/session.hpp` 已新增 `RuntimeSession`、workflow/node status、mock usage 与 `RuntimeSessionOptions`
2. `src/runtime_session/session.cpp` 已新增 `build_runtime_session(...)`，以 `ExecutionPlan + CapabilityMockSet + DryRunRequest` 构造 deterministic session snapshot
3. `tests/runtime_session/session.cpp` 已新增 direct regression，覆盖 project 正例、missing workflow 与 missing mock 失败路径

## [x] Issue 05

标题：
增加 Runtime Session validation 与 direct regression

背景：
session 一旦成为 future scheduler 输入，就必须能区分 plan 错误、mock 错误与 session 自身不一致。

目标：

1. 校验 workflow state、node state、dependency satisfaction 与 mock usage 一致性
2. 增加 session direct 正例 / 负例回归
3. 让 diagnostics 明确归属 session construction / validation 层

验收标准：

1. session validation 有 direct unit regression
2. 不合法 session 不会进入 journal bootstrap
3. diagnostics 能稳定匹配 failure 类型

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/runtime_session/session.hpp` 已新增 `validate_runtime_session(...)` 与独立 `RuntimeSessionValidationResult`
2. `src/runtime_session/session.cpp` 已新增 workflow/node state、execution_order、dependency satisfaction 与 mock usage 一致性校验，并在 `build_runtime_session(...)` 返回前执行自校验
3. `tests/runtime_session/session.cpp` 已新增 validation 正例、incomplete completed workflow 与 missing used mock 负例回归

## [x] Issue 06

标题：
增加 session snapshot 输出路径

背景：
如果 session 只停留在内部对象，下游 scheduler / audit consumer 仍无法稳定审查或集成。

目标：

1. 新增 `emit-runtime-session` 或等价输出
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 session golden

验收标准：

1. CLI 可输出稳定 session snapshot
2. 输出覆盖 workflow / node state / mock usage
3. 有 single-file / project / workspace golden

主要涉及文件：

- `src/cli/`
- `src/backends/`
- `tests/session/`
- `tests/cmake/`

当前实现备注：

1. `include/ahfl/backends/runtime_session.hpp` 与 `src/backends/runtime_session.cpp` 已新增 `RuntimeSession` JSON printer
2. `src/cli/ahflc.cpp` 已新增 `emit-runtime-session`，复用 `--package --capability-mocks --input-fixture [--workflow] [--run-id]`
3. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已新增 single-file / project / workspace session golden 回归

## [x] Issue 07

标题：
引入 execution journal 数据模型

背景：
session 解决状态问题，但 replay / audit 仍需要稳定 event sequence，而当前 trace 主要面向 review。

目标：

1. 新增 `ExecutionJournal` 与 journal event 模型
2. 定义 deterministic event ordering 规则
3. 保持 journal 不承诺生产 runtime log 语义

验收标准：

1. journal model 有 direct regression
2. event ordering 可 deterministic 回归
3. journal 不依赖 AST、raw source 或 project descriptor

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/execution_journal/journal.hpp` 已新增 `ExecutionJournal`、`ExecutionJournalEvent` 与稳定 `format_version`
2. `src/execution_journal/journal.cpp` 已新增 `validate_execution_journal(...)`，校验 event ordering、node phase 与 workflow completion 边界
3. `tests/execution_journal/journal.cpp` 已新增 direct regression，覆盖正例、node phase 乱序、execution index 回退与 workflow completed 后追加事件

## [x] Issue 08

标题：
增加 step-wise scheduler bootstrap 与 journal event 生成

背景：
journal 只有模型还不够，需要最小 step-wise bootstrap 把 session 投影为 ready/start/complete event sequence。

目标：

1. 基于 session 生成 deterministic event sequence
2. 覆盖 ready / start / complete 最小事件
3. 诊断 event consistency 错误

验收标准：

1. 有 direct journal bootstrap regression
2. event 序列与 session state 一致
3. failure 可区分 session 错误与 journal consistency 错误

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/execution_journal/journal.hpp` 已新增 `build_execution_journal(...)` 与 `ExecutionJournalResult`
2. `src/execution_journal/journal.cpp` 已新增 `RuntimeSession -> ExecutionJournal` 的 deterministic bootstrap，输出 `session_started`、node ready/start/complete 与 `workflow_completed`
3. `tests/execution_journal/journal.cpp` 已新增 bootstrap 正例与 invalid runtime session 负例，覆盖 session 错误与 journal bootstrap 路径

## [x] Issue 09

标题：
增加 journal 输出与 golden 回归

背景：
journal 若只返回内部对象，就无法作为 replay / audit / regression 稳定入口。

目标：

1. 新增 `emit-execution-journal` 或等价输出
2. 输出 workflow、node event、execution index、mock usage
3. 固定 journal golden

验收标准：

1. journal 输出有稳定 golden
2. 覆盖 project / workspace / package authoring 路径
3. 不包含真实 connector、secret 或 deployment 信息

主要涉及文件：

- `src/cli/`
- `src/backends/`
- `tests/journal/`
- `tests/cmake/`

当前实现备注：

1. `include/ahfl/backends/execution_journal.hpp` 与 `src/backends/execution_journal.cpp` 已新增 `ExecutionJournal` JSON printer
2. `src/cli/ahflc.cpp` 已新增 `emit-execution-journal`，复用 `--package --capability-mocks --input-fixture [--workflow] [--run-id]`
3. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已新增 single-file / project / workspace journal golden 回归

## [x] Issue 10

标题：
冻结 Runtime Session / Execution Journal compatibility 契约

背景：
session 与 journal 一旦进入仓库，就会被 future scheduler、replay 与 audit consumer 消费，必须有单独版本与迁移规则。

目标：

1. 明确 session format version 与 breaking change 规则
2. 明确 journal format version 与 breaking change 规则
3. 明确 package / plan / trace / session / journal 的兼容层次关系

验收标准：

1. 存在 V0.7 compatibility / versioning 文档
2. 文档写出兼容扩展、breaking change 与迁移要求
3. 后续字段扩展有 docs / code / golden / tests 同步流程

主要涉及文件：

- `docs/reference/`
- `include/ahfl/`
- `tests/`

当前实现备注：

1. `docs/reference/runtime-session-compatibility-v0.7.zh.md` 已冻结 `RuntimeSession` 的稳定字段、兼容扩展、breaking change 与变更流程
2. `docs/reference/execution-journal-compatibility-v0.7.zh.md` 已冻结 `ExecutionJournal` 的稳定字段、event ordering 边界与 breaking change 规则
3. 当前文档已明确 package / plan / session / journal / trace 的兼容层次与同步要求

## [x] Issue 11

标题：
更新 runtime-adjacent consumer matrix 与 contributor 扩展模板

背景：
新增 session 与 journal 后，V0.6 consumer matrix 只覆盖 package / plan / trace，不足以指导 future scheduler / replay / audit consumer。

目标：

1. 更新 consumer matrix，加入 session consumer 与 journal consumer
2. 更新 backend extension guide 的 V0.7 runtime-adjacent 模板
3. 更新 contributor guide，说明改 session / journal 时的文件落点和验证命令

验收标准：

1. matrix 明确每类 consumer 可稳定依赖的字段
2. extension guide 明确不能绕开 plan / session / journal 公共层
3. contributor guide 给出最小验证清单

主要涉及文件：

- `docs/reference/`
- `docs/design/backend-extension-guide-v0.2.zh.md`
- `docs/README.md`

当前实现备注：

1. `docs/reference/native-consumer-matrix-v0.7.zh.md` 已加入 runtime session consumer 与 execution journal consumer
2. `docs/reference/contributor-guide-v0.7.zh.md` 已补 session / journal 文件落点、CLI 命令与最小验证清单
3. `docs/README.md` 已把 V0.7 reference 文档加入当前索引

## [x] Issue 12

标题：
建立 V0.7 session / journal regression、CI 与 reference 文档闭环

背景：
session 与 journal 一旦进入仓库，如果没有标签切片和 CI 覆盖，很快会在 state surface、event ordering 与 output format 上产生隐式回退。

目标：

1. 增加 session 正例、负例与 golden
2. 增加 journal 正例、负例与 golden
3. 让 session / journal / compatibility 进入 `ctest` 与 CI
4. 补齐 reference 文档与 docs index

验收标准：

1. `tests/` 中存在明确的 V0.7 regression 分层
2. CI 显式运行 V0.7 session / journal 标签
3. docs index 可直接索引到 V0.7 compatibility / guide 文档

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `docs/reference/`
- `docs/README.md`

当前实现备注：

1. `tests/runtime_session/`、`tests/execution_journal/`、`tests/session/` 与 `tests/journal/` 已形成 direct model / bootstrap / emission 分层
2. `.github/workflows/ci.yml` 已显式加入 `v0.7-(runtime-session-.*|execution-journal-.*)` 回归步骤
3. `docs/README.md` 与 V0.7 reference 文档已形成可导航闭环
2. CI 会显式执行新增 V0.7 标签
3. `docs/README.md` 与 `README.md` 已更新索引
4. 新贡献者可按文档跑通 V0.7 local runtime bootstrap 路径

主要涉及文件：

- `tests/`
- `tests/cmake/`
- `.github/workflows/ci.yml`
- `docs/reference/`
- `docs/design/`
- `docs/README.md`
