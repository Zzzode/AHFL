# AHFL V0.8 Issue Backlog

本文给出与 [roadmap-v0.8.zh.md](./roadmap-v0.8.zh.md) 对齐的 issue-ready backlog。目标是把 V0.7 的 runtime session + execution journal bootstrap 推进为 replay / audit consumer 的稳定边界，而不是直接进入生产 scheduler。

## [x] Issue 01

标题：
冻结 V0.8 replay / audit scope 与 scheduler 非目标

背景：
V0.7 已完成 session 与 journal bootstrap；如果下一阶段直接进入 scheduler / telemetry / host runtime，会再次把生产系统目标混入 compiler / handoff 仓库。

目标：

1. 明确 V0.8 不交付生产 scheduler、真实 host runtime 或 observability pipeline
2. 明确 replay / audit 与 session / journal / trace 的职责关系
3. 明确 failure-path 演进优先落在 journal compatibility 扩展

验收标准：

1. roadmap 与 design 文档对 V0.8 scope 一致
2. replay / audit 与 future runtime 的边界清晰
3. backlog 不把 deployment、telemetry、persistence 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. `docs/design/native-replay-audit-bootstrap-v0.8.zh.md` 已冻结 V0.8 的 scope：当前阶段只交付 replay / audit consumer bootstrap，不交付生产 scheduler、真实 host runtime 或 observability pipeline
2. `docs/plan/roadmap-v0.8.zh.md` 已明确 replay / audit 与 session / journal / trace 的职责拆分，并把 deployment、telemetry、persistence 排除在当前阶段之外
3. V0.8 后续 compatibility 文档已沿用该 scope：failure-path 扩展优先落在 journal / session / replay 兼容演进，而不是 host log integration

## [x] Issue 02

标题：
冻结 Replay View 的最小模型与一致性边界

背景：
如果没有正式 replay 视图，future scheduler prototype、CI consistency 检查与 audit tooling 会各自重建 journal -> state progression 语义。

目标：

1. 定义 `ReplayView` 顶层结构
2. 定义 replay status、per-node lifecycle projection 与 consistency summary 的最小字段
3. 冻结 replay `format_version` 的单一来源

验收标准：

1. replay artifact 有明确最小字段集合
2. replay 与 session / journal 的关系明确
3. 后续实现有单一 `format_version` 来源

主要涉及文件：

- `docs/design/`
- `include/ahfl/`

当前实现备注：

1. `docs/design/native-replay-audit-bootstrap-v0.8.zh.md` 已冻结 `ReplayView` 的最小边界，明确 replay status、per-node progression 与 consistency summary 的职责
2. `include/ahfl/replay_view/replay.hpp` 已新增 `ReplayView`、`ReplayNodeProgression`、`ReplayConsistencySummary` 与 `kReplayViewFormatVersion`，形成单一 `format_version` 来源
3. `docs/reference/replay-view-compatibility-v0.8.zh.md` 已补齐 replay 的稳定字段、兼容扩展与 breaking change 规则，把 replay / session / journal 的关系冻结为正式 consumer contract

## [x] Issue 03

标题：
冻结 Audit Report 的最小字段集合与 failure-path 扩展原则

背景：
当前人工审查要同时看 execution plan、session、journal 与 trace，后续若不冻结 audit report，review / CI 会长期依赖多份分散 artifact。

目标：

1. 定义 `AuditReport` 的最小字段集合
2. 明确 plan / session / journal / trace 在 audit report 中各自承担的信息层次
3. 明确 failure-path 扩展的兼容策略

验收标准：

1. audit report 有单独设计说明
2. review / CI consumer 的依赖边界明确
3. failure-path 扩展不会回退依赖真实 host log

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-replay-audit-bootstrap-v0.8.zh.md` 已冻结 `AuditReport` 的最小边界，明确 plan / session / journal / trace 在 audit report 中各自承担的信息层次
2. `include/ahfl/audit_report/report.hpp` 已新增 `AuditReport`、plan/session/journal/trace summaries、consistency summaries 与 `kAuditReportFormatVersion`
3. `docs/reference/audit-report-compatibility-v0.8.zh.md` 已明确 audit report 的稳定字段、consumer 依赖边界与 failure-path 扩展原则，并显式禁止回退依赖真实 host log 或 provider payload

## [x] Issue 04

标题：
引入 Replay View 数据模型

背景：
V0.8 需要把 session + journal 提升为 replay-ready consumer surface，但当前仓库只有 final snapshot 与 event sequence 两层原始 artifact。

目标：

1. 新增 `ReplayView`、replay status、per-node progression 等数据结构
2. 让 replay 只消费 execution plan / session / journal
3. 保持模型不依赖 AST、raw source 或 project descriptor

验收标准：

1. 有 direct replay model regression
2. replay 模型不回退消费编译期输入
3. replay failure 不会静默吞掉 session / journal validation 错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/replay_view/replay.hpp` 已新增 `ReplayView`、`ReplayNodeProgression`、`ReplayConsistencySummary` 与 `ReplayStatus`
2. `src/replay_view/replay.cpp` 已新增 `build_replay_view(...)`，显式只消费 `ExecutionPlan + RuntimeSession + ExecutionJournal`
3. `tests/replay_view/replay.cpp` 已新增 direct regression，覆盖 project 正例、invalid journal 失败转发与 session/journal mismatch 失败路径

