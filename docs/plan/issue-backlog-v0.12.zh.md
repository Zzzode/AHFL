# AHFL V0.12 Issue Backlog

本文给出与 [roadmap-v0.12.zh.md](./roadmap-v0.12.zh.md) 对齐的 issue-ready backlog。目标是把 V0.11 已完成的 checkpoint record / review artifact，继续推进为 future persistence prototype 可稳定消费的 persistence-facing 下一层边界，而不是直接进入 production persistence / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.12 persistence handoff scope 与 non-goals

背景：
V0.11 已经把 checkpoint record / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable checkpoint store、resume protocol 或 production persistence 目标重新混入本地 prototype。

目标：

1. 明确 V0.12 的成功标准是 persistence-facing local prototype artifact
2. 明确 V0.12 不交付 durable store、crash recovery、distributed recovery daemon 或 host log pipeline
3. 明确 persistence prototype 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 persistence descriptor / planned durable identity 术语
2. 文档明确 prototype consumer 不得跳过现有 runtime artifact 与 checkpoint artifact
3. backlog 不把 deployment、telemetry、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-persistence-prototype-bootstrap-v0.12.zh.md`，明确 V0.12 当前只交付 persistence-facing local prototype artifact，不交付 durable store、resume protocol、crash recovery、distributed recovery daemon 或 host telemetry
2. 同一设计文档已明确 persistence prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence` 这条正式链路之上，并显式禁止回退依赖 checkpoint review、scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.12.zh.md` 当前已同步把 V0.12 的 next step 切到 Issue 02-03，确保后续先冻结 persistence descriptor 最小模型与 layering，再进入代码实现

## [x] Issue 02

标题：
冻结 persistence descriptor / planned durable identity 的最小模型与 store-adjacent 边界

背景：
当前仓库虽已有 `CheckpointRecord` / `CheckpointReviewSummary`，但还没有正式的 persistence-facing 状态对象；future persistence prototype 很容易私造 planned durable identity、export handle 与 persistence blocker 语义。

目标：

1. 定义 persistence descriptor 顶层状态集合
2. 定义 planned durable checkpoint identity、exportable prefix、persistence blocker 与 local/store boundary 的最小字段
3. 冻结 persistence-facing `format_version` 的兼容入口与 future durable store / recovery 边界

验收标准：

1. 文档明确哪些字段属于 persistence-facing 稳定输入
2. descriptor 与 checkpoint / review / future store 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/`

当前实现备注：

1. 已新增 `docs/reference/persistence-prototype-compatibility-v0.12.zh.md`，给出 `CheckpointPersistenceDescriptor` 的 `ahfl.persistence-descriptor.v1` 与 future `PersistenceReviewSummary` 的 `ahfl.persistence-review.v1` 计划版本入口基线，同时明确 persistence-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：planned durable identity、exportable prefix、persistence blocker、store boundary 与 source artifact version chain，并明确 durable checkpoint id、resume token、store metadata 仍不属于当前版本稳定承诺
3. `docs/plan/roadmap-v0.12.zh.md` 当前已同步把 V0.12 的 next step 切到 Issue 03，确保后续先冻结 checkpoint artifact / persistence descriptor / future durable store 的 layering，再进入具体模型实现

## [x] Issue 03

标题：
冻结 checkpoint artifact、persistence descriptor 与 future durable store / recovery protocol 的分层关系

背景：
若 V0.12 没先冻结 layering，后续很容易在 persistence review 输出或 CLI 层直接偷塞 store URI、resume token、recovery handle、replication metadata 或 host-side payload。

目标：

1. 明确 persistence descriptor 如何消费 plan / session / journal / replay / snapshot / checkpoint
2. 明确 persistence review summary 与 checkpoint review / scheduler review / audit / trace 的边界
3. 明确 persistence descriptor 与 future durable store / recovery protocol 的兼容关系

验收标准：

