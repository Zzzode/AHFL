# AHFL V0.19 Issue Backlog

本文给出与 [roadmap-v0.19.zh.md](./roadmap-v0.19.zh.md) 对齐的 issue-ready backlog。目标是把 V0.18 已完成的 receipt persistence request / review artifact，继续推进为 future real durable store adapter 可稳定消费的 local durable adapter receipt persistence response 边界，而不是直接进入 production durable store adapter / persistence executor / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.19 durable adapter receipt persistence response prototype scope 与 non-goals

背景：
V0.18 已经把 receipt persistence request / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 persistence writer、persistence id、resume token、retry token 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.19 的成功标准是 durable-adapter-receipt-persistence-response-facing local prototype artifact
2. 明确 V0.19 不交付真实 persistence writer、persistence id、resume token、retry token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 durable adapter receipt persistence response 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt -> durable receipt persistence request` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 durable adapter receipt persistence response / capability gap / response blocker 术语
2. 文档明确 future real durable store adapter 不得跳过现有 runtime、checkpoint、persistence、export、store import、durable request、durable decision、durable receipt 与 durable receipt persistence request artifact
3. backlog 不把 deployment、telemetry、provider connector、durable persistence executor 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md`，明确 V0.19 当前只交付 durable-adapter-receipt-persistence-response-facing local prototype artifact，不交付真实 persistence writer、persistence id、resume token、retry token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 durable adapter receipt persistence response prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt -> durable receipt persistence request -> durable receipt persistence response` 这条正式链路之上，并显式禁止回退依赖 durable receipt persistence review、durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. 上述 scope 冻结已作为后续 `Issue 04-12` 的约束前提，确保实现阶段不回退扩张私有 durable adapter 语义

## [x] Issue 02

标题：
冻结 durable adapter receipt persistence response / capability gap 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `PersistenceRequest` / `PersistenceReviewSummary`，但还没有正式的 durable-adapter-receipt-persistence-response-facing 状态对象；future real durable store adapter 很容易私造 response、capability gap、accept / block / defer / reject 语义。

目标：

1. 定义 durable-adapter-receipt-persistence-response-facing 顶层 response 对象
2. 定义 durable receipt persistence response identity、response outcome、capability gap、response blocker 与 local/adapter boundary 的最小字段
3. 冻结 durable-adapter-receipt-persistence-response-facing `format_version` 的兼容入口与 future real durable store adapter / persistence executor / recovery 边界

验收标准：

1. 文档明确哪些字段属于 durable-adapter-receipt-persistence-response-facing 稳定输入
2. response 与 persistence request / review / future real durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已在 `docs/design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md`、`docs/reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md` 与 `include/ahfl/durable_store_import/receipt_persistence_response.hpp` 中冻结 `Response`、response outcome、capability gap、response blocker 与 local / adapter boundary 的最小模型
2. `format_version = "ahfl.durable-store-import-decision-receipt-persistence-response.v1"` 已成为 durable-adapter-receipt-persistence-response-facing consumer 的正式版本入口
3. 文档已明确 response 是 machine-facing 第一事实来源，review summary 只做 reviewer-facing projection

## [x] Issue 03

标题：
冻结 persistence request、persistence response 与 future real durable store adapter 的分层关系

背景：
若 V0.19 没先冻结 layering，后续很容易在 durable receipt persistence response review 输出或 CLI 层直接偷塞 store URI、object path、persistence id、resume token、retry token、executor payload 或 host-side payload。

目标：

1. 明确 durable adapter receipt persistence response 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision / durable receipt / durable receipt persistence request
2. 明确 durable receipt persistence response review summary 与 durable receipt persistence review / durable receipt review / durable decision review / durable request review / store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 durable receipt persistence response 与 future real durable store adapter / persistence executor / recovery protocol 的兼容关系

验收标准：

1. persistence request / persistence response / review / future adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 durable receipt persistence review、durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已在 V0.19 design、compatibility、consumer matrix 与 contributor guide 中冻结 `PersistenceRequest -> Response -> ResponseReviewSummary` 的分层关系
2. 文档已显式禁止从 review、trace、AST、host log 或 provider payload 回推 response machine state
3. 当前 response / review 仍不承诺真实 store URI、object path、persistence id、resume token、retry token、executor payload 或 host-side payload

## [x] Issue 04

标题：
引入 durable adapter receipt persistence response 数据模型

背景：
要让 future real durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 response identity / capability gap / response blocker 状态。

目标：

1. 引入 durable-adapter-receipt-persistence-response-facing 顶层 response 对象
2. 覆盖 response identity、source version chain、response outcome、acknowledged-for-response 与 response blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct durable adapter receipt persistence response model regression
2. durable receipt persistence response 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/durable_store_import/receipt_persistence_response.hpp` 与 `src/durable_store_import/receipt_persistence_response.cpp`
2. `PersistenceResponse` 已覆盖 response identity、source version chain、response outcome、acknowledged-for-response、next required capability 与 response blocker
3. direct model validation 入口 `validate_persistence_response(...)` 已落地，并保留 legacy alias 以兼容既有命名

