# AHFL V0.17 Issue Backlog

本文给出与 [roadmap-v0.17.zh.md](./roadmap-v0.17.zh.md) 对齐的 issue-ready backlog。目标是把 V0.16 已完成的 durable decision / review artifact，继续推进为 future real durable store adapter 可稳定消费的 local durable adapter receipt 边界，而不是直接进入 production durable store adapter / receipt persistence / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.17 durable adapter receipt prototype scope 与 non-goals

背景：
V0.16 已经把 durable decision / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable store adapter executor、object uploader、database writer、receipt persistence、resume token、retry token 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.17 的成功标准是 durable-adapter-receipt-facing local prototype artifact
2. 明确 V0.17 不交付真实 durable store adapter executor、receipt persistence、resume token、retry token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 durable adapter receipt 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 durable adapter receipt / capability gap / receipt blocker 术语
2. 文档明确 future real durable store adapter 不得跳过现有 runtime、checkpoint、persistence、export、store import、durable request 与 durable decision artifact
3. backlog 不把 deployment、telemetry、provider connector、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md`，明确 V0.17 当前只交付 durable-adapter-receipt-facing local prototype artifact，不交付真实 durable store adapter executor、receipt persistence、resume token、retry token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 durable adapter receipt prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt` 这条正式链路之上，并显式禁止回退依赖 durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. 上述 scope 冻结已作为后续 `Issue 04-12` 的约束前提，确保实现阶段不回退扩张私有 durable adapter 语义

## [x] Issue 02

标题：
冻结 durable adapter receipt / capability gap 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `DurableStoreImportDecision` / `DurableStoreImportDecisionReviewSummary`，但还没有正式的 durable-adapter-receipt-facing 状态对象；future real durable store adapter 很容易私造 adapter receipt、capability gap、archive / defer / reject 语义。

目标：

1. 定义 durable-adapter-receipt-facing 顶层 receipt 对象
2. 定义 durable receipt identity、receipt outcome、capability gap、receipt blocker 与 local/adapter boundary 的最小字段
3. 冻结 durable-adapter-receipt-facing `format_version` 的兼容入口与 future real durable store adapter / receipt persistence / recovery 边界

验收标准：

1. 文档明确哪些字段属于 durable-adapter-receipt-facing 稳定输入
2. receipt 与 durable decision / review / future real durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已新增 `docs/reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md`，给出 `DurableStoreImportDecisionReceipt` 的 `ahfl.durable-store-import-decision-receipt.v1` 与 future `DurableStoreImportDecisionReceiptReviewSummary` 的 `ahfl.durable-store-import-decision-receipt-review.v1` 版本入口基线，同时明确 adapter-receipt-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：durable receipt identity、receipt outcome、receipt boundary、capability gap、receipt blocker 与 source artifact version chain，并明确真实 import receipt persistence id、resume token、store URI、object path、database key、executor payload 仍不属于当前版本稳定承诺
3. 该版本入口与边界冻结已作为后续 `Issue 04-12` 的兼容约束，防止在实现阶段静默扩张私有 adapter 语义

## [x] Issue 03

标题：
冻结 durable decision、durable receipt 与 future real durable store adapter 的分层关系

背景：
若 V0.17 没先冻结 layering，后续很容易在 durable receipt review 输出或 CLI 层直接偷塞 store URI、object path、receipt id、resume token、retry token、executor payload 或 host-side payload。

目标：

1. 明确 durable adapter receipt 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision
2. 明确 durable receipt review summary 与 durable decision review / durable request review / store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 durable receipt 与 future real durable store adapter / receipt persistence / recovery protocol 的兼容关系

验收标准：