1. checkpoint / persistence / review / future store 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-persistence-prototype-bootstrap-v0.12.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `CheckpointRecord` 仍是 persistence descriptor 的直接 machine-facing 上游，而 `PersistenceReviewSummary` 仍只是 projection，不得承接 durable store mutation、resume token 派发或 recovery command synthesis
2. `docs/reference/persistence-prototype-compatibility-v0.12.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 checkpoint review / scheduler review / audit / trace 反向成为 persistence descriptor 的第一输入，或让 persistence descriptor / review 提前承诺 durable store / recovery ABI，通常就应视为 versioning 事件
3. `docs/plan/roadmap-v0.12.zh.md` 当前已同步把 V0.12 的 next step 切到 Issue 04-06，确保后续先进入 persistence descriptor 的 direct model、validation 与 bootstrap，而不是继续在 layering 未冻结前扩张私有 store 语义

## [x] Issue 04

标题：
引入 checkpoint persistence descriptor 数据模型

背景：
要让 persistence prototype 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 planned durable identity / exportable prefix / persistence blocker 状态。

目标：

1. 引入 persistence-facing 顶层 descriptor / cursor 对象
2. 覆盖 planned durable identity、exportable prefix、descriptor-ready、persistence blocker 与 local/store boundary 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct persistence descriptor model regression
2. persistence descriptor 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/persistence_descriptor/descriptor.hpp`，定义 `CheckpointPersistenceDescriptor`、`PersistenceCursor`、`PersistenceBlocker`、顶层 `PersistenceDescriptorStatus` / `PersistenceBasisKind` / `PersistenceBlockerKind`，以及单一 `format_version` 入口 `ahfl.persistence-descriptor.v1`
2. 已新增 `src/persistence_descriptor/descriptor.cpp` 与 `src/persistence_descriptor/CMakeLists.txt`，提供 persistence-facing direct model validation 入口，并把 `ahfl_persistence_descriptor` 接入主编译链路，作为后续 bootstrap / backend / CLI 的正式落点
3. 已新增 `tests/persistence_descriptor/descriptor.cpp`、`tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake`，覆盖 valid ready-to-export、valid blocked、valid terminal failed、missing planned durable identity、non-prefix exportable prefix 与 export-ready + blocker 冲突六类 direct model regression，并接入 `ahfl-v0.12` / `v0.12-persistence-descriptor-model` 标签

## [x] Issue 05

标题：
增加 persistence descriptor validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 planned durable identity、exportable prefix 与上游 checkpoint artifact 保持一致。

目标：

1. 为 persistence descriptor 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 persistence-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. persistence descriptor 与 checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `src/persistence_descriptor/descriptor.cpp` 已补齐 persistence descriptor validation 的核心一致性约束：包括 source artifact format version gate、planned durable identity 非空、exportable prefix 必须是 execution order 前缀、export-ready 与 blocker 互斥、ReadyToExport 必须带 next export candidate 且 checkpoint 必须 ReadyToPersist，以及 completed / failed / partial workflow 与 terminal checkpoint / persistence status 的对齐关系
2. 同一 validator 已把 terminal failed / partial 状态下禁止 export-ready / next candidate、TerminalFailed 必须带 workflow failure summary、TerminalCompleted 必须具备 full exportable prefix，以及 `StoreAdjacent` basis 只能出现在 ready 或 completed persistence status 中显式化，避免这些规则被后续 CLI / review 层隐式吞掉
3. `tests/persistence_descriptor/descriptor.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 direct regression 扩展到 ready、blocked、terminal failed、terminal partial 四类正例，以及 missing planned durable identity、non-prefix cursor、export-ready + blocker、unsupported checkpoint record format、ready from blocked checkpoint、terminal failed export-ready、store-adjacent blocked 七类负例，并继续挂到 `ahfl-v0.12` / `v0.12-persistence-descriptor-model`

## [x] Issue 06

标题：
增加 deterministic persistence descriptor bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `CheckpointRecord` 及其上游 artifact 投影为 persistence-facing descriptor。

目标：

1. 基于 V0.11 已冻结 artifact 构建 deterministic persistence descriptor
2. 覆盖 success、partial 与 failure-path 的 persistence-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 persistence-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. persistence descriptor 与 plan / session / journal / replay / snapshot / checkpoint 保持一致
3. failure 归因不会被 checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/persistence_descriptor/descriptor.hpp` 与 `src/persistence_descriptor/descriptor.cpp` 已新增 `PersistenceDescriptorResult` / `build_persistence_descriptor(...)`，把 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot` 与 `CheckpointRecord` 作为正式 deterministic bootstrap 输入，并继续复用 direct validator 区分 input artifact error 与 bootstrap mismatch
2. 同一 bootstrap 已显式校验 source format version chain、package identity、workflow / session / run / input / failure summary 对齐关系，以及 replay / snapshot / checkpoint execution order 与 prefix 一致性，然后再把 `CheckpointRecord` 投影为 persistence-facing `planned_durable_identity`、`exportable_prefix`、`persistence_status` 与 `persistence_blocker`
3. `tests/persistence_descriptor/descriptor.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 `v0.12-persistence-descriptor-bootstrap` 回归切片，覆盖 completed / failed / partial 三类 bootstrap 正例，以及 invalid checkpoint record / checkpoint workflow mismatch 两类负例；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.12-persistence-descriptor-(model|bootstrap)'` 稳定回归