## [x] Issue 05

标题：
增加 durable adapter receipt persistence response validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 response identity、response outcome 与上游 persistence request 保持一致。

目标：

1. 为 durable adapter receipt persistence response 增加 direct validation
2. 覆盖 accepted / blocked / deferred / rejected 场景
3. 固定 durable-adapter-receipt-persistence-response-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. durable receipt persistence response 与 persistence request / durable receipt / durable decision / durable request / store import descriptor / export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已增加 accepted、blocked、deferred、rejected 四类 response direct validation 回归
2. 已增加缺失 response identity、unsupported source request format、accepted response 携带 blocker 等负例回归
3. direct tests 已通过 `v0.19-durable-store-import-receipt-persistence-response-model` 标签纳入 CTest

## [x] Issue 06

标题：
增加 deterministic durable adapter receipt persistence response bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `PersistenceRequest` 及其上游 artifact 投影为 durable-adapter-receipt-persistence-response-facing response。

目标：

1. 基于 V0.18 已冻结 artifact 构建 deterministic durable adapter receipt persistence response
2. 覆盖 accepted、blocked、deferred 与 rejected 的 durable-adapter-receipt-persistence-response-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 response-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. durable receipt persistence response 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor / durable request / durable decision / durable receipt / durable receipt persistence request 保持一致
3. failure 归因不会被 durable receipt persistence review / durable receipt review / durable decision review / durable request review / store import review / export review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已新增 `build_persistence_response(...)`，从 `PersistenceRequest` deterministic 构建 durable adapter receipt persistence response
2. bootstrap 已固定 ready request 与 invalid request 的正负例回归
3. bootstrap tests 已通过 `v0.19-durable-store-import-receipt-persistence-response-bootstrap` 标签纳入 CTest

## [x] Issue 07

标题：
增加 durable adapter receipt persistence response 输出路径

背景：
如果 adapter-receipt-persistence-response-facing 状态只停留在内部对象，future real durable store adapter、CI 与 reviewer 都无法稳定消费 response outcome / capability gap 语义。

目标：

1. 扩展 CLI / backend 输出 durable adapter receipt persistence response
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 accepted / blocked / deferred / rejected 的 durable adapter receipt persistence response golden

验收标准：

1. CLI 可输出稳定 durable adapter receipt persistence response 结果
2. 输出覆盖 response identity、response outcome、capability gap、response blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `emit-durable-store-import-receipt-persistence-response` CLI / backend 输出路径
2. single-file、project、workspace 与 `--package` 三类路径均已有 golden 回归
3. 输出已覆盖 response identity、response outcome、capability gap、response blocker 与 boundary summary

## [x] Issue 08

标题：
增加 durable adapter receipt persistence response review / response preview surface

背景：
durable adapter receipt persistence response prototype 若只有 machine-facing response，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断"现在能不能形成 stable adapter response handoff、后续 real durable store adapter 应看什么"。

