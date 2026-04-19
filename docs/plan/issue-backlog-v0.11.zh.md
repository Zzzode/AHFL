# AHFL V0.11 Issue Backlog

本文给出与 [roadmap-v0.11.zh.md](./roadmap-v0.11.zh.md) 对齐的 issue-ready backlog。目标是把 V0.10 已完成的 scheduler snapshot / review artifact，继续推进为 future persistence prototype 可稳定消费的 checkpoint-facing 下一层边界，而不是直接进入 production persistence / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.11 checkpoint prototype scope 与 non-goals

背景：
V0.10 已经把 scheduler snapshot / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把 durable checkpoint store、真实 recovery protocol 或 production persistence 目标重新混入本地 prototype。

目标：

1. 明确 V0.11 的成功标准是 checkpoint-facing local prototype artifact
2. 明确 V0.11 不交付 durable store、crash recovery、distributed recovery daemon 或 host log pipeline
3. 明确 checkpoint prototype 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 checkpoint prototype / resume basis 术语
2. 文档明确 prototype consumer 不得跳过现有 runtime artifact
3. backlog 不把 deployment、telemetry、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. `docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md` 已新增并冻结 V0.11 的 scope：当前阶段只交付 checkpoint-facing local prototype artifact，不交付 durable store、resume protocol、crash recovery、distributed recovery daemon 或 host telemetry
2. 同一设计文档已明确 checkpoint prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint` 这条正式链路之上，并显式禁止回退依赖 scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.11.zh.md` 当前已同步把 V0.11 的 next step 切到 Issue 02-03，确保后续先冻结 checkpoint record 最小模型与 layering，再进入代码实现

## [x] Issue 02

标题：
冻结 checkpoint record / resume basis 的最小模型与 durable-adjacent 边界

背景：
当前仓库虽已有 scheduler snapshot / review，但还没有正式的 checkpoint-facing 状态对象，future persistence prototype 很容易私造 checkpoint identity、persistable prefix 与 resume blocker 语义。

目标：

1. 定义 checkpoint record 顶层状态集合
2. 定义 persistable prefix、checkpoint identity、resume basis 与 local/durable boundary 的最小字段
3. 冻结 checkpoint-facing `format_version` 的兼容入口与 future recovery 边界

验收标准：

1. 文档明确哪些字段属于 checkpoint-facing 稳定输入
2. checkpoint 与 snapshot / review / recovery 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/`

当前实现备注：

1. `docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md` 已冻结 checkpoint record / resume basis 的最小问题域，明确 checkpoint identity、persistable prefix、resume basis / blocker 与 local-only / durable-adjacent boundary 是当前 V0.11 的最小 machine-facing 模型目标
2. `docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md` 已新增并给出 `CheckpointRecord` 的 `ahfl.checkpoint-record.v1` 与 future `CheckpointReviewSummary` 的 `ahfl.checkpoint-review.v1` 版本入口基线，同时明确 checkpoint-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint` 链路之上
3. 同一 reference 文档已列出当前稳定输入边界、明确不承诺字段与 breaking-change 候选，作为后续 Issue 04-10 的版本化基线，避免后续实现把 durable checkpoint id、resume token 或 store metadata 提前塞进当前模型

## [x] Issue 03

标题：
冻结 checkpoint prototype、scheduler artifact 与 future recovery protocol 的分层关系

背景：
若 V0.11 没先冻结 layering，后续很容易在 checkpoint review 输出或 CLI 层直接偷塞 resume token、recovery handle、store metadata 或 host-side payload。

目标：

1. 明确 checkpoint prototype 如何消费 plan / session / journal / replay / snapshot
2. 明确 checkpoint review summary 与 scheduler review / audit / trace 的边界
3. 明确 checkpoint record 与 future recovery protocol / durable store 的兼容关系

验收标准：

