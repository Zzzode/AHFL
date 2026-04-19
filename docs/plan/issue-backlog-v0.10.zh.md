# AHFL V0.10 Issue Backlog

本文给出与 [roadmap-v0.10.zh.md](./roadmap-v0.10.zh.md) 对齐的 issue-ready backlog。目标是把 V0.9 已完成的 partial session / failure-path artifact，继续推进为 richer scheduler prototype 可稳定消费的下一层边界，而不是直接进入生产 scheduler / persistence 系统。

## [x] Issue 01

标题：
冻结 V0.10 richer scheduler prototype scope 与 non-goals

背景：
V0.9 已经把 partial session、failure-path journal、replay / audit 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把 durable persistence、真实 recovery protocol 或 production scheduler 目标重新混入本地 prototype。

目标：

1. 明确 V0.10 的成功标准是 scheduler-facing local prototype artifact
2. 明确 V0.10 不交付生产 scheduler、durable persistence、checkpoint store 或 host log pipeline
3. 明确 richer scheduler prototype 仍然只能建立在 `plan -> session -> journal -> replay` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 scheduler prototype / checkpoint-friendly 术语
2. 文档明确 prototype consumer 不得跳过现有 runtime artifact
3. backlog 不把 deployment、telemetry、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. `docs/design/native-scheduler-prototype-bootstrap-v0.10.zh.md` 已冻结 V0.10 的 scope：当前阶段只交付 scheduler-facing local prototype artifact，不交付生产 scheduler、durable persistence、checkpoint store、resume protocol 或 host telemetry
2. `docs/plan/roadmap-v0.10.zh.md` 已明确 richer scheduler prototype 只能建立在 `plan -> session -> journal -> replay -> scheduler snapshot` 这条链路之上
3. backlog 当前已显式排除 deployment、telemetry、durable persistence、distributed runtime 等非目标

## [x] Issue 02

标题：
冻结 scheduler snapshot / cursor 的最小模型与 checkpoint-friendly 边界

背景：
当前仓库虽已有 session / journal / replay / audit，但还没有正式的 scheduler-facing 状态对象，future prototype 很容易私造 ready-set、blocked-reason 与 next-step 语义。

目标：

1. 定义 scheduler snapshot / cursor 顶层状态集合
2. 定义 ready node、blocked reason、executed prefix 与 local checkpoint-friendly state 的最小字段
3. 冻结 scheduler-facing `format_version` 的兼容入口与 future persistence 边界

验收标准：

1. 文档明确哪些字段属于 scheduler-facing 稳定输入
2. snapshot 与 session / journal / replay 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/`

当前实现备注：

1. `docs/design/native-scheduler-prototype-bootstrap-v0.10.zh.md` 已冻结 scheduler snapshot / cursor 的最小状态集合，明确顶层 prototype 状态、ready set、blocked reason、executed prefix 与 checkpoint-friendly local state 的职责边界
2. 同一设计文档已区分 input artifact error、runtime result failure、scheduler-facing inconsistency 与 future persistence 语义，避免把 runtime artifact 与 durable checkpoint 混为一谈
3. `docs/reference/scheduler-prototype-compatibility-v0.10.zh.md` 已明确 scheduler-facing 需要单独版本入口与 breaking-change 触发条件，作为后续实现的版本化基线

## [x] Issue 03

标题：
冻结 scheduler prototype、runtime artifact 与 future persistence 的分层关系

背景：
若 V0.10 没先冻结 layering，后续很容易在 scheduler review 输出或 CLI 层直接偷塞 resume token、checkpoint id 或 host-side recovery 语义。

目标：

1. 明确 scheduler prototype 如何消费 plan / session / journal / replay
2. 明确 scheduler-facing summary 与 audit / trace 的边界
3. 明确 local checkpoint-friendly snapshot 与 durable persistence / recovery protocol 的兼容关系

验收标准：

1. scheduler / review / persistence 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 AST、trace、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-scheduler-prototype-bootstrap-v0.10.zh.md` 已冻结 `RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot / Cursor` 与 `Scheduler Review Summary` 的分层关系，并显式要求 scheduler-facing state 先进入专用 snapshot artifact
2. `docs/reference/scheduler-prototype-compatibility-v0.10.zh.md` 已写明哪些变化可视为兼容扩展，哪些新增 scheduler state / cursor / checkpoint-friendly 语义必须 bump version
3. 文档已显式禁止 scheduler / review / 外部脚本回退依赖 AST、trace、host log 或 provider payload 重建 scheduler-facing 语义