目标：

1. 增加 durable-adapter-receipt-persistence-response-facing reviewer summary
2. 覆盖 response identity、response outcome、capability gap、response boundary 与 next-step recommendation
3. 保持 summary 只消费正式 durable adapter receipt persistence response model，而不私造真实 durable store adapter / persistence executor / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source durable receipt persistence response 一致
3. review surface 不倒灌 host telemetry、store metadata、persistence payload、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `ResponseReviewSummary`、`build_persistence_response_review(...)` 与 `validate_persistence_response_review(...)`
2. 已新增 `emit-durable-store-import-receipt-persistence-response-review` reviewer-facing 输出路径
3. review summary 只消费正式 `Response` model，不私造真实 durable store adapter / persistence executor / recovery 状态机

## [x] Issue 09

标题：
增加 single-file / project / workspace durable adapter receipt persistence response golden 回归

背景：
若 durable adapter receipt persistence response prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 durable adapter receipt persistence response / review 增加 single-file、project、workspace golden 基线
2. 固定 single-file / project / workspace durable-adapter-receipt-persistence-response-facing CLI 输出，并通过 direct model regression 覆盖 accepted、blocked、deferred、rejected 四类 response 路径
3. 把 V0.19 durable adapter receipt persistence response regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-durable-store-import-receipt-persistence-response` 与 `emit-durable-store-import-receipt-persistence-response-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 response identity、response outcome / preview、boundary、blocker 与 next-step recommendation
3. durable adapter receipt persistence response direct / CLI / golden regression 将纳入 `ahfl-v0.19` 与专用 `v0.19-durable-store-import-receipt-persistence-response-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 已新增 response / response review 的 single-file、project、workspace golden 文件
2. `emit-durable-store-import-receipt-persistence-response` 与 `emit-durable-store-import-receipt-persistence-response-review` 已纳入 `v0.19-durable-store-import-receipt-persistence-response-emission` 与 `v0.19-durable-store-import-receipt-persistence-response-golden`
3. accepted / rejected 已由 CLI golden 覆盖；blocked / deferred 已由 direct model regression 固定，避免为当前 source-to-artifact 链路引入私有 CLI fixture

## [x] Issue 10

标题：
冻结 durable adapter receipt persistence response / review compatibility 契约

背景：
当 response / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把真实 durable adapter / persistence executor 字段静默塞回当前 durable adapter receipt persistence response prototype。

目标：

1. 冻结 `Response` 与 durable adapter receipt persistence response review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 response / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. 已新增 `docs/reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md`
2. compatibility 文档已按 `Response` 与 `ResponseReviewSummary` 分节冻结版本入口、source artifact 校验顺序、稳定字段边界与 breaking-change 规则
3. 文档已明确 consumer 必须检查 source version chain，不能靠字段集合猜测语义

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future real durable store adapter 仍需要知道 adapter-receipt-persistence-response-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.19 durable-adapter-receipt-persistence-response-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future real durable store adapter / persistence executor / recovery boundary guidance

验收标准：

1. 有独立的 V0.19 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.19.zh.md`
2. 已新增 `docs/reference/contributor-guide-v0.19.zh.md`
3. future real durable store adapter / persistence executor / recovery boundary guidance 已明确落在 `Response` 与 `ResponseReviewSummary` 之后，且不得越界引入真实 store metadata / credential / operator payload

## [x] Issue 12

标题：
建立 V0.19 durable adapter receipt persistence response prototype regression、CI 与 reference 文档闭环

背景：
adapter-receipt-persistence-response-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 response、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 durable adapter receipt persistence response prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.19 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.19 durable adapter receipt persistence response prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.19 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `tests/` 已具备 response / review direct tests、CLI emission tests 与 golden regression
2. `.github/workflows/ci.yml` 已显式执行 `ahfl-v0.19` dev / ASan 标签切片后再继续全量回归
3. `README.md`、`docs/README.md`、roadmap、backlog 与 reference docs 已同步到 V0.19 当前完成状态
