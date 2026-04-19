# AHFL V0.14 Issue Backlog

本文给出与 [roadmap-v0.14.zh.md](./roadmap-v0.14.zh.md) 对齐的 issue-ready backlog。目标是把 V0.13 已完成的 export manifest / review artifact，继续推进为 future durable store adapter 可稳定消费的 local store-import descriptor 边界，而不是直接进入 production durable persistence / recovery 系统。

## [ ] Issue 01

标题：
冻结 V0.14 store import prototype scope 与 non-goals

背景：
V0.13 已经把 export manifest / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable store import executor、object storage layout、transaction commit protocol 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.14 的成功标准是 store-import-facing local prototype artifact
2. 明确 V0.14 不交付真实 durable store import executor、resume token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 store import descriptor 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 store import descriptor / staging blocker / staging artifact set 术语
2. 文档明确 future durable store adapter 不得跳过现有 runtime、checkpoint、persistence 与 export artifact
3. backlog 不把 deployment、telemetry、durable persistence、provider connector 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 尚未开始

## [ ] Issue 02

标题：
冻结 store import descriptor / staging artifact set 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `PersistenceExportManifest` / `PersistenceExportReviewSummary`，但还没有正式的 store-import-facing 状态对象；future durable store adapter 很容易私造 staging descriptor、artifact mapping、adapter readiness 与 blocker 语义。

目标：

1. 定义 store import descriptor 顶层状态集合
2. 定义 store import candidate identity、staging artifact set、adapter blocker 与 local/store boundary 的最小字段
3. 冻结 store-import-facing `format_version` 的兼容入口与 future durable store adapter / recovery 边界

验收标准：

1. 文档明确哪些字段属于 store-import-facing 稳定输入
2. descriptor 与 export manifest / review / future durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/`

当前实现备注：

1. 尚未开始

## [ ] Issue 03

标题：
冻结 export manifest、store import descriptor 与 future durable store adapter 的分层关系

背景：
若 V0.14 没先冻结 layering，后续很容易在 store import review 输出或 CLI 层直接偷塞 store URI、object path、resume token、import receipt、replication metadata 或 host-side payload。

目标：

1. 明确 store import descriptor 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest
2. 明确 store import review summary 与 export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 store import descriptor 与 future durable store adapter / recovery protocol 的兼容关系

验收标准：

1. export manifest / store import descriptor / review / future store adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 尚未开始

## [ ] Issue 04

标题：
引入 store import descriptor 数据模型

背景：
要让 future durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 store import candidate identity / staging artifact set / adapter blocker 状态。

目标：

1. 引入 store-import-facing 顶层 descriptor / staging artifact set 对象
2. 覆盖 store import candidate identity、source version chain、staging artifact entry、import-ready 与 adapter blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct store import descriptor model regression
2. store import descriptor 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 尚未开始

## [ ] Issue 05

标题：
增加 store import descriptor validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 store import candidate identity、staging artifact set 与上游 export manifest 保持一致。

目标：

1. 为 store import descriptor 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 store-import-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. store import descriptor 与 export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 尚未开始

## [ ] Issue 06

标题：
增加 deterministic store import descriptor bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `PersistenceExportManifest` 及其上游 artifact 投影为 store-import-facing descriptor。

目标：

1. 基于 V0.13 已冻结 artifact 构建 deterministic store import descriptor
2. 覆盖 success、partial 与 failure-path 的 store-import-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 descriptor-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. store import descriptor 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest 保持一致
3. failure 归因不会被 export review / persistence review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 尚未开始

## [ ] Issue 07

标题：
增加 store import descriptor 输出路径

背景：
如果 store-import-facing 状态只停留在内部对象，future durable store adapter、CI 与 reviewer 都无法稳定消费 staging artifact set 语义。

目标：

1. 扩展 CLI / backend 输出 store import descriptor
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 store import descriptor golden

验收标准：

1. CLI 可输出稳定 store import descriptor 结果
2. 输出覆盖 candidate identity、staging artifact set、adapter blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 尚未开始

## [ ] Issue 08

标题：
增加 store import review / staging preview surface

背景：
store import prototype 若只有 machine-facing descriptor，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable store import handoff、后续 durable store adapter 应看什么”。

目标：

1. 增加 store-import-facing reviewer summary
2. 覆盖 candidate identity、staging artifact set、adapter blocker、store boundary 与 next-step recommendation
3. 保持 summary 只消费正式 store import descriptor model，而不私造 durable store / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source store import descriptor 一致
3. review surface 不倒灌 host telemetry、store metadata、replication metadata、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 尚未开始

## [ ] Issue 09

标题：
增加 single-file / project / workspace store import golden 回归

背景：
若 store import prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 固定 single-file store import golden
2. 固定 project / workspace store import golden
3. 保持 success / partial / failure-path 都被覆盖

验收标准：

1. golden 可独立运行
2. `--package` 路径与 project / workspace 路径都被覆盖
3. store-import-facing 变更能被稳定 diff

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 尚未开始

## [ ] Issue 10

标题：
冻结 store import descriptor / review compatibility 契约

背景：
当 descriptor / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把 durable store / recovery 字段静默塞回当前 store import prototype。

目标：

1. 冻结 `StoreImportDescriptor` 与 store import review summary 的正式版本入口
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

1. 尚未开始

## [ ] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future durable store adapter 仍需要知道 store-import-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.14 store-import-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future durable store adapter / recovery boundary guidance

验收标准：

1. 有独立的 V0.14 consumer matrix / contributor guide
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
建立 V0.14 store import prototype regression、CI 与 reference 文档闭环

背景：
store-import-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 descriptor、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 store import prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.14 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.14 store import prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.14 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. 尚未开始