## [x] Issue 07

标题：
增加 persistence descriptor 输出路径

背景：
如果 persistence-facing 状态只停留在内部对象，future persistence prototype、CI 与 reviewer 都无法稳定消费 export 语义。

目标：

1. 扩展 CLI / backend 输出 persistence descriptor
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 persistence descriptor golden

验收标准：

1. CLI 可输出稳定 persistence descriptor 结果
2. 输出覆盖 planned durable identity、exportable prefix、persistence blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/backends/persistence_descriptor.hpp` 与 `src/backends/persistence_descriptor.cpp`，提供 `print_persistence_descriptor_json(...)`，把 source format chain、workflow / checkpoint / persistence status、planned durable identity、exportable prefix、next export candidate、export basis 与 persistence blocker 以稳定 JSON 形式输出
2. `src/cli/ahflc.cpp` 已新增 `emit-persistence-descriptor` 子命令，沿用 `emit-checkpoint-record` 的 deterministic 链路构建 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor`，并把 `--package` / `--capability-mocks` / `--input-fixture` / `--run-id` / `--workflow` 的参数校验同步扩展到新命令
3. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 与 `tests/persistence/` 已新增 single-file success、project failed、workspace partial 三类 emission regression，并接入 `ahfl-v0.12` / `v0.12-persistence-descriptor-emission` 标签，确保输出路径不回退成隐式约定

## [x] Issue 08

标题：
增加 persistence review / export preview surface

背景：
persistence prototype 若只有 machine-facing descriptor，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable persistence handoff、后续 durable store 应看什么”。

目标：

1. 增加 persistence-facing reviewer summary
2. 覆盖 planned durable identity、exportable prefix、persistence blocker、store boundary 与 next-step recommendation
3. 保持 summary 只消费正式 persistence-facing model，而不私造 durable store / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source persistence descriptor 一致
3. review surface 不倒灌 host telemetry、store metadata、replication metadata 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/persistence_descriptor/review.hpp` 与 `src/persistence_descriptor/review.cpp`，定义 `PersistenceReviewSummary`、`PersistenceReviewNextActionKind`、validation 与 `build_persistence_review_summary(...)`，并把 review 严格收口为只消费正式 `CheckpointPersistenceDescriptor` 的 readable projection
2. 已新增 `include/ahfl/backends/persistence_review.hpp`、`src/backends/persistence_review.cpp` 与 `src/cli/ahflc.cpp` 中的 `emit-persistence-review` 子命令，固定输出 `planned_durable_identity`、`exportable_prefix`、`persistence_blocker`、`store_boundary_summary`、`export_preview` 与 `next_step_recommendation`，同时继续沿用 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceReviewSummary` 的 deterministic 链路
3. `tests/persistence_descriptor/descriptor.cpp`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 与 `tests/persistence/` 已新增 persistence review 的 direct / CLI regression，覆盖 completed、failed、partial 三类正例，以及 invalid descriptor / unsupported source descriptor format 两类负例，并接入 `ahfl-v0.12` / `v0.12-persistence-review-model` / `v0.12-persistence-review-emission`

## [x] Issue 09

标题：
增加 single-file / project / workspace persistence golden 回归

背景：
若 persistence prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 固定 single-file persistence golden
2. 固定 project / workspace persistence golden
3. 保持 success / partial / failure-path 都被覆盖

验收标准：