## [x] Issue 04

标题：
引入 scheduler snapshot / cursor 数据模型

背景：
要让 richer scheduler prototype 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建状态机。

目标：

1. 引入 scheduler-facing 顶层 snapshot / cursor 对象
2. 覆盖 ready、blocked、terminal、executed-prefix 与 next-step summary 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct scheduler model regression
2. snapshot 不回退依赖 trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/scheduler_snapshot/snapshot.hpp`，定义 `SchedulerSnapshot`、`SchedulerCursor`、ready / blocked node summary、顶层 scheduler prototype 状态与单一 `format_version` 来源
2. 已新增 `src/scheduler_snapshot/snapshot.cpp` 与 `src/scheduler_snapshot/CMakeLists.txt`，为后续 validation / bootstrap 提供独立模块入口
3. 已新增 `tests/scheduler_snapshot/snapshot.cpp`，覆盖 valid model、runnable-without-ready-node、non-prefix cursor 与 unknown-ready-node 四类 direct regression
4. `tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 scheduler snapshot model 接入 `v0.10-scheduler-snapshot-model` 标签切片

## [x] Issue 05

标题：
增加 scheduler snapshot validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 ready-set、blocked reason、executed prefix 与上游 artifact 保持一致。

目标：

1. 为 scheduler snapshot 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 scheduler-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. snapshot 与 session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `src/scheduler_snapshot/snapshot.cpp` 已补齐 scheduler snapshot validation：覆盖 source package identity、failure summary、ready / blocked dependency hygiene、next candidate 与 snapshot status 对齐、terminal completed prefix 完整性，以及 blocked terminal failure / upstream partial 的一致性约束
2. `tests/scheduler_snapshot/snapshot.cpp` 已扩展 direct regression，覆盖 valid partial runnable、valid terminal failed，以及 next candidate mismatch、dependency subset violation、blocked terminal failure missing summary、terminal completed without full prefix 等正负例
3. `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 scheduler snapshot validation 扩展到 9 个 direct model regression，并继续挂到 `ahfl-v0.10` / `v0.10-scheduler-snapshot-model` 标签

## [x] Issue 06

标题：
增加 deterministic scheduler snapshot bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 execution plan、runtime session、execution journal 与 replay 投影为 scheduler-facing local state。

目标：

1. 基于 V0.9 已冻结 artifact 构建 deterministic scheduler snapshot
2. 覆盖 success、partial 与 failure-path 的 scheduler-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 scheduler-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. scheduler snapshot 与 plan / session / journal / replay 保持一致
3. failure 归因不会被 review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/scheduler_snapshot/snapshot.hpp` 与 `src/scheduler_snapshot/snapshot.cpp` 已新增 `build_scheduler_snapshot(...)` 入口：先验证 `ExecutionPlan` / `RuntimeSession` / `ExecutionJournal` / `ReplayView`，再做 source version、package identity、workflow / session / run / replay consistency 对齐检查
2. bootstrap 当前以 execution plan workflow order 作为稳定 `execution_order`，并基于 runtime session / replay 状态投影 completed prefix、ready set、blocked frontier、next candidate 与 top-level snapshot status，覆盖 completed / failed / partial 三条 deterministic 分支
3. `tests/scheduler_snapshot/snapshot.cpp` 已新增 direct bootstrap regression，覆盖 success、failed、partial 与 replay workflow mismatch 负例；`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把这组回归接入 `ahfl-v0.10` / `v0.10-scheduler-snapshot-bootstrap`

## [x] Issue 07

标题：
增加 scheduler snapshot 输出路径

背景：
如果 scheduler-facing 状态只停留在内部对象，future prototype、CI 与 reviewer 都无法稳定消费 richer scheduler 语义。

目标：

1. 扩展 CLI / backend 输出 scheduler snapshot
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 scheduler snapshot golden

验收标准：

1. CLI 可输出稳定 scheduler snapshot 结果
2. 输出覆盖 ready set、blocked reason、executed prefix 与 next-step summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. `include/ahfl/backends/scheduler_snapshot.hpp` 与 `src/backends/scheduler_snapshot.cpp` 已新增 scheduler snapshot JSON printer，覆盖 top-level status、workflow failure、ready set、blocked frontier 与 cursor 输出
2. `src/cli/ahflc.cpp` 已新增 `emit-scheduler-snapshot` 子命令，并沿着 `execution plan -> runtime session -> execution journal -> replay view -> scheduler snapshot` 链路构建与打印 scheduler-facing artifact
3. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 已新增 single-file success、project failed、workspace partial 三条 CLI golden regression；golden 文件位于 `tests/scheduler/`，并接入 `ahfl-v0.10` / `v0.10-scheduler-snapshot-emission`

## [x] Issue 08

标题：
增加 scheduler decision summary / review surface

背景：
scheduler prototype 若只有 machine-facing snapshot，而没有 reviewer-facing summary，新贡献者仍会回退依赖 trace / grep 来判断下一步调度含义。

目标：

1. 增加 scheduler-facing reviewer summary
2. 覆盖 ready set、blocked reason、executed prefix、terminal reason 与 next-step recommendation
3. 保持 summary 只消费正式 scheduler-facing model，而不私造状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source snapshot 一致
3. review surface 不倒灌 host telemetry 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/scheduler_snapshot/review.hpp` 与 `src/scheduler_snapshot/review.cpp`，定义 `SchedulerDecisionSummary`、`SchedulerNextActionKind` 与 `build_scheduler_decision_summary(...)`：summary 只消费已验证 `SchedulerSnapshot`，覆盖 workflow/session/run/input、ready set、blocked frontier、executed prefix、terminal reason、next action 与 next-step recommendation
2. 已新增 `include/ahfl/backends/scheduler_review.hpp` 与 `src/backends/scheduler_review.cpp`，提供 reviewer-facing text printer；`src/cli/ahflc.cpp` 已新增 `emit-scheduler-review` 子命令，并沿着 `execution plan -> runtime session -> execution journal -> replay view -> scheduler snapshot -> decision summary` 的正式链路输出 review surface
3. `tests/scheduler_snapshot/snapshot.cpp`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 direct summary regression、single-file success / project failed / workspace partial 三条 CLI golden，以及 `v0.10-scheduler-review-model` / `v0.10-scheduler-review-emission` 标签切片；对应 golden 位于 `tests/scheduler/*.scheduler-review`