1. checkpoint / review / recovery 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md` 已新增 checkpoint-adjacent 七层总表，正式区分 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord` 与 `CheckpointReviewSummary` 的职责边界，并明确 `CheckpointRecord` 才是 checkpoint-facing machine artifact 的第一正式落点
2. 同一设计文档已显式拆分 `SchedulerDecisionSummary`、future `CheckpointReviewSummary`、`AuditReport` 与 `DryRunTrace` 的职责：scheduler review 只回答调度语义，checkpoint review 只回答 persistable basis / resume preview，audit / trace 不得替代 checkpoint-facing 第一事实来源
3. `docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md` 已补充 layering-sensitive compatibility 规则，明确一旦让 review / trace / audit / scheduler summary 反向成为 checkpoint ABI 第一输入，或让 `SchedulerSnapshot` 承担 durable store / recovery 语义，通常就应视为 breaking change

## [x] Issue 04

标题：
引入 checkpoint record 数据模型

背景：
要让 checkpoint prototype 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 persistable prefix / resume basis 状态。

目标：

1. 引入 checkpoint-facing 顶层 record / cursor 对象
2. 覆盖 checkpoint identity、persistable prefix、resume-ready、resume blocker 与 local/durable boundary 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct checkpoint model regression
2. checkpoint record 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/checkpoint_record/record.hpp`，定义 `CheckpointRecord`、`CheckpointCursor`、`CheckpointResumeBlocker`、顶层 `CheckpointRecordStatus` / `CheckpointBasisKind` / `CheckpointResumeBlockerKind`，以及单一 `format_version` 入口 `ahfl.checkpoint-record.v1`
2. 已新增 `src/checkpoint_record/record.cpp` 与 `src/checkpoint_record/CMakeLists.txt`，提供 checkpoint-facing direct model validation 入口，并把 `ahfl_checkpoint_record` 接入主编译链路，作为后续 bootstrap / backend / CLI 的正式落点
3. 已新增 `tests/checkpoint_record/record.cpp`、`tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake`，覆盖 valid ready-to-persist、valid blocked、non-prefix persistable prefix 与 resume-ready + blocker 冲突四类 direct model regression，并接入 `ahfl-v0.11` / `v0.11-checkpoint-record-model` 标签

## [x] Issue 05

标题：
增加 checkpoint record validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 persistable prefix、resume basis 与上游 artifact 保持一致。

目标：

1. 为 checkpoint record 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 checkpoint-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. checkpoint 与 snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `src/checkpoint_record/record.cpp` 已补齐 checkpoint record validation 的核心一致性约束：包括 persistable prefix 必须是 execution order 前缀、resume candidate 不得已在 prefix 中、`ReadyToPersist` 必须带 resume candidate、`TerminalCompleted` 必须具备 full prefix、`TerminalFailed` 必须具备 workflow failure summary，以及 `workflow_status` / `snapshot_status` / `checkpoint_status` 的 terminal 对齐关系
2. 同一 validator 已把 `basis_kind = DurableAdjacent` 与 `checkpoint_friendly_source` 的关系、resume_ready 与 resume_blocker 的互斥/必需关系、terminal blocked status 与 resume candidate 的冲突等约束显式化，避免这些规则被后续 CLI / review 层隐式吞掉
3. `tests/checkpoint_record/record.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 direct regression 扩展到 ready、blocked、terminal completed、terminal failed 四类正例，以及 non-prefix cursor、resume-ready + blocker、terminal completed without full prefix、terminal failed without failure summary、durable-adjacent without checkpoint-friendly source 五类负例，并继续挂到 `ahfl-v0.11` / `v0.11-checkpoint-record-model`

## [x] Issue 06

标题：
增加 deterministic checkpoint record bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 execution plan、runtime session、execution journal、replay 与 scheduler snapshot 投影为 checkpoint-facing record。

目标：