1. durable decision / durable receipt / review / future adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `DurableStoreImportDecision` 仍是 durable receipt 的直接 machine-facing 上游，而 future `DurableStoreImportDecisionReceiptReviewSummary` 仍只是 projection，不得承接真实 import receipt persistence、resume token 派发、store mutation execution 或 recovery command synthesis
2. `docs/reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、audit / trace 反向成为 durable receipt 的第一输入，或让 receipt / review 提前承诺真实 durable store adapter / receipt persistence / recovery ABI，通常就应视为 versioning 事件
3. layering 约束已作为后续 `Issue 04-12` 的工程基线，确保实现阶段不绕过 durable decision / receipt 的正式分层

## [x] Issue 04

标题：
引入 durable adapter receipt 数据模型

背景：
要让 future real durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 durable receipt identity / capability gap / receipt blocker 状态。

目标：

1. 引入 durable-adapter-receipt-facing 顶层 receipt 对象
2. 覆盖 durable receipt identity、source version chain、receipt outcome、accepted-for-receipt-archive 与 receipt blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct durable adapter receipt model regression
2. durable receipt 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `DurableStoreImportDecisionReceipt` 模型头文件与实现：`include/ahfl/durable_store_import/receipt.hpp`、`src/durable_store_import/receipt.cpp`，并冻结 `ahfl.durable-store-import-decision-receipt.v1` 版本入口
2. 该模型已覆盖 receipt identity、source version chain、receipt status / outcome、accepted-for-receipt-archive、receipt boundary、next required capability 与 receipt blocker
3. 已通过 `tests/durable_store_import/decision.cpp` 接入 direct model regression，并在 `tests/cmake/ProjectTests.cmake` 中注册模型测试入口

## [x] Issue 05

标题：
增加 durable adapter receipt validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 durable receipt identity、receipt outcome 与上游 durable decision 保持一致。

目标：

1. 为 durable adapter receipt 增加 direct validation
2. 覆盖 receipt-ready / blocked / deferred / rejected 场景
3. 固定 durable-adapter-receipt-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. durable receipt 与 durable decision / durable request / store import descriptor / export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已为 durable adapter receipt 增加 direct validation，覆盖 `ready / blocked / deferred / rejected` 四类场景
2. 已固定负例 diagnostics 回归，覆盖 `missing receipt identity`、`unsupported source decision format` 与 `ready with blocker` 约束
3. validation 回归已纳入 `v0.17-durable-store-import-receipt-model` 标签，并包含在 `ahfl-v0.17` 切片中

## [x] Issue 06

标题：
增加 deterministic durable adapter receipt bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `DurableStoreImportDecision` 及其上游 artifact 投影为 durable-adapter-receipt-facing receipt。

目标：

1. 基于 V0.16 已冻结 artifact 构建 deterministic durable adapter receipt
2. 覆盖 receipt-ready、blocked、deferred 与 rejected 的 durable-adapter-receipt-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 receipt-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. durable receipt 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision 保持一致
3. failure 归因不会被 durable decision review / durable request review / store import review / export review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已新增 deterministic bootstrap：`build_durable_store_import_decision_receipt(const DurableStoreImportDecision&)`
2. bootstrap direct 回归已覆盖 `ready / blocked / deferred / rejected` 与 `invalid decision` 失败归因
3. bootstrap 逻辑严格消费 `DurableStoreImportDecision` 与其 source version chain，不回退读取 review / trace / CLI 文本

## [x] Issue 07

标题：
增加 durable adapter receipt 输出路径

背景：
如果 adapter-receipt-facing 状态只停留在内部对象，future real durable store adapter、CI 与 reviewer 都无法稳定消费 receipt outcome / capability gap 语义。

目标：

1. 扩展 CLI / backend 输出 durable adapter receipt
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 receipt-ready / blocked / deferred / rejected 的 durable adapter receipt golden

验收标准：

1. CLI 可输出稳定 durable adapter receipt 结果
2. 输出覆盖 receipt identity、receipt outcome、capability gap、receipt blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 receipt backend 输出：`include/ahfl/backends/durable_store_import_receipt.hpp`、`src/backends/durable_store_import_receipt.cpp`
2. `ahflc` 已接入 `emit-durable-store-import-receipt`，覆盖 single-file、project、workspace 与 `--package` 路径
3. receipt 输出已形成对应 golden 基线并接入 CLI regression

## [x] Issue 08

标题：
增加 durable adapter receipt review / receipt preview surface

背景：
durable adapter receipt prototype 若只有 machine-facing receipt，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable adapter receipt handoff、后续 real durable store adapter 应看什么”。

目标：

1. 增加 durable-adapter-receipt-facing reviewer summary
2. 覆盖 receipt identity、receipt outcome、capability gap、receipt boundary 与 next-step recommendation
3. 保持 summary 只消费正式 durable adapter receipt model，而不私造真实 durable store adapter / receipt persistence / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source durable receipt 一致
3. review surface 不倒灌 host telemetry、store metadata、receipt payload、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `DurableStoreImportDecisionReceiptReviewSummary` 模型与实现：`include/ahfl/durable_store_import/receipt_review.hpp`、`src/durable_store_import/receipt_review.cpp`
2. 已新增 receipt-review backend 输出：`include/ahfl/backends/durable_store_import_receipt_review.hpp`、`src/backends/durable_store_import_receipt_review.cpp`
3. `ahflc` 已接入 `emit-durable-store-import-receipt-review`，并具备 direct / CLI review regression

## [x] Issue 09

标题：
增加 single-file / project / workspace durable adapter receipt golden 回归

背景：
若 durable adapter receipt prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 durable adapter receipt / review 增加 single-file、project、workspace golden 基线
2. 固定 receipt-ready、blocked、deferred、rejected 四类 durable-adapter-receipt-facing CLI 输出
3. 把 V0.17 durable adapter receipt regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-durable-store-import-receipt` 与 `emit-durable-store-import-receipt-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 receipt identity、receipt outcome / preview、boundary、blocker 与 next-step recommendation
3. durable adapter receipt CLI 输出将纳入 `ahfl-v0.17` 与专用 `v0.17-durable-store-import-receipt-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 已新增 receipt / receipt-review golden 文件：single-file、project（failed）与 workspace（partial）三条路径均已固定
2. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 已接入对应 CLI regression 与标签切片
3. `ctest --preset test-dev -L ahfl-v0.17 --output-on-failure` 已通过，验证 receipt model/bootstrap/review/emission/golden 闭环

