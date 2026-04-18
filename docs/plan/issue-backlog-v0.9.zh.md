# AHFL V0.9 Issue Backlog

本文给出与 [roadmap-v0.9.zh.md](./roadmap-v0.9.zh.md) 对齐的 issue-ready backlog。目标是把 V0.8 已完成的 replay / audit consumer bootstrap，继续推进为 partial session 与 failure-path journal 的稳定边界，而不是直接进入生产 scheduler / persistence 系统。

## [x] Issue 01

标题：
冻结 V0.9 failure-path / partial session scope 与 scheduler prototype 非目标

背景：
V0.8 已完成 success-path 的 replay / audit 闭环，但当前 runtime-adjacent artifact 仍只覆盖完成态。若下一阶段直接去做私有 scheduler / recovery / host telemetry，仓库会再次把生产系统目标混入 compiler / handoff 边界。

目标：

1. 明确 V0.9 的成功标准是 partial session、failure-path journal 与 replay / audit failure 消费
2. 明确 V0.9 不交付生产 scheduler、persistence、checkpoint / resume 或 host log pipeline
3. 明确 failure-path 首先落在 session / journal，再进入 replay / audit

验收标准：

1. roadmap 与 backlog 使用同一组 failure-path / partial session 术语
2. 文档明确 richer scheduler prototype 只能建立在 plan / session / journal / replay 之上
3. backlog 不把 deployment、telemetry、persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. `docs/design/native-partial-session-failure-bootstrap-v0.9.zh.md` 已冻结 V0.9 的 scope：当前阶段只交付 partial session、failure-path journal 与 replay / audit failure 消费边界，不交付生产 scheduler、persistence、checkpoint / resume 或 host telemetry
2. `docs/plan/roadmap-v0.9.zh.md` 已明确 richer scheduler prototype 只能建立在 `plan -> session -> journal -> replay` 这条链路之上
3. backlog 当前已显式排除 deployment、telemetry、persistence、distributed runtime 等非目标

## [x] Issue 02

标题：
冻结 partial Runtime Session 的最小模型与 failure taxonomy

背景：
当前 `RuntimeSession` 只有 `Completed` workflow 与 `Blocked / Ready / Completed` node 状态，无法稳定承接未完成、失败、阻断或跳过的执行结果。

目标：

1. 定义 partial runtime session 顶层状态集合
2. 定义 per-node failure / blocked / skipped / terminal summary 的最小字段
3. 冻结 partial session `format_version` 的兼容入口与 failure taxonomy 边界

验收标准：

1. 文档明确 session 哪些字段是 failure-path 稳定输入
2. session 与 plan / journal / replay 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/runtime_session/session.hpp`

当前实现备注：

1. `docs/design/native-partial-session-failure-bootstrap-v0.9.zh.md` 已冻结 partial session 的最小状态集合，明确 workflow 向 `Completed / Failed / Partial` 演进、node 向 `Blocked / Ready / Completed / Failed / Skipped` 演进
2. 同一设计文档已区分 `Input Artifact Error`、`Bootstrap Construction Error`、`Runtime Result Failure` 与 downstream consistency failure，避免把 builder diagnostics 与 runtime result failure 混为一谈
3. `docs/reference/failure-path-compatibility-v0.9.zh.md` 已明确 failure-path 需要单独版本入口与 breaking-change 触发条件，作为后续 `RuntimeSession` 实现的版本化基线

## [x] Issue 03

标题：
冻结 failure-path journal / replay / audit 的分层与兼容边界

背景：
replay / audit 已在 V0.8 建立 success-path 稳定消费面；若 failure-path 没有先冻结层次关系，后续很容易在 replay / audit 或 CLI 层重复实现失败语义。

目标：

1. 明确 failure-path 先进入 `RuntimeSession` / `ExecutionJournal`
2. 明确 `ReplayView` 与 `AuditReport` 如何消费 failure-path
3. 明确 success-path 向 failure-path 扩展时哪些变化兼容，哪些需要 bump version

验收标准：

1. session / journal / replay / audit 的 failure 分层关系明确
2. compatibility 文档写出兼容扩展、breaking change 与迁移要求
3. 文档显式禁止回退依赖 host log、provider payload 或 source re-scan

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-partial-session-failure-bootstrap-v0.9.zh.md` 已冻结 `RuntimeSession`、`ExecutionJournal`、`ReplayView` 与 `AuditReport` 的 failure 分层关系，并显式要求 failure 先进入 session / journal
2. `docs/reference/failure-path-compatibility-v0.9.zh.md` 已写明哪些变化可视为兼容扩展，哪些新增状态 / 事件 / consistency 语义必须 bump version
3. 文档已显式禁止 replay / audit / CLI / 外部脚本回退依赖 AST、trace、host log 或 provider payload 重建 failure 语义