## [x] Issue 09

标题：
增加 single-file / project / workspace scheduler golden 回归

背景：
若 richer scheduler prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 固定 single-file scheduler golden
2. 固定 project / workspace scheduler golden
3. 保持 success / partial / failure-path 都被覆盖

验收标准：

1. golden 可独立运行
2. `--package` 路径与 project/workspace 路径都被覆盖
3. scheduler-facing 变更能被稳定 diff

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. `tests/scheduler/` 当前已固定 6 份 scheduler-facing golden：`scheduler-snapshot` 与 `scheduler-review` 各覆盖 single-file success、project failed、workspace partial 三条代表路径，使 success / partial / failure-path 都有稳定 diff 基线
2. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已把这 6 条 golden 全部接入 CLI regression，覆盖 `--package`、`--project` 与 `--workspace --project-name` 三种入口
3. `tests/cmake/LabelTests.cmake` 已新增 `v0.10-scheduler-golden` 与 `v0.10-scheduler-regression` 两个独立切片：前者只跑 6 条 scheduler golden，后者汇总 scheduler snapshot/review 的 model、bootstrap 与 golden 回归，方便在 scheduler-facing 变更时单独执行

## [x] Issue 10

标题：
冻结 scheduler snapshot / decision summary compatibility 契约

背景：
一旦 scheduler-facing artifact 进入仓库，就会被 future prototype、review tooling 与 checkpoint explorer 消费，必须有单独版本与迁移规则。

目标：

1. 明确 scheduler-facing format version 与 breaking change 规则
2. 明确 snapshot / review summary 与 V0.9 artifact 的兼容层次关系
3. 明确 future persistence boundary 不得静默塞入当前版本

验收标准：

1. 存在 V0.10 compatibility / versioning 文档
2. 文档写出兼容扩展、breaking change 与迁移要求
3. 后续字段扩展有 docs / code / golden / tests 同步流程

主要涉及文件：

- `docs/reference/`
- `include/ahfl/`
- `tests/`

