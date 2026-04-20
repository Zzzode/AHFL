# AHFL V0.18 Issue Backlog

本文给出与 [roadmap-v0.18.zh.md](./roadmap-v0.18.zh.md) 对齐的 issue-ready backlog。目标是把 V0.17 已完成的 durable receipt / receipt review artifact，继续推进为 future real durable store adapter 可稳定消费的 local durable adapter receipt persistence 边界，而不是直接进入 production durable store adapter / receipt persistence / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.18 durable adapter receipt persistence prototype scope 与 non-goals

背景：
V0.17 已经把 durable receipt / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 receipt persistence writer、object uploader、database writer、receipt persistence id、resume token、retry token 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.18 的成功标准是 durable-adapter-receipt-persistence-facing local prototype artifact
2. 明确 V0.18 不交付真实 receipt persistence writer、receipt persistence id、resume token、retry token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 durable adapter receipt persistence 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 durable adapter receipt persistence / capability gap / persistence blocker 术语
2. 文档明确 future real durable store adapter 不得跳过现有 runtime、checkpoint、persistence、export、store import、durable request、durable decision 与 durable receipt artifact
3. backlog 不把 deployment、telemetry、provider connector、durable persistence 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md`，明确 V0.18 当前只交付 durable-adapter-receipt-persistence-facing local prototype artifact，不交付真实 receipt persistence writer、receipt persistence id、resume token、retry token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 durable adapter receipt persistence prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt -> durable receipt persistence request` 这条正式链路之上，并显式禁止回退依赖 durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. 上述 scope 冻结已作为后续 `Issue 04-12` 的约束前提，确保实现阶段不回退扩张私有 durable adapter 语义

## [x] Issue 02

标题：
冻结 durable adapter receipt persistence / capability gap 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `DurableStoreImportDecisionReceipt` / `DurableStoreImportDecisionReceiptReviewSummary`，但还没有正式的 durable-adapter-receipt-persistence-facing 状态对象；future real durable store adapter 很容易私造 persistence request、capability gap、persist / defer / reject 语义。

目标：

1. 定义 durable-adapter-receipt-persistence-facing 顶层 persistence request 对象
2. 定义 durable receipt persistence request identity、persistence outcome、capability gap、persistence blocker 与 local/adapter boundary 的最小字段
3. 冻结 durable-adapter-receipt-persistence-facing `format_version` 的兼容入口与 future real durable store adapter / receipt persistence / recovery 边界

验收标准：

1. 文档明确哪些字段属于 durable-adapter-receipt-persistence-facing 稳定输入
2. persistence request 与 durable receipt / review / future real durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已新增 `docs/reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md`，给出 `DurableStoreImportDecisionReceiptPersistenceRequest` 的 `ahfl.durable-store-import-decision-receipt-persistence-request.v1` 与 future `DurableStoreImportDecisionReceiptPersistenceReviewSummary` 的 `ahfl.durable-store-import-decision-receipt-persistence-review.v1` 版本入口基线，同时明确 adapter-receipt-persistence-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt -> durable receipt persistence request` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：durable receipt persistence request identity、persistence outcome、persistence boundary、capability gap、persistence blocker 与 source artifact version chain，并明确真实 import receipt persistence id、resume token、store URI、object path、database key、executor payload 仍不属于当前版本稳定承诺
3. 该版本入口与边界冻结已作为后续 `Issue 04-12` 的兼容约束，防止在实现阶段静默扩张私有 adapter 语义

## [x] Issue 03

标题：
冻结 durable receipt、durable receipt persistence request 与 future real durable store adapter 的分层关系

背景：
若 V0.18 没先冻结 layering，后续很容易在 durable receipt persistence review 输出或 CLI 层直接偷塞 store URI、object path、receipt persistence id、resume token、retry token、executor payload 或 host-side payload。

目标：

1. 明确 durable adapter receipt persistence 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision / durable receipt
2. 明确 durable receipt persistence review summary 与 durable receipt review / durable decision review / durable request review / store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 durable receipt persistence request 与 future real durable store adapter / receipt persistence / recovery protocol 的兼容关系

验收标准：

1. durable receipt / durable receipt persistence request / review / future adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `DurableStoreImportDecisionReceipt` 仍是 durable receipt persistence request 的直接 machine-facing 上游，而 future `DurableStoreImportDecisionReceiptPersistenceReviewSummary` 仍只是 projection，不得承接真实 import receipt persistence、resume token 派发、store mutation execution 或 recovery command synthesis
2. `docs/reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、audit / trace 反向成为 durable receipt persistence request 的第一输入，或让 persistence request / review 提前承诺真实 durable store adapter / receipt persistence / recovery ABI，通常就应视为 versioning 事件
3. layering 约束已作为后续 `Issue 04-12` 的工程基线，确保实现阶段不绕过 durable receipt / receipt persistence request 的正式分层