## [x] Issue 04

标题：
扩展 Runtime Session 数据模型与 validation 到 partial / failed 语义

背景：
当前 session model 只能表达完成态；future scheduler prototype 若要消费失败路径，必须先有稳定的 partial session 对象模型与 direct validation。

目标：

1. 扩展 `WorkflowSessionStatus` 与 `NodeSessionStatus`
2. 为 node / workflow failure 增加最小摘要字段
3. 增加 session direct validation，覆盖 partial / failed / blocked 场景

验收标准：

1. 有 direct session model / validation regression
2. partial / failed session 不回退依赖 trace 或 audit 结论
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/runtime_session/session.hpp`
- `src/runtime_session/session.cpp`
- `tests/runtime_session/session.cpp`

当前实现备注：

1. `include/ahfl/runtime_session/session.hpp` 已扩展 `WorkflowSessionStatus` 到 `Completed / Failed / Partial`，并把 `NodeSessionStatus` 扩展到 `Blocked / Ready / Completed / Failed / Skipped`
2. 同一头文件已新增 `RuntimeFailureKind` 与 `RuntimeFailureSummary`，为 workflow / node failure 提供最小摘要入口
3. `src/runtime_session/session.cpp` 已扩展 direct validation：区分 terminal workflow、partial workflow、executed node 与 non-executed node，并要求 failed workflow / node 携带稳定 failure summary
4. `tests/runtime_session/session.cpp` 已新增 partial workflow 正例、failed workflow 正例与缺失 failure summary 负例回归

## [x] Issue 05

标题：
增加 deterministic partial session bootstrap 与 failure 分类

背景：
只有模型还不够；仓库需要最小 bootstrap 把 execution plan、dry-run request 与 mock 输入投影为 partial / failed session。

目标：

1. 基于已有 deterministic dry-run 输入构建 partial session
2. 覆盖 mock missing、node failure、workflow failure 或等价最小 failure 分类
3. 区分 input artifact error、bootstrap construction error 与 runtime result failure

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. partial session 与 plan / request / mock set 保持一致
3. failure 诊断不会被 audit / replay 层吞掉

主要涉及文件：

- `src/runtime_session/session.cpp`
- `tests/runtime_session/session.cpp`
- `tests/dry_run/`

当前实现备注：

1. `src/runtime_session/session.cpp` 已将 `build_runtime_session(...)` 扩展为 deterministic success / partial / failed 三分支
2. capability mock `result_fixture` 以 `.pending` / `.partial` 结尾时会生成 partial session，当前 ready node 保持 `Ready`，下游保持 `Blocked`
3. capability mock 缺失会生成 `MockMissing` node failure 与 failed workflow；mock `result_fixture` 以 `.fail` / `.failed` 结尾时会生成 `NodeFailed` node failure 与 failed workflow
4. `tests/runtime_session/session.cpp` 已覆盖 missing mock failed session、pending mock partial session 与 node failure failed session

## [x] Issue 06

标题：
增加 partial session 输出路径与 golden 回归

背景：
partial session 若只停留在内部对象，下游 scheduler prototype、replay 与 CI 无法稳定消费失败路径状态。

目标：

1. 扩展 `emit-runtime-session` 输出 partial / failed 结果
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success-path 与 failure-path 的 session golden

验收标准：

1. CLI 可输出稳定 partial session 结果
2. 输出覆盖 workflow status、node final status、failure summary 与 mock usage
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/runtime_session.cpp`
- `tests/session/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`