## [x] Issue 05

标题：
增加 replay consistency validation 与 direct regression

背景：
replay 一旦成为 future scheduler / CI 输入，就必须能区分 source artifact 错误与 replay 自身不一致。

目标：

1. 校验 workflow / node lifecycle 与 journal event ordering 一致性
2. 增加 replay direct 正例 / 负例回归
3. 让 diagnostics 明确归属 replay validation 层

验收标准：

1. replay validation 有 direct unit regression
2. 不合法 replay 不会进入 audit bootstrap
3. diagnostics 能稳定匹配 failure 类型

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/replay_view/replay.hpp` 已新增 `validate_replay_view(...)` 与独立 `ReplayViewValidationResult`
2. `src/replay_view/replay.cpp` 已新增 replay-specific consistency check，覆盖 format/source version、execution order、node progression、dependency 与 consistency flags
3. `tests/replay_view/replay.cpp` 已新增 validation 正例、missing completed progression 与 execution_order/index mismatch 负例回归

## [x] Issue 06

标题：
增加 replay 输出路径

背景：
如果 replay 只停留在内部对象，下游 scheduler prototype / CI / audit tooling 仍无法稳定检查消费结果。

目标：

1. 新增 `emit-replay-view` 或等价输出
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 replay golden

验收标准：

1. CLI 可输出稳定 replay 结果
2. 输出覆盖 workflow / node progression / consistency summary
3. 有 single-file / project / workspace golden

主要涉及文件：

- `src/cli/`
- `src/backends/`
- `tests/`

当前实现备注：

1. `include/ahfl/backends/replay_view.hpp` 与 `src/backends/replay_view.cpp` 已新增 `ReplayView` JSON printer
2. `src/cli/ahflc.cpp` 已新增 `emit-replay-view`，复用 `--package --capability-mocks --input-fixture [--workflow] [--run-id]`
3. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已新增 single-file / project / workspace replay golden 回归

## [x] Issue 07

标题：
引入 Audit Report 数据模型

背景：
audit 需要消费规划、状态、事件与 reviewer-friendly 摘要，但当前仓库没有单独的统一报告模型。

目标：

1. 新增 `AuditReport` 顶层结构
2. 定义 plan / session / journal / trace 各自投影到 report 的稳定字段
3. 保持 audit report 不承诺真实 host telemetry 或 provider payload

验收标准：

1. audit model 有 direct regression
2. report 字段边界清晰且职责不混乱
3. audit 不依赖 AST、raw source 或 project descriptor

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/audit_report/report.hpp` 已新增 `AuditReport`、plan/session/journal/trace summary 与 consistency summary 模型
2. `src/audit_report/report.cpp` 已新增 `validate_audit_report(...)`，冻结 audit model 的最小一致性边界
3. `tests/audit_report/report.cpp` 已新增 direct regression，覆盖 validation 正例、unknown session node 与 journal order mismatch 负例

## [x] Issue 08

标题：
增加 plan / session / journal / trace 到 audit report 的 bootstrap

背景：
audit report 只有模型还不够，需要最小 bootstrap 把多份 runtime-adjacent artifact 投影为统一审计视图。

目标：

1. 基于 execution plan、runtime session、execution journal 与 dry-run trace 生成 deterministic audit report
2. 覆盖 success-path 摘要与 failure 分类
3. 诊断 input artifact consistency 错误

验收标准：

1. 有 direct audit bootstrap regression
2. audit report 与 source artifact 一致
3. failure 可区分 replay consistency 错误与 input artifact 错误

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/audit_report/report.hpp` 已新增 `build_audit_report(...)` 与独立 `AuditReportResult`
2. `src/audit_report/report.cpp` 已新增 `ExecutionPlan + RuntimeSession + ExecutionJournal + DryRunTrace -> AuditReport` 的 deterministic bootstrap，并复用 `build_replay_view(...)`
3. `tests/audit_report/report.cpp` 已新增 bootstrap 正例与 trace workflow / execution order mismatch 负例回归

## [x] Issue 09

标题：
增加 audit 输出与 golden 回归

背景：
audit report 若只存在于内部对象，就无法作为 review / CI / regression 稳定入口。

目标：

1. 新增 `emit-audit-report` 或等价输出
2. 输出 workflow、plan summary、session summary、journal summary 与 validation 结论
3. 固定 audit golden

验收标准：

1. audit 输出有稳定 golden
2. 覆盖 project / workspace / package authoring 路径
3. 不包含真实 connector、secret、deployment 或 host telemetry 信息

主要涉及文件：

- `src/cli/`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/backends/audit_report.hpp` 与 `src/backends/audit_report.cpp`，固定 `AuditReport` 的 JSON 输出格式
2. `src/cli/ahflc.cpp` 已接入 `emit-audit-report`，复用 plan / session / journal / trace bootstrap 后统一生成 audit report
3. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已补 single-file / project / workspace golden 回归
4. `tests/audit/` 已新增稳定 golden，覆盖 package authoring 与 project/workspace consumer 路径