## [x] Issue 04

标题：
引入 durable adapter receipt persistence 数据模型

背景：
要让 future real durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 durable receipt persistence identity / capability gap / persistence blocker 状态。

目标：

1. 引入 durable-adapter-receipt-persistence-facing 顶层 persistence request 对象
2. 覆盖 durable receipt persistence identity、source version chain、persistence outcome、accepted-for-receipt-persistence 与 persistence blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct durable adapter receipt persistence model regression
2. durable receipt persistence request 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `DurableStoreImportDecisionReceiptPersistenceRequest` 模型头文件与实现：`include/ahfl/durable_store_import/receipt_persistence.hpp`、`src/durable_store_import/receipt_persistence.cpp`，并冻结 `ahfl.durable-store-import-decision-receipt-persistence-request.v1` 版本入口
2. 该模型已覆盖 persistence request identity、source version chain、persistence status / outcome、accepted-for-receipt-persistence、persistence boundary、next required capability 与 persistence blocker
3. 已通过 `tests/durable_store_import/decision.cpp` 接入 direct model regression，并在 `tests/cmake/ProjectTests.cmake` 中注册模型测试入口

## [x] Issue 05

标题：
增加 durable adapter receipt persistence validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 durable receipt persistence identity、persistence outcome 与上游 durable receipt 保持一致。

目标：

1. 为 durable adapter receipt persistence 增加 direct validation
2. 覆盖 persistence-ready / blocked / deferred / rejected 场景
3. 固定 durable-adapter-receipt-persistence-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. durable receipt persistence request 与 durable receipt / durable decision / durable request / store import descriptor / export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已为 durable adapter receipt persistence 增加 direct validation，覆盖 `ready / blocked / deferred / rejected` 四类场景
2. 已固定负例 diagnostics 回归，覆盖 `missing request identity`、`unsupported source receipt format` 与 `ready with blocker` 约束
3. validation 回归已纳入 `v0.18-durable-store-import-receipt-persistence-model` 标签，并包含在 `ahfl-v0.18` 切片中

## [x] Issue 06

标题：
增加 deterministic durable adapter receipt persistence bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `DurableStoreImportDecisionReceipt` 及其上游 artifact 投影为 durable-adapter-receipt-persistence-facing request。

目标：

1. 基于 V0.17 已冻结 artifact 构建 deterministic durable adapter receipt persistence request
2. 覆盖 persistence-ready、blocked、deferred 与 rejected 的 durable-adapter-receipt-persistence-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 persistence-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. durable receipt persistence request 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision / durable receipt 保持一致
3. failure 归因不会被 durable receipt review / durable decision review / durable request review / store import review / export review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已新增 deterministic bootstrap：`build_durable_store_import_decision_receipt_persistence_request(const DurableStoreImportDecisionReceipt&)`
2. bootstrap direct 回归已覆盖 `ready / blocked / deferred / rejected` 与 `invalid receipt` 失败归因
3. bootstrap 逻辑严格消费 `DurableStoreImportDecisionReceipt` 与其 source version chain，不回退读取 review / trace / CLI 文本

## [x] Issue 07

标题：
增加 durable adapter receipt persistence 输出路径

背景：
如果 adapter-receipt-persistence-facing 状态只停留在内部对象，future real durable store adapter、CI 与 reviewer 都无法稳定消费 persistence outcome / capability gap 语义。

目标：

1. 扩展 CLI / backend 输出 durable adapter receipt persistence request
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 persistence-ready / blocked / deferred / rejected 的 durable adapter receipt persistence golden

验收标准：

1. CLI 可输出稳定 durable adapter receipt persistence 结果
2. 输出覆盖 persistence request identity、persistence outcome、capability gap、persistence blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 durable adapter receipt persistence request JSON backend 输出：`include/ahfl/backends/durable_store_import_receipt_persistence_request.hpp` 与 `src/backends/durable_store_import_receipt_persistence_request.cpp`，覆盖 persistence request identity、persistence outcome、capability gap、persistence blocker 与 boundary 字段映射
2. 已在 CLI 增加 `emit-durable-store-import-receipt-persistence-request` 子命令，并打通 `request -> receipt -> decision -> durable request` 链路 helper 与 diagnostics 透传
3. `src/backends/CMakeLists.txt`、`src/cli/ahflc.cpp` 与相关参数校验/usage 文本已完成 wiring；single-file/project/workspace 路径均可稳定输出 persistence request artifact