当前实现备注：

1. `src/backends/runtime_session.cpp` 已输出 workflow / node `failure_summary`，并序列化 `partial / failed / skipped / ready / blocked` 状态
2. `tests/session/ok_alias_const.partial.with_package.runtime-session.json` 覆盖 single-file `--package` partial session golden
3. `tests/session/project_workflow_value_flow.failed.with_package.runtime-session.json` 覆盖 project `--package` failed session golden
4. `tests/session/project_workflow_value_flow.partial.with_package.runtime-session.json` 覆盖 workspace `--package` partial session golden
5. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已纳入 V0.9 runtime-session emission 回归

## [x] Issue 07

标题：
扩展 execution journal 数据模型与 validation 到 failure-path

背景：
当前 `ExecutionJournal` 只有 success-path 事件；replay / audit 若要稳定解释失败过程，必须先有正式 failure event family。

目标：

1. 定义 `node_failed`、`workflow_failed`、`mock_missing` 或等价最小失败事件
2. 扩展 event ordering、node phase 与 terminal workflow validation
3. 保持 success-path journal 仍可被兼容消费

验收标准：

1. 有 direct journal model / validation regression
2. failure-path journal 不会破坏既有 success-path 语义
3. diagnostics 能稳定匹配 event ordering 与 terminal-state 错误

主要涉及文件：

- `include/ahfl/execution_journal/journal.hpp`
- `src/execution_journal/journal.cpp`
- `tests/execution_journal/journal.cpp`

当前实现备注：

1. `ExecutionJournalEventKind` 已扩展 `mock_missing`、`node_failed` 与 `workflow_failed` failure event family
2. `ExecutionJournalEvent` 已增加 `failure_summary`，并复用 `RuntimeSession` 的最小 failure taxonomy
3. `validate_execution_journal(...)` 已覆盖 failure event kind / summary 对齐、node failed phase ordering、terminal workflow event 与失败节点一致性
4. `tests/execution_journal/journal.cpp` 已补 direct failure-path regression，包括 success-path 兼容、failure-path valid case、failed-before-start 与 missing-workflow-failed 诊断
5. `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已纳入 `v0.9-execution-journal-model` 标签切片

## [x] Issue 08

标题：
增加 failure-path journal bootstrap 与 replay / audit 消费升级

背景：
journal model 冻结后，还需要最小 bootstrap 把 partial session 投影为 failure event sequence，并升级 replay / audit 对失败路径的正式消费。

目标：

1. 基于 partial session 生成 deterministic failure-path journal
2. 升级 `build_replay_view(...)` 与 `build_audit_report(...)` 消费 failure-path
3. 区分 input artifact error、journal consistency error 与 replay consistency error

验收标准：

1. 有 direct journal bootstrap 与 replay / audit bootstrap 回归
2. replay / audit 与 source artifact 一致
3. failure 可稳定归因到 session、journal 或 replay 层

主要涉及文件：

- `src/execution_journal/journal.cpp`
- `src/replay_view/replay.cpp`
- `src/audit_report/report.cpp`
- `tests/execution_journal/journal.cpp`
- `tests/replay_view/replay.cpp`
- `tests/audit_report/report.cpp`

当前实现备注：

1. `build_execution_journal(...)` 已支持 completed / failed / partial 三类 session：completed 产出 `workflow_completed`，failed 产出 node failure event + `workflow_failed`，partial 只保留已执行前缀事件
2. `build_replay_view(...)` 已将 failure-path 作为 runtime result 投影：failed workflow 产出 `RuntimeFailed` replay status 与 workflow / node failure summary，partial workflow 产出 `Partial` replay status
3. replay consistency 已改为对齐“completed prefix”而非一律要求 full success order，避免把 runtime failure 误报成 journal consistency error
4. `build_audit_report(...)` 已把 runtime failure 归入 report conclusion，并把 trace / replay 比较从“全序一致”放宽为“executed prefix 一致”
5. `AuditJournalSummary` 已统计 failed node / workflow event，direct tests 已覆盖 failed / partial journal bootstrap、replay bootstrap 与 audit bootstrap
6. `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已纳入 `v0.9-execution-journal-bootstrap`、`v0.9-replay-view-model` 与 `v0.9-audit-report-bootstrap` 标签切片

