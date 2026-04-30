# AHFL V0.16 Issue Backlog

本文给出与 [roadmap-v0.16.zh.md](./roadmap-v0.16.zh.md) 对齐的 issue-ready backlog。目标是把 V0.15 已完成的 durable request / review artifact，继续推进为 future real durable store adapter 可稳定消费的 local durable adapter decision 边界，而不是直接进入 production durable store adapter / receipt / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.16 durable adapter decision prototype scope 与 non-goals

背景：
V0.15 已经把 durable request / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable store adapter、object uploader、database writer、import receipt、resume token、retry token 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.16 的成功标准是 durable-adapter-decision-facing local prototype artifact
2. 明确 V0.16 不交付真实 durable store adapter、import receipt、resume token、retry token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 durable adapter decision 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 durable adapter decision / capability gap / decision blocker 术语
2. 文档明确 future real durable store adapter 不得跳过现有 runtime、checkpoint、persistence、export、store import 与 durable request artifact
3. backlog 不把 deployment、telemetry、provider connector、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md`，明确 V0.16 当前只交付 durable-adapter-decision-facing local prototype artifact，不交付真实 durable store adapter、import receipt、resume token、retry token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 durable adapter decision prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision` 这条正式链路之上，并显式禁止回退依赖 durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.16.zh.md` 当前已同步把 V0.16 的 next step 切到 `Issue 04-06`，确保后续先进入 durable adapter decision 最小模型实现，而不是继续在 scope 未冻结前扩张私有 durable adapter 语义

## [x] Issue 02

标题：
冻结 durable adapter decision / capability gap 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `Request` / `ReviewSummary`，但还没有正式的 durable-adapter-decision-facing 状态对象；future real durable store adapter 很容易私造 adapter decision、capability gap、accept / defer / reject 语义。

目标：

1. 定义 durable-adapter-decision-facing 顶层 decision 对象
2. 定义 durable decision identity、decision outcome、capability gap、decision blocker 与 local/adapter boundary 的最小字段
3. 冻结 durable-adapter-decision-facing `format_version` 的兼容入口与 future real durable store adapter / receipt / recovery 边界

验收标准：

1. 文档明确哪些字段属于 durable-adapter-decision-facing 稳定输入
2. decision 与 durable request / review / future real durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已新增 `docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md`，给出 `Decision` 的 `ahfl.durable-store-import-decision.v1` 与 future `DecisionReviewSummary` 的 `ahfl.durable-store-import-decision-review.v1` 版本入口基线，同时明确 adapter-decision-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：durable decision identity、decision outcome、decision boundary、capability gap、decision blocker 与 source artifact version chain，并明确真实 import receipt id、resume token、store URI、object path、database key、executor payload 仍不属于当前版本稳定承诺
3. `docs/plan/roadmap-v0.16.zh.md` 当前已同步收口 M0，并把 V0.16 的 next step 切到 `Issue 04-06`，确保后续进入 direct model、validation 与 bootstrap 实现，而不是在 versioning 入口未冻结前扩张私有 adapter 语义

## [x] Issue 03

标题：
冻结 durable request、durable decision 与 future real durable store adapter 的分层关系

背景：
若 V0.16 没先冻结 layering，后续很容易在 durable decision review 输出或 CLI 层直接偷塞 store URI、object path、receipt id、resume token、retry token、executor payload 或 host-side payload。

目标：

1. 明确 durable adapter decision 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request
2. 明确 durable decision review summary 与 durable request review / store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 durable decision 与 future real durable store adapter / receipt / recovery protocol 的兼容关系

验收标准：

1. durable request / durable decision / review / future adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `Request` 仍是 durable decision 的直接 machine-facing 上游，而 future `DecisionReviewSummary` 仍只是 projection，不得承接真实 import receipt synthesis、resume token 派发、store mutation execution 或 recovery command synthesis
2. `docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、audit / trace 反向成为 durable decision 的第一输入，或让 decision / review 提前承诺真实 durable store adapter / receipt / recovery ABI，通常就应视为 versioning 事件
3. `docs/plan/roadmap-v0.16.zh.md` 当前已同步把 V0.16 的 next step 切到 `Issue 04-06`，确保后续先进入 durable adapter decision 的 direct model、validation 与 bootstrap，而不是继续在 layering 未冻结前扩张私有 adapter 语义

## [x] Issue 04

标题：
引入 durable adapter decision 数据模型

背景：
要让 future real durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 durable decision identity / capability gap / decision blocker 状态。

目标：