1. golden 可独立运行
2. `--package` 路径与 project / workspace 路径都被覆盖
3. persistence-facing 变更能被稳定 diff

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. `tests/persistence/` 现已固定 six-file persistence golden 样本：single-file success、project failed、workspace partial，分别覆盖 `persistence-descriptor` JSON 与 `persistence-review` readable summary 两类输出
2. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已把 `emit-persistence-descriptor` / `emit-persistence-review` 的 `--package`、project 与 workspace 路径都接入 expected-output 对比，确保 persistence-facing 变更可以稳定 diff
3. `tests/cmake/LabelTests.cmake` 已新增 `v0.12-persistence-golden` 独立标签，把 descriptor / review 的 six-file golden regression 收口为单独可运行测试集，满足“golden 可独立运行”的验收要求

## [x] Issue 10

标题：
冻结 persistence descriptor / review compatibility 契约

背景：
当 descriptor / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把 durable store / recovery 字段静默塞回当前 persistence prototype。

目标：

1. 冻结 `CheckpointPersistenceDescriptor` 与 persistence review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 descriptor / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/persistence-prototype-compatibility-v0.12.zh.md` 已从 Issue 02 的“计划入口 / 预计顺序”收口为正式 compatibility contract，明确 `CheckpointPersistenceDescriptor` 的 `ahfl.persistence-descriptor.v1` 与 `PersistenceReviewSummary` 的 `ahfl.persistence-review.v1` 为当前稳定版本入口
2. 同一文档已按 artifact 固定 descriptor / review 的 source version 校验顺序、最小稳定字段边界、明确不承诺字段、实现入口、CLI 命令、回归标签、docs / code / golden 同步要求，以及 non-breaking extension / breaking-change / version bump 基线
3. `docs/plan/roadmap-v0.12.zh.md` 与本 backlog 已同步把 `Issue 10` 标记完成，并把下一步推进到 `Issue 11` 的 native consumer matrix、contributor guide 与 future persistence boundary guidance

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future persistence boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future persistence prototype 仍需要知道 persistence-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.12 persistence-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future durable store / recovery boundary guidance

验收标准：

1. 有独立的 V0.12 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.12.zh.md`，把 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceReviewSummary` 的推荐消费顺序、共享入口、扩展模板、future durable store / recovery explorer 落点与反模式正式矩阵化
2. 已新增 `docs/reference/contributor-guide-v0.12.zh.md`，给出 persistence descriptor / review 的入口文件、推荐扩展顺序、future durable store boundary guidance 与最小验证清单，并把 `v0.12-persistence-descriptor-model`、`v0.12-persistence-descriptor-bootstrap`、`v0.12-persistence-review-model`、`v0.12-persistence-golden` 固定为当前主链路回归锚点
3. `README.md`、`docs/README.md`、`docs/plan/roadmap-v0.12.zh.md` 与本 backlog 已同步把 V0.12 persistence-facing 文档入口切到新的 consumer matrix / contributor guide，并把下一步推进到 `Issue 12` 的 regression / CI / reference 收口

## [x] Issue 12

标题：
建立 V0.12 persistence prototype regression、CI 与 reference 文档闭环

背景：
persistence-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 descriptor、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 persistence prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.12 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.12 persistence prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.12 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `.github/workflows/ci.yml` 已在普通 build/test 与 Linux ASan/UBSan job 中显式加入 `ctest --preset test-dev --output-on-failure -L 'ahfl-v0.12'` / `ctest --preset test-asan --output-on-failure -L 'ahfl-v0.12'` 的 V0.12 persistence prototype 回归切片，并保持后续全量 `ctest` 作为兜底
2. `tests/cmake/LabelTests.cmake` 当前已形成 V0.12 分层标签：descriptor model、descriptor bootstrap、descriptor emission、review model、review emission、persistence golden 与 umbrella `ahfl-v0.12`
3. `README.md`、`docs/README.md`、`docs/reference/persistence-prototype-compatibility-v0.12.zh.md`、`docs/reference/native-consumer-matrix-v0.12.zh.md`、`docs/reference/contributor-guide-v0.12.zh.md`、roadmap 与本 backlog 当前已形成 docs / tests / CI / reference 闭环，新贡献者可按 contributor guide 跑通 V0.12 主链路并定位失败归因