## [x] Issue 09

标题：
增加 failure-path replay / audit 输出与 golden 回归

背景：
replay / audit 若不能输出稳定 failure-path 结果，CI 与 future tooling 仍会继续依赖分散的 direct tests 和人工拼接。

目标：

1. 扩展 `emit-replay-view` 与 `emit-audit-report` 输出 failure-path
2. 固定 failure-path replay / audit golden
3. 覆盖 single-file、project、workspace 与 `--package`

验收标准：

1. CLI 可输出稳定 failure-path replay / audit 结果
2. 输出覆盖 workflow failure、node progression、consistency 与审查结论
3. 有完整 golden 回归并纳入标签切片

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/replay_view.cpp`
- `src/backends/audit_report.cpp`
- `tests/replay/`
- `tests/audit/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

当前实现备注：

1. `src/backends/replay_view.cpp` 已输出 `runtime_failed / partial` replay status、workflow failure summary、node failed progression 与 node failure summary
2. `src/backends/audit_report.cpp` 已输出 `runtime_failed / partial` conclusion，以及 journal failed event 计数
3. success-path replay / audit golden 已随新增字段刷新，保持旧路径回归稳定
4. `tests/replay/ok_alias_const.partial.with_package.replay-view.json` 与 `tests/audit/ok_alias_const.partial.with_package.audit-report.json` 覆盖 single-file partial 路径
5. `tests/replay/project_workflow_value_flow.failed.with_package.replay-view.json` 与 `tests/audit/project_workflow_value_flow.failed.with_package.audit-report.json` 覆盖 project failed 路径
6. `tests/replay/project_workflow_value_flow.partial.with_package.replay-view.json` 与 `tests/audit/project_workflow_value_flow.partial.with_package.audit-report.json` 覆盖 workspace partial 路径
7. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已纳入 `v0.9-replay-view-emission` / `v0.9-audit-report-emission` 切片

## [x] Issue 10

标题：
冻结 partial session / failure-path journal / replay / audit compatibility 契约

背景：
一旦 failure-path 进入正式 artifact，就会被 future scheduler prototype、replay verifier 与 audit tooling 消费，必须有单独的版本与迁移规则。

目标：

1. 明确 session / journal / replay / audit 的 failure-path 兼容扩展规则
2. 明确哪些字段扩展仍兼容，哪些变更必须 bump version
3. 明确 V0.9 与 V0.8 success-path artifact 的兼容层次关系

验收标准：

1. 存在 V0.9 compatibility / versioning 文档
2. 文档写出扩展流程、breaking change 与 docs / code / golden / tests 同步要求
3. success-path 与 failure-path consumer 的依赖边界明确

主要涉及文件：

- `docs/reference/`
- `include/ahfl/runtime_session/session.hpp`
- `include/ahfl/execution_journal/journal.hpp`
- `include/ahfl/replay_view/replay.hpp`
- `include/ahfl/audit_report/report.hpp`

当前实现备注：