## [x] Issue 10

标题：
冻结 replay / audit compatibility 契约与 failure-path 演进规则

背景：
replay 与 audit 一旦进入仓库，就会被 future scheduler、CI 与审计工具消费，必须有单独版本与迁移规则。

目标：

1. 明确 replay format version 与 breaking change 规则
2. 明确 audit format version 与 breaking change 规则
3. 明确 success-path 到 failure-path 的兼容扩展边界

验收标准：

1. 存在 V0.8 compatibility / versioning 文档
2. 文档写出兼容扩展、breaking change 与迁移要求
3. 后续字段扩展有 docs / code / golden / tests 同步流程

主要涉及文件：

- `docs/reference/`
- `include/ahfl/`
- `tests/`

当前实现备注：

1. 已新增 `docs/reference/replay-view-compatibility-v0.8.zh.md`，冻结 `ReplayView` 的稳定字段、breaking change 与 format bump 规则
2. 已新增 `docs/reference/audit-report-compatibility-v0.8.zh.md`，冻结 `AuditReport` 的稳定字段、breaking change 与 format bump 规则
3. 两份文档都已明确 success-path -> failure-path 的演进边界：failure-path 必须先经过 journal / session / replay 的兼容扩展，再进入 replay / audit summary
4. `docs/README.md`、`README.md` 与 `docs/plan/roadmap-v0.8.zh.md` 已同步纳入 V0.8 replay / audit compatibility 入口

## [x] Issue 11

标题：
更新 native consumer matrix 与 contributor guide

背景：
如果不把 replay / audit 纳入 consumer matrix 与 contributor guide，新贡献者仍会把 trace 当作唯一 review / replay 入口。

目标：

1. 更新 native consumer matrix，补 replay / audit consumer 位次
2. 更新 contributor guide，给出 replay / audit 扩展模板
3. 明确 replay / audit 与 plan / trace / session / journal 的推荐依赖顺序

验收标准：

1. consumer matrix 与 contributor guide 都包含 V0.8 新 artifact
2. 文档写出推荐消费顺序与反模式
3. 新 contributor 可据此扩展 consumer 而不误用 trace

主要涉及文件：

- `docs/reference/`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.8.zh.md`，把 replay view 与 audit report 纳入正式 runtime-adjacent consumer 层次
2. 已新增 `docs/reference/contributor-guide-v0.8.zh.md`，给出 replay / audit 扩展模板、推荐顺序、反模式与最小验证清单
3. `docs/README.md`、`README.md` 与 `docs/plan/roadmap-v0.8.zh.md` 已同步切到 V0.8 matrix / guide 入口
4. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> audit`，而 `trace` 保持 contributor-facing review 输出

## [x] Issue 12

标题：
建立 V0.8 replay / audit regression、CI 与 reference 文档闭环

背景：
如果 replay / audit 没有 golden、label、CI 与 reference 闭环，后续 consumer 扩展会重新退化为隐式约定。

目标：

1. 增加 direct model / validation / CLI emission regression
2. 更新 CMake label 与 CI，使 replay / audit 进入持续回归
3. 补齐 reference 文档索引

验收标准：

1. replay / audit regression 可独立运行
2. CI 显式包含 V0.8 相关标签
3. `docs/README.md` 与根 README 均同步当前 plan / backlog / reference 入口

主要涉及文件：

- `tests/`
- `.github/workflows/`
- `docs/README.md`
- `README.md`

当前实现备注：

1. `tests/cmake/LabelTests.cmake` 已包含 `v0.8-replay-view-*` 与 `v0.8-audit-report-*` 六组独立标签切片
2. `.github/workflows/ci.yml` 已新增 `Run V0.8 Replay And Audit Regression`，显式运行 V0.8 replay / audit 标签组合
3. `docs/README.md` 与根 `README.md` 已同步当前 roadmap / backlog / compatibility / consumer matrix / contributor guide 入口
4. 本地已通过 `ctest --preset test-dev --output-on-failure -L 'v0.8-(replay-view-model|replay-view-validation|replay-view-emission|audit-report-model|audit-report-bootstrap|audit-report-emission)'`