1. 引入 durable-adapter-decision-facing 顶层 decision 对象
2. 覆盖 durable decision identity、source version chain、decision outcome、accepted-for-future-execution 与 decision blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct durable adapter decision model regression
2. durable decision 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/durable_store_import/decision.hpp`，冻结 `Decision` direct model、`ahfl.durable-store-import-decision.v1` 版本入口、source artifact version chain、decision identity、decision boundary、decision status、decision outcome、accepted-for-future-execution、next required adapter capability 与 decision blocker 字段
2. 已新增 `src/durable_store_import/decision.cpp`，把 durable adapter decision 作为 `ahfl_durable_store_import` 模块的一部分接入，并保持第一输入为 V0.15 `Request`
3. direct model 当前显式保留 `Request` 的 source version chain、workflow / checkpoint / persistence / manifest / descriptor / request status 上游信息，不回退依赖 review、trace、audit 或 CLI 文本

## [x] Issue 05

标题：
增加 durable adapter decision validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 durable decision identity、decision outcome 与上游 durable request 保持一致。

目标：

1. 为 durable adapter decision 增加 direct validation
2. 覆盖 accepted / blocked / deferred / rejected 场景
3. 固定 durable-adapter-decision-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. durable decision 与 durable request / store import descriptor / export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已实现 `validate_durable_store_import_decision`，覆盖 format version、source version chain、identity、decision boundary、decision status / outcome、accepted-for-future-execution、next required adapter capability 与 decision blocker consistency 检查
2. 已新增 `tests/durable_store_import/decision.cpp` 的 direct validation 回归，覆盖 accepted、blocked、deferred、rejected 正例，以及 missing decision identity、accepted with blocker、unsupported source request format、adapter-decision-consumable blocked、rejected without failure summary 等负例
3. 已在 `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 接入 `ahfl.durable_store_import_decision.model.*`，并打上 `ahfl-v0.16` / `v0.16-durable-store-import-decision-model` 标签

## [x] Issue 06

标题：
增加 deterministic durable adapter decision bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `Request` 及其上游 artifact 投影为 durable-adapter-decision-facing decision。

目标：

1. 基于 V0.15 已冻结 artifact 构建 deterministic durable adapter decision
2. 覆盖 accepted、blocked、deferred 与 rejected 的 durable-adapter-decision-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 decision-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. durable decision 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request 保持一致
3. failure 归因不会被 durable request review / store import review / export review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已实现 `build_durable_store_import_decision(const DurableStoreImportRequest&)`，在 bootstrap 前先校验上游 request，并从正式 `Request` deterministic 投影 decision identity、decision outcome、decision boundary、next required adapter capability 与 decision blocker
2. 已覆盖 ready request、terminal completed request 与 invalid request bootstrap 回归；blocked、failed、partial 分支也通过 direct model 构造路径验证 capability gap 与 decision status 映射
3. 已新增 `ahfl.durable_store_import_decision.bootstrap.*` 测试并打上 `ahfl-v0.16` / `v0.16-durable-store-import-decision-bootstrap` 标签，当前 focused regression 使用 `ctest --preset test-dev -L ahfl-v0.16 --output-on-failure` 通过

## [x] Issue 07

标题：
增加 durable adapter decision 输出路径

背景：
如果 adapter-decision-facing 状态只停留在内部对象，future real durable store adapter、CI 与 reviewer 都无法稳定消费 decision outcome / capability gap 语义。

目标：

1. 扩展 CLI / backend 输出 durable adapter decision
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 accepted / blocked / deferred / rejected 的 durable adapter decision golden

验收标准：