1. `include/ahfl/runtime_session/session.hpp`、`include/ahfl/execution_journal/journal.hpp`、`include/ahfl/replay_view/replay.hpp` 与 `include/ahfl/audit_report/report.hpp` 已将当前稳定 artifact 入口切换到 `ahfl.*.v2`
2. `docs/reference/runtime-session-compatibility-v0.9.zh.md` 已冻结 runtime session `v2` 的 partial / failed workflow、node failure summary 与 `v1` success baseline 迁移规则
3. `docs/reference/execution-journal-compatibility-v0.9.zh.md` 已冻结 execution journal `v2` 的 `mock_missing` / `node_failed` / `workflow_failed` event family 与 breaking-change 规则
4. `docs/reference/replay-view-compatibility-v0.9.zh.md` 与 `docs/reference/audit-report-compatibility-v0.9.zh.md` 已冻结 replay / audit 的 failure-aware status、summary、计数与 consumer 依赖边界
5. `docs/reference/failure-path-compatibility-v0.9.zh.md` 已从 M0 umbrella guidance 收口为 Issue 10 umbrella contract，明确 `v1` 是 historical success baseline、`v2` 是当前 V0.9 failure-path baseline
6. `README.md`、`docs/README.md` 与 `docs/plan/roadmap-v0.9.zh.md` 已同步指向 V0.9 compatibility 入口
7. `tests/session/`、`tests/journal/`、`tests/replay/` 与 `tests/audit/` golden 已随 `format_version` 切换到 `v2` 刷新

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 richer scheduler prototype guidance

背景：
新增 partial session 与 failure-path 后，当前 V0.8 matrix / guide 还只描述 success-path replay / audit 闭环，不足以指导下一阶段扩展。

目标：

1. 更新 consumer matrix，加入 failure-path / partial session 消费建议
2. 更新 contributor guide，说明改 session / journal / replay / audit failure-path 时的文件落点和验证命令
3. 补充 richer scheduler prototype 的推荐依赖顺序与反模式

验收标准：

1. matrix 明确各类 consumer 可稳定依赖的 failure-path 字段
2. contributor guide 给出最小验证清单与标签切片
3. 文档明确不能跳过 session / journal 直接在 replay / audit / CLI 层做私有失败语义

主要涉及文件：

- `docs/reference/native-consumer-matrix-v0.9.zh.md`
- `docs/reference/contributor-guide-v0.9.zh.md`
- `docs/README.md`
- `README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.9.zh.md`，把 partial session、failure-path journal、failure-aware replay / audit 与 richer scheduler prototype 的推荐依赖顺序冻结到正式 consumer matrix
2. 已新增 `docs/reference/contributor-guide-v0.9.zh.md`，给出 session / journal / replay / audit failure-path 改动时的文件落点、最小验证清单、标签切片与 richer scheduler prototype 反模式
3. 两份文档都已明确禁止跳过 `RuntimeSession` / `ExecutionJournal`，直接在 replay / audit / CLI / 外部脚本层私造失败语义
4. `docs/README.md`、`README.md` 与 `docs/plan/roadmap-v0.9.zh.md` 已同步切到 V0.9 matrix / guide 入口

## [x] Issue 12

标题：
建立 V0.9 partial session / failure-path regression、CI 与 reference 文档闭环

背景：
failure-path 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 session、journal、replay 与 audit 的链路上产生隐式回退。

目标：

1. 增加 partial session / failure-path 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.9 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.9 failure-path 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.9 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `tests/cmake/LabelTests.cmake` 已形成 `v0.9-runtime-session-*`、`v0.9-execution-journal-*`、`v0.9-replay-view-*` 与 `v0.9-audit-report-*` 九组 failure-path 标签切片
2. `.github/workflows/ci.yml` 已新增 `Run V0.9 Failure-Path Regression`，显式先跑 V0.9 切片，再继续全量回归
3. `README.md`、`docs/README.md`、`docs/plan/roadmap-v0.9.zh.md` 与 `docs/plan/issue-backlog-v0.9.zh.md` 已同步 current roadmap / backlog / reference 入口
4. `docs/reference/runtime-session-compatibility-v0.9.zh.md`、`docs/reference/execution-journal-compatibility-v0.9.zh.md`、`docs/reference/replay-view-compatibility-v0.9.zh.md`、`docs/reference/audit-report-compatibility-v0.9.zh.md`、`docs/reference/failure-path-compatibility-v0.9.zh.md`、`docs/reference/native-consumer-matrix-v0.9.zh.md` 与 `docs/reference/contributor-guide-v0.9.zh.md` 已形成 V0.9 reference 闭环
5. 本地已通过 V0.9 标签回归与全量 `ctest`，新贡献者可按文档跑通 `package -> execution plan -> partial session -> failure-path journal -> replay / audit`