## [x] Issue 08

标题：
增加 durable adapter receipt persistence review / persistence preview surface

背景：
durable adapter receipt persistence prototype 若只有 machine-facing persistence request，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable adapter receipt persistence handoff、后续 real durable store adapter 应看什么”。

目标：

1. 增加 durable-adapter-receipt-persistence-facing reviewer summary
2. 覆盖 persistence request identity、persistence outcome、capability gap、persistence boundary 与 next-step recommendation
3. 保持 summary 只消费正式 durable adapter receipt persistence model，而不私造真实 durable store adapter / receipt persistence / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source durable receipt persistence request 一致
3. review surface 不倒灌 host telemetry、store metadata、receipt payload、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 direct reviewer model：`include/ahfl/durable_store_import/receipt_persistence_review.hpp` 与 `src/durable_store_import/receipt_persistence_review.cpp`，并冻结 `ahfl.durable-store-import-decision-receipt-persistence-review.v1`
2. review summary 仅消费 `DurableStoreImportDecisionReceiptPersistenceRequest`，覆盖 next_action、persistence_preview、next_step_recommendation、contract summary 与 blocker/failure 透传，不回退依赖 review/trace/host payload
3. 已新增 reviewer 输出 backend：`include/ahfl/backends/durable_store_import_receipt_persistence_review.hpp` 与 `src/backends/durable_store_import_receipt_persistence_review.cpp`，并在 CLI 增加 `emit-durable-store-import-receipt-persistence-review` 子命令
4. direct regression 已覆盖 `validate_ok / unsupported_source_request_format / ready_request / rejected_request / invalid_request`

## [x] Issue 09

标题：
增加 single-file / project / workspace durable adapter receipt persistence golden 回归

背景：
若 durable adapter receipt persistence prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 durable adapter receipt persistence request / review 增加 single-file、project、workspace golden 基线
2. 固定 persistence-ready、blocked、deferred、rejected 四类 durable-adapter-receipt-persistence-facing CLI 输出
3. 把 V0.18 durable adapter receipt persistence regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-durable-store-import-receipt-persistence-request` 与 `emit-durable-store-import-receipt-persistence-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 persistence request identity、persistence outcome / preview、boundary、blocker 与 next-step recommendation
3. durable adapter receipt persistence CLI 输出将纳入 `ahfl-v0.18` 与专用 `v0.18-durable-store-import-receipt-persistence-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 已新增 6 份 golden：single-file/project/workspace 的 durable-store-import-receipt-persistence-request 与 durable-store-import-receipt-persistence-review 输出
2. 已在 `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 增加对应 CLI 回归，并在 `tests/cmake/LabelTests.cmake` 增加 `v0.18-durable-store-import-receipt-persistence-emission`、`v0.18-durable-store-import-receipt-persistence-golden` 与 `v0.18-durable-store-import-receipt-persistence-review-model`
3. 当前回归状态：`ctest --preset test-dev -L ahfl-v0.18 --output-on-failure` 与 `ctest --preset test-dev -L ahfl-v0.17 --output-on-failure` 均已通过

## [ ] Issue 10

标题：
冻结 durable adapter receipt persistence / review compatibility 契约

背景：
当 persistence request / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把真实 durable adapter / receipt persistence 字段静默塞回当前 durable adapter receipt persistence prototype。

目标：

1. 冻结 `DurableStoreImportDecisionReceiptPersistenceRequest` 与 durable adapter receipt persistence review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 persistence request / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. 尚未开始

## [ ] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future real durable store adapter 仍需要知道 adapter-receipt-persistence-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.18 durable-adapter-receipt-persistence-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future real durable store adapter / receipt persistence / recovery boundary guidance

验收标准：

1. 有独立的 V0.18 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 尚未开始

## [ ] Issue 12

标题：
建立 V0.18 durable adapter receipt persistence prototype regression、CI 与 reference 文档闭环

背景：
adapter-receipt-persistence-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 persistence request、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 durable adapter receipt persistence prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.18 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.18 durable adapter receipt persistence prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.18 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. 尚未开始