## [x] Issue 10

标题：
冻结 durable adapter receipt / review compatibility 契约

背景：
当 receipt / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把真实 durable adapter / receipt persistence 字段静默塞回当前 durable adapter receipt prototype。

目标：

1. 冻结 `DurableStoreImportDecisionReceipt` 与 durable adapter receipt review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 receipt / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md` 已按 receipt / receipt-review artifact 分节冻结正式版本入口
2. 文档已冻结 source artifact 版本校验顺序、稳定字段边界与 breaking-change 基线
3. 文档已明确 docs / code / golden / tests 的联动更新约束，避免跨层静默扩张 receipt 契约

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future real durable store adapter 仍需要知道 adapter-receipt-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.17 durable-adapter-receipt-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future real durable store adapter / receipt persistence / recovery boundary guidance

验收标准：

1. 有独立的 V0.17 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.17.zh.md`，冻结 durable-receipt-facing consumer 矩阵、推荐消费顺序与反模式
2. 已新增 `docs/reference/contributor-guide-v0.17.zh.md`，补齐 contributor 入口、改动映射与最小验证清单
3. 已同步更新 `README.md` 与 `docs/README.md`，把 V0.17 matrix / contributor guide 纳入当前入口

## [x] Issue 12

标题：
建立 V0.17 durable adapter receipt prototype regression、CI 与 reference 文档闭环

背景：
adapter-receipt-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 receipt、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 durable adapter receipt prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.17 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.17 durable adapter receipt prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.17 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `tests/` 与 `tests/cmake/` 已形成 V0.17 receipt / receipt-review direct、CLI、golden 与标签切片闭环
2. `.github/workflows/ci.yml` 已显式加入 `ahfl-v0.17` 回归步骤（dev 与 asan 预设），再继续全量测试
3. `docs/plan/`、`README.md`、`docs/README.md` 与 `docs/reference/` 已同步更新，完成文档与回归入口收口