1. CLI 可输出稳定 durable adapter decision 结果
2. 输出覆盖 decision identity、decision outcome、capability gap、decision blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `src/backends/durable_store_import_decision.cpp` 与 `include/ahfl/backends/durable_store_import_decision.hpp`，固定 durable adapter decision 的 machine-facing JSON 输出，覆盖 source version chain、decision identity、decision outcome、boundary、capability gap 与 decision blocker
2. `src/cli/ahflc.cpp` 已接入 `emit-durable-store-import-decision`，并复用正式 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> PersistenceDescriptor -> ExportManifest -> StoreImportDescriptor -> DurableStoreImportRequest -> DurableStoreImportDecision` 构建链路
3. `src/backends/CMakeLists.txt`、`src/durable_store_import/CMakeLists.txt` 与 `tests/cmake/*` 已同步接线，确保 single-file、project、workspace 与 `--package` 路径可稳定输出 durable adapter decision

## [x] Issue 08

标题：
增加 durable adapter decision review / decision preview surface

背景：
durable adapter decision prototype 若只有 machine-facing decision，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable adapter decision handoff、后续 real durable store adapter 应看什么”。

目标：

1. 增加 durable-adapter-decision-facing reviewer summary
2. 覆盖 decision identity、decision outcome、capability gap、decision boundary 与 next-step recommendation
3. 保持 summary 只消费正式 durable adapter decision model，而不私造真实 durable store adapter / receipt / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source durable decision 一致
3. review surface 不倒灌 host telemetry、store metadata、receipt payload、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/durable_store_import/decision_review.hpp` 与 `src/durable_store_import/decision_review.cpp`，冻结 `ahfl.durable-store-import-decision-review.v1` reviewer-facing summary、next-action、decision preview 与 next-step recommendation 语义
2. 已新增 `src/backends/durable_store_import_decision_review.cpp` 与 `include/ahfl/backends/durable_store_import_decision_review.hpp`，固定文本 review 输出风格，并通过 `emit-durable-store-import-decision-review` 暴露给 CLI
3. `tests/durable_store_import/decision.cpp` 已新增 direct review model / bootstrap regression，覆盖 validate ok、unsupported source decision format、accepted decision review、rejected decision review 与 invalid decision 负例

## [x] Issue 09

标题：
增加 single-file / project / workspace durable adapter decision golden 回归

背景：
若 durable adapter decision prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 durable adapter decision / review 增加 single-file、project、workspace golden 基线
2. 固定 accepted、blocked、deferred、rejected 四类 durable-adapter-decision-facing CLI 输出
3. 把 V0.16 durable adapter decision regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-durable-store-import-decision` 与 `emit-durable-store-import-decision-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 decision identity、decision outcome / preview、boundary、blocker 与 next-step recommendation
3. durable adapter decision CLI 输出将纳入 `ahfl-v0.16` 与专用 `v0.16-durable-store-adapter-decision-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 已新增 `tests/durable_store_import/*durable-store-import-decision*.json` 与 `tests/durable_store_import/*durable-store-import-decision-review` golden 基线，覆盖 single-file accepted-completed、project rejected 与 workspace accepted-ready 三条正式 CLI 路径
2. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已接入 `emit-durable-store-import-decision` / `emit-durable-store-import-decision-review` 回归，并增加 `v0.16-durable-store-import-decision-emission`、`v0.16-durable-store-import-decision-golden`、`v0.16-durable-store-import-decision-review-model` 标签
3. 当前仓库尚无自然 end-to-end blocked / deferred fixture；这两类语义仍通过 `ahfl.durable_store_import_decision.model.*` direct regression 固定。focused 回归 `ctest --preset test-dev --output-on-failure -L ahfl-v0.16` 当前通过

## [x] Issue 10

标题：
冻结 durable adapter decision / review compatibility 契约

背景：
当 decision / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把真实 durable adapter / receipt 字段静默塞回当前 durable adapter decision prototype。

目标：

1. 冻结 `Decision` 与 durable adapter decision review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 decision / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md` 已按 artifact 分节冻结 `Decision` 与 `DecisionReviewSummary` 的正式版本入口、source version 检查顺序、稳定字段边界与 breaking-change 候选
2. compatibility 文档已明确 consumer 需先检查 `format_version` 与 source version chain，再消费 decision / review 语义，禁止通过字段集合猜测版本
3. 文档已补齐 compatibility 与实现联动边界，明确哪些变化应触发 version bump，满足 Issue 10 验收标准

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future real durable store adapter 仍需要知道 adapter-decision-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.16 durable-adapter-decision-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future real durable store adapter / receipt / recovery boundary guidance

验收标准：

1. 有独立的 V0.16 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.16.zh.md`，独立冻结 durable-adapter-decision-facing consumer 矩阵、推荐消费顺序、扩展模板与反模式
2. 已新增 `docs/reference/contributor-guide-v0.16.zh.md`，独立给出 V0.16 contributor 入口、最小验证清单与 future real durable store adapter boundary guidance
3. `README.md` 与 `docs/README.md` 已同步把 V0.16 matrix / contributor guide 纳入当前入口索引

## [x] Issue 12

标题：
建立 V0.16 durable adapter decision prototype regression、CI 与 reference 文档闭环

背景：
adapter-decision-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 decision、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 durable adapter decision prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.16 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.16 durable adapter decision prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.16 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. V0.16 durable adapter decision 的 direct / emission / golden regression 已在 `tests/` 与 `tests/cmake/` 完成并打上 `ahfl-v0.16` 与 `v0.16-durable-store-import-decision-*` 标签
2. `.github/workflows/ci.yml` 已新增 `Run V0.16 Durable Adapter Decision Prototype Regression`（dev 与 asan）步骤，显式先跑 `-L 'ahfl-v0.16'` 再继续全量回归
3. roadmap / backlog / README / docs index / reference 链接已同步到 V0.16 decision 闭环，满足 Issue 12 文档收口条件