当前实现备注：

1. `docs/reference/scheduler-prototype-compatibility-v0.10.zh.md` 已从高层 layering 说明收口为逐 artifact compatibility contract，正式冻结 `SchedulerSnapshot` 的 `ahfl.scheduler-snapshot.v1` 与 `SchedulerDecisionSummary` 的 `ahfl.scheduler-review.v1` 版本入口、source artifact 校验顺序、兼容扩展 / breaking change 规则，以及 docs / code / golden / tests 的同步流程
2. `include/ahfl/scheduler_snapshot/review.hpp` 与 `src/scheduler_snapshot/review.cpp` 已新增 `validate_scheduler_decision_summary(...)`，把 review artifact 自身的 `format_version`、`source_scheduler_snapshot_format_version`、completed prefix 长度、terminal reason / next action 对齐关系等纳入正式 validator，而不是只依赖 builder 隐式保证
3. `tests/scheduler_snapshot/snapshot.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 snapshot / review compatibility regression，并接入 `v0.10-scheduler-compatibility` 标签；当前可单独回归 scheduler-facing version gate、source version gate 与关键 summary consistency

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future persistence boundary guidance

背景：
新增 scheduler-facing artifact 后，当前 V0.9 matrix / guide 还只描述到 replay / audit，不足以指导下一阶段 prototype / checkpoint-friendly 扩展。

目标：

1. 更新 consumer matrix，加入 scheduler prototype / checkpoint-friendly 消费建议
2. 更新 contributor guide，说明改 scheduler snapshot / review surface 时的文件落点和验证命令
3. 补充 future persistence boundary 的推荐依赖顺序与反模式

验收标准：

1. matrix 明确各类 consumer 可稳定依赖的 scheduler-facing 字段
2. contributor guide 给出最小验证清单与标签切片
3. 文档明确不能跳过现有 runtime artifact，直接在 scheduler / CLI 层做私有状态机

主要涉及文件：

- `docs/reference/`
- `docs/README.md`
- `README.md`

当前实现备注：

1. `docs/reference/native-consumer-matrix-v0.10.zh.md` 已新增并正式冻结 V0.10 scheduler-facing consumer matrix，把 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`SchedulerDecisionSummary`、`AuditReport` 与 `DryRunTrace` 的稳定输入、边界职责、推荐消费顺序与反模式统一收口到 `plan -> session -> journal -> replay -> snapshot -> review`
2. `docs/reference/contributor-guide-v0.10.zh.md` 已新增并给出 scheduler snapshot / review 的改动入口、最小验证命令、标签切片与 future persistence boundary guidance，明确后续 checkpoint-friendly explorer 不得绕过现有 runtime artifact 或把 durable checkpoint 字段提前塞入 review 输出
3. `README.md` 与 `docs/README.md` 已同步把 `emit-scheduler-snapshot`、`emit-scheduler-review`、V0.10 consumer matrix 与 V0.10 contributor guide 暴露为当前版本入口，避免新贡献者继续误跟随 V0.9 文档路径

## [x] Issue 12

标题：
建立 V0.10 scheduler prototype regression、CI 与 reference 文档闭环

背景：
scheduler-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 snapshot、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 scheduler prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.10 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.10 scheduler prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.10 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `.github/workflows/ci.yml` 已在常规 build matrix 与 ASan job 中显式新增 `v0.10-scheduler-regression` 切片，并保持后续全量 `ctest` 回归，确保 scheduler snapshot / review 链路先以独立标签执行，再进入全量测试
2. `docs/reference/cli-commands-v0.10.zh.md` 已新增为当前 CLI reference，正式收口 `emit-execution-plan`、`emit-runtime-session`、`emit-execution-journal`、`emit-replay-view`、`emit-scheduler-snapshot`、`emit-scheduler-review`、`emit-audit-report` 与 `emit-dry-run-trace` 的命令列表、选项矩阵、project-aware 支持矩阵与 V0.10 主链路示例
3. `README.md`、`docs/README.md` 与 `docs/plan/roadmap-v0.10.zh.md` 已同步把 V0.10 CLI command reference 暴露为当前入口，并把 V0.10 roadmap / backlog 收口为 M0-M4 全部完成态，便于新贡献者按文档定位 scheduler prototype 的 direct / golden / doc / CI 分层