1. 基于 V0.10 已冻结 artifact 构建 deterministic checkpoint record
2. 覆盖 success、partial 与 failure-path 的 checkpoint-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 checkpoint-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. checkpoint record 与 plan / session / journal / replay / snapshot 保持一致
3. failure 归因不会被 scheduler review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `include/ahfl/checkpoint_record/record.hpp` 与 `src/checkpoint_record/record.cpp` 已新增 `CheckpointRecordResult` 与 `build_checkpoint_record(...)`，沿用 `scheduler_snapshot` 的 bootstrap 模式，先验证 plan / session / journal / replay / snapshot，再检查 source format chain、package identity、workflow / session / run / input 对齐，以及 replay / snapshot 与 execution order 的 prefix 关系
2. 同一 bootstrap 现已把 `SchedulerSnapshot` 稳定投影为 checkpoint-facing record：completed 映射到 `TerminalCompleted`，failed 映射到 `TerminalFailed` + `WorkflowFailure` blocker，partial runnable 映射到 `ReadyToPersist`，waiting / not-checkpoint-friendly 路径映射到 `Blocked`，并最终复用 `validate_checkpoint_record(...)` 做落地后校验
3. `tests/checkpoint_record/record.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 deterministic bootstrap regression，覆盖 project success、failed、partial 与 scheduler snapshot workflow mismatch 负例，并接入 `ahfl-v0.11` / `v0.11-checkpoint-record-bootstrap` 标签

## [x] Issue 07

标题：
增加 checkpoint record 输出路径

背景：
如果 checkpoint-facing 状态只停留在内部对象，future persistence prototype、CI 与 reviewer 都无法稳定消费 checkpoint 语义。

目标：

1. 扩展 CLI / backend 输出 checkpoint record
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 checkpoint record golden

验收标准：

1. CLI 可输出稳定 checkpoint record 结果
2. 输出覆盖 checkpoint identity、persistable prefix、resume basis 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/backends/checkpoint_record.hpp` 与 `src/backends/checkpoint_record.cpp`，提供 `print_checkpoint_record_json(...)`，把 checkpoint identity、source format chain、workflow / snapshot / checkpoint status、persistable prefix、resume candidate / blocker 与 basis kind 以稳定 JSON 形式输出
2. `src/cli/ahflc.cpp` 已新增 `emit-checkpoint-record` 子命令，沿用 `emit-scheduler-snapshot` 的 bootstrap 路径构建 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord`，并把 `--package` / `--capability-mocks` / `--input-fixture` / `--run-id` / `--workflow` 的参数校验同步扩展到新命令
3. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 与 `tests/checkpoint/` 已新增 single-file success、project failed、workspace partial 三类 emission regression，并接入 `ahfl-v0.11` / `v0.11-checkpoint-record-emission` 标签，确保输出路径不回退成隐式约定

## [x] Issue 08

标题：
增加 checkpoint review / resume preview surface

背景：
checkpoint prototype 若只有 machine-facing record，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能持久化、以后如何 resume”。

目标：

1. 增加 checkpoint-facing reviewer summary
2. 覆盖 persistable prefix、resume basis、resume blocker、checkpoint boundary 与 next-step recommendation
3. 保持 summary 只消费正式 checkpoint-facing model，而不私造 recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source checkpoint record 一致
3. review surface 不倒灌 host telemetry、store metadata 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/checkpoint_record/review.hpp` 与 `src/checkpoint_record/review.cpp`，定义 `CheckpointReviewSummary`、`CheckpointReviewNextActionKind`、`validate_checkpoint_review_summary(...)` 与 `build_checkpoint_review_summary(...)`，summary 只消费已验证 `CheckpointRecord`，覆盖 persistable prefix、resume candidate / blocker、checkpoint boundary、resume preview、terminal reason 与 next-step recommendation
2. 已新增 `include/ahfl/backends/checkpoint_review.hpp` 与 `src/backends/checkpoint_review.cpp`，提供 contributor-facing review printer；`src/cli/ahflc.cpp` 也已新增 `emit-checkpoint-review` 子命令，并把 `--package` / `--capability-mocks` / `--input-fixture` / `--run-id` / `--workflow` 的参数边界同步扩展到新命令
3. `tests/checkpoint_record/record.cpp`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 与 `tests/checkpoint/` 已新增 direct review regression 与 CLI review regression，覆盖 completed、failed、partial、invalid-record、compat 负例，以及 single-file / project / workspace 三类 review 输出

## [x] Issue 09

标题：
增加 single-file / project / workspace checkpoint golden 回归

背景：
若 checkpoint prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 固定 single-file checkpoint golden
2. 固定 project / workspace checkpoint golden
3. 保持 success / partial / failure-path 都被覆盖

验收标准：

1. golden 可独立运行
2. `--package` 路径与 project/workspace 路径都被覆盖
3. checkpoint-facing 变更能被稳定 diff

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. `tests/checkpoint/` 现已新增并固定 six-file checkpoint golden 样本：single-file success、project failed、workspace partial，分别覆盖 `checkpoint-record` JSON 与 `checkpoint-review` readable summary 两类输出
2. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已把 `emit-checkpoint-record` / `emit-checkpoint-review` 的 `--package`、project 与 workspace 路径都接入 expected-output 对比，确保 checkpoint-facing 变更可以稳定 diff
3. `tests/cmake/LabelTests.cmake` 已新增 `v0.11-checkpoint-golden` 独立标签，把 record/review 的 six-file golden regression 收口为单独可运行测试集，满足“golden 可独立运行”的验收要求

## [x] Issue 10

标题：
冻结 checkpoint record / review compatibility 契约

背景：
当 record / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把 durable recovery 字段静默塞回当前 checkpoint prototype。

目标：

1. 冻结 `CheckpointRecord` 与 `CheckpointReviewSummary` 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 record / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md` 已从最小设计入口收口为逐 artifact compatibility contract，正式冻结 `CheckpointRecord` 的 `ahfl.checkpoint-record.v1` 与 `CheckpointReviewSummary` 的 `ahfl.checkpoint-review.v1` 版本入口、source artifact 依赖方向，以及 consumer 的最小 version gate 顺序
2. 同一 compatibility 文档已分别列出 record / review 的稳定字段边界、明确不承诺字段、兼容扩展条件与 breaking-change 触发点，避免后续把 durable checkpoint id、resume token、store metadata 或 recovery daemon state 提前塞进当前版本
3. 文档还已补齐 checkpoint-facing 变更同步流程与回归锚点，明确改稳定字段时必须同步 `tests/checkpoint_record/record.cpp`、`tests/checkpoint/`、`tests/cmake/LabelTests.cmake` 与 roadmap / backlog

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future recovery boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future persistence prototype 仍需要知道 checkpoint-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.11 checkpoint-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future recovery boundary guidance

验收标准：

1. 有独立的 V0.11 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.11.zh.md`，正式把 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointReviewSummary`、`AuditReport` 与 `DryRunTrace` 的稳定输入、边界职责、推荐消费顺序与反模式统一收口到 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> checkpoint-review`
2. 已新增 `docs/reference/contributor-guide-v0.11.zh.md`，给出 checkpoint record / review 的入口文件、推荐扩展顺序、future recovery boundary guidance 与最小验证清单，并把 `v0.11-checkpoint-record-model`、`v0.11-checkpoint-record-bootstrap`、`v0.11-checkpoint-review-model` 与 `v0.11-checkpoint-golden` 固定为当前主链路回归锚点
3. `README.md` 与 `docs/README.md` 已把 current documentation index 切到 V0.11 的 checkpoint compatibility / consumer matrix / contributor guide，旧的 V0.10 matrix / guide 则保留为 completed baseline

## [x] Issue 12

标题：
建立 V0.11 checkpoint prototype regression、CI 与 reference 文档闭环

背景：
checkpoint-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 record、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 checkpoint prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.11 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.11 checkpoint prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.11 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `tests/checkpoint_record/record.cpp`、`tests/checkpoint/`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 checkpoint-facing direct / emission / golden regression 收口到 `v0.11-checkpoint-record-model`、`v0.11-checkpoint-record-bootstrap`、`v0.11-checkpoint-record-emission`、`v0.11-checkpoint-review-model`、`v0.11-checkpoint-review-emission` 与 `v0.11-checkpoint-golden`，并统一归入 `ahfl-v0.11` umbrella label
2. `.github/workflows/ci.yml` 已显式增加 “Run V0.11 Checkpoint Prototype Regression” 步骤，在常规 `test-dev` 与 `test-asan` 两条主 CI 链路中先运行 `ctest --preset ... -L 'ahfl-v0.11'`，再继续全量回归，确保 V0.11 checkpoint prototype 有单独的持续回归入口
3. `docs/plan/roadmap-v0.11.zh.md`、`README.md` 与现有 V0.11 reference 文档已同步到闭环状态：roadmap / backlog 现都标记 V0.11 全部 issue 完成，README 的 CI 说明也已明确包含 checkpoint regression slice，而 compatibility / consumer matrix / contributor guide 继续作为 checkpoint-facing reference 基线
