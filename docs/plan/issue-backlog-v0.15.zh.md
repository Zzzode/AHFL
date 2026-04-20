# AHFL V0.15 Issue Backlog

本文给出与 [roadmap-v0.15.zh.md](./roadmap-v0.15.zh.md) 对齐的 issue-ready backlog。目标是把 V0.14 已完成的 store import descriptor / review artifact，继续推进为 future real durable store adapter 可稳定消费的 local durable-store-import request 边界，而不是直接进入 production durable store import / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.15 durable store import prototype scope 与 non-goals

背景：
V0.14 已经把 store import descriptor / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable store import executor、object uploader、database writer、transaction commit protocol 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.15 的成功标准是 durable-store-import-facing local prototype artifact
2. 明确 V0.15 不交付真实 durable store import executor、import receipt、resume token、crash recovery、distributed recovery daemon 或 object storage layout
3. 明确 durable store import request 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 durable store import request / adapter blocker / requested artifact set 术语
2. 文档明确 future real durable store adapter 不得跳过现有 runtime、checkpoint、persistence、export 与 store import artifact
3. backlog 不把 deployment、telemetry、durable persistence、provider connector 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md`，明确 V0.15 当前只交付 durable-store-import-facing local prototype artifact，不交付真实 durable store import executor、import receipt、resume token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 durable store import prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable store import request` 这条正式链路之上，并显式禁止回退依赖 store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.15.zh.md` 当前已同步把 V0.15 的 next step 切到 `Issue 04-06`，确保后续先进入 durable store import request 最小模型实现，而不是继续在 scope 未冻结前扩张私有 durable store 语义

## [x] Issue 02

标题：
冻结 durable store import request / requested artifact set 的最小模型与 adapter 边界

背景：
当前仓库虽已有 `StoreImportDescriptor` / `StoreImportReviewSummary`，但还没有正式的 durable-store-import-facing状态对象；future real durable store adapter 很容易私造 adapter request、artifact role mapping、adapter readiness 与 blocker 语义。

目标：

1. 定义 durable-store-import-facing 顶层 request / requested artifact set 对象
2. 定义 durable store import request identity、requested artifact set、adapter blocker 与 local/adapter boundary 的最小字段
3. 冻结 durable-store-import-facing `format_version` 的兼容入口与 future real durable store adapter / recovery 边界

验收标准：

1. 文档明确哪些字段属于 durable-store-import-facing 稳定输入
2. request 与 store import descriptor / review / future real durable store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. 已新增 `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md`，给出 `DurableStoreImportRequest` 的 `ahfl.durable-store-import-request.v1` 与 future `DurableStoreImportReviewSummary` 的 `ahfl.durable-store-import-review.v1` 版本入口基线，同时明确 durable-store-import-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable store import request` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：durable store import request identity、requested artifact set entries、request boundary、adapter readiness、adapter blocker 与 source artifact version chain，并明确 durable checkpoint id、resume token、store URI、object path、database key、import receipt、store metadata 仍不属于当前版本稳定承诺
3. `docs/plan/roadmap-v0.15.zh.md` 当前已同步收口 M0，并把 V0.15 的 next step 切到 `Issue 04-06`，确保后续进入 direct model、validation 与 bootstrap 实现，而不是在 versioning 入口未冻结前扩张私有 adapter 语义

## [x] Issue 03

标题：
冻结 store import descriptor、durable store import request 与 future real durable store adapter 的分层关系

背景：
若 V0.15 没先冻结 layering，后续很容易在 durable store import review 输出或 CLI 层直接偷塞 store URI、object path、resume token、import receipt、replication metadata 或 host-side payload。

目标：

1. 明确 durable store import request 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor
2. 明确 durable store import review summary 与 store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 durable store import request 与 future real durable store adapter / recovery protocol 的兼容关系

验收标准：

1. store import descriptor / durable store import request / review / future adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `StoreImportDescriptor` 仍是 durable store import request 的直接 machine-facing 上游，而 future `DurableStoreImportReviewSummary` 仍只是 projection，不得承接 durable mutation、resume token 派发、import receipt synthesis 或 recovery command synthesis
2. `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 store import review / export review / persistence review / checkpoint review / scheduler review / audit / trace 反向成为 durable store import request 的第一输入，或让 request / review 提前承诺真实 durable store / recovery ABI，通常就应视为 versioning 事件
3. `docs/plan/roadmap-v0.15.zh.md` 当前已同步把 V0.15 的 next step 切到 `Issue 04-06`，确保后续先进入 durable store import request 的 direct model、validation 与 bootstrap，而不是继续在 layering 未冻结前扩张私有 store 语义

## [x] Issue 04

标题：
引入 durable store import request 数据模型

背景：
要让 future real durable store adapter 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 durable store import request identity / requested artifact set / adapter blocker 状态。

目标：

1. 引入 durable-store-import-facing 顶层 request / requested artifact set 对象
2. 覆盖 durable store import request identity、source version chain、requested artifact entry、adapter-ready 与 adapter blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct durable store import request model regression
2. durable store import request 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/durable_store_import/request.hpp`，冻结 `DurableStoreImportRequest` direct model、`ahfl.durable-store-import-request.v1` 版本入口、source artifact version chain、request identity、requested artifact set、adapter-ready、request boundary、request status 与 adapter blocker 字段
2. 已新增 `src/durable_store_import/request.cpp` 与 `src/durable_store_import/CMakeLists.txt`，把 durable store import request 作为独立模块接入 `ahfl_compiler`，并保持第一输入为 V0.14 `StoreImportDescriptor`
3. requested artifact set 当前显式包含 `StoreImportDescriptor` request anchor 以及 descriptor staging artifact set 投影出的 export manifest、persistence descriptor、persistence review / checkpoint record 等 adapter-facing entry，不回退依赖 review、trace、audit 或 CLI 文本

## [x] Issue 05

标题：
增加 durable store import request validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 durable store import request identity、requested artifact set 与上游 store import descriptor 保持一致。

目标：

1. 为 durable store import request 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 durable-store-import-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. durable store import request 与 store import descriptor / export manifest / persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已实现 `validate_durable_store_import_request`，覆盖 format version、source version chain、identity、requested artifact set entry count / duplicate logical name、adapter-ready / blocker、request status 与 boundary consistency 检查
2. 已新增 `tests/durable_store_import/request.cpp` 的 direct validation 回归，覆盖 ready、blocked、terminal failed、terminal partial 正例，以及 missing request identity、duplicate artifact name、ready with blocker、unsupported source descriptor format、adapter-contract blocked、terminal failed without failure summary 等负例
3. 已在 `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 接入 `ahfl.durable_store_import_request.model.*`，并打上 `ahfl-v0.15` / `v0.15-durable-store-import-request-model` 标签

## [x] Issue 06

标题：
增加 deterministic durable store import request bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `StoreImportDescriptor` 及其上游 artifact 投影为 durable-store-import-facing request。

目标：

1. 基于 V0.14 已冻结 artifact 构建 deterministic durable store import request
2. 覆盖 success、partial 与 failure-path 的 durable-store-import-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 request-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. durable store import request 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor / export manifest / store import descriptor 保持一致
3. failure 归因不会被 store import review / export review / persistence review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已实现 `build_durable_store_import_request(const StoreImportDescriptor&)`，在 bootstrap 前先校验上游 descriptor，并从正式 `StoreImportDescriptor` deterministic 投影 request identity、requested artifact set、adapter blocker、request boundary 与 request status
2. 已覆盖 ready descriptor、terminal completed descriptor 与 invalid descriptor bootstrap 回归；blocked、failed、partial 分支也通过 direct model 构造路径验证 adapter blocker 与 request status 映射
3. 已新增 `ahfl.durable_store_import_request.bootstrap.*` 测试并打上 `ahfl-v0.15` / `v0.15-durable-store-import-request-bootstrap` 标签，当前 focused regression 使用 `ctest --preset test-dev -L ahfl-v0.15 --output-on-failure` 通过

## [x] Issue 07

标题：
增加 durable store import request 输出路径

背景：
如果 durable-store-import-facing 状态只停留在内部对象，future real durable store adapter、CI 与 reviewer 都无法稳定消费 requested artifact set 语义。

目标：

1. 扩展 CLI / backend 输出 durable store import request
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 durable store import request golden

验收标准：

1. CLI 可输出稳定 durable store import request 结果
2. 输出覆盖 request identity、requested artifact set、adapter blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/backends/durable_store_import_request.hpp` 与 `src/backends/durable_store_import_request.cpp`，以 JSON 形式输出 `DurableStoreImportRequest` 的 source version chain、request identity、requested artifact set、adapter-ready、request boundary、request status 与 adapter blocker
2. 已在 `src/cli/ahflc.cpp` 接入 `emit-durable-store-import-request`，复用正式 package / dry-run / journal / replay / scheduler / checkpoint / persistence / export / store import descriptor 链路，不从 review、trace、AST 或 CLI 文本倒推 request
3. `src/backends/CMakeLists.txt` 与 `src/cli/ahflc.cpp` 已接入 durable store import request backend / command，single-file、project、workspace 与 `--package` 路径均可输出

## [x] Issue 08

标题：
增加 durable store import review / request preview surface

背景：
durable store import prototype 若只有 machine-facing request，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable durable store import handoff、后续 real durable store adapter 应看什么”。

目标：

1. 增加 durable-store-import-facing reviewer summary
2. 覆盖 request identity、requested artifact set、adapter blocker、adapter boundary 与 next-step recommendation
3. 保持 summary 只消费正式 durable store import request model，而不私造真实 durable store / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source durable store import request 一致
3. review surface 不倒灌 host telemetry、store metadata、replication metadata、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/durable_store_import/review.hpp` 与 `src/durable_store_import/review.cpp`，冻结 `DurableStoreImportReviewSummary` 的 reviewer-facing direct model、validation 与 request-based projection
2. 已新增 `include/ahfl/backends/durable_store_import_review.hpp` 与 `src/backends/durable_store_import_review.cpp`，输出 request identity、requested artifact preview、adapter blocker、adapter boundary、request preview 与 next-step recommendation
3. 已在 `src/cli/ahflc.cpp` 接入 `emit-durable-store-import-review`；review summary 只消费正式 `DurableStoreImportRequest`，不承诺 store URI、object path、credential、replication metadata、host telemetry 或真实 recovery ABI

## [x] Issue 09

标题：
增加 single-file / project / workspace durable store import golden 回归

背景：
若 durable store import prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 durable store import request / review 增加 single-file、project、workspace golden 基线
2. 固定 completed、failed、partial 三类 durable-store-import-facing CLI 输出
3. 把 V0.15 durable store import regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-durable-store-import-request` 与 `emit-durable-store-import-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 request identity、requested artifact set / preview、boundary、blocker 与 next-step recommendation
3. durable store import CLI 输出已纳入 `ahfl-v0.15` 与专用 `v0.15-durable-store-import-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. 已新增 `tests/durable_store_import/*.durable-store-import-request.json` 与 `tests/durable_store_import/*.durable-store-import-review`，覆盖 single-file completed、project failed 与 workspace partial 三类 durable-store-import-facing CLI golden
2. 已在 `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 接入 `emit-durable-store-import-request` / `emit-durable-store-import-review` golden regression
3. 已在 `tests/cmake/LabelTests.cmake` 接入 `v0.15-durable-store-import-emission` 与 `v0.15-durable-store-import-golden` 标签；当前 `ctest --preset test-dev -L ahfl-v0.15 --output-on-failure` 覆盖 24 条测试并通过

## [x] Issue 10

标题：
冻结 durable store import request / review compatibility 契约

背景：
当 request / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把真实 durable store / recovery 字段静默塞回当前 durable store import prototype。

目标：

1. 冻结 `DurableStoreImportRequest` 与 durable store import review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 request / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md` 已补全 request / review 分节、source version 校验顺序、breaking-change 基线、non-breaking extension 候选，以及文档 / 代码 / golden / tests 的同步规则
2. compatibility 文档已把 `emit-durable-store-import-request` / `emit-durable-store-import-review`、`tests/durable_store_import/`、`tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml` 纳入正式收口锚点
3. V0.15 当前最小回归切片已冻结为 `v0.15-durable-store-import-request-model`、`v0.15-durable-store-import-request-bootstrap`、`v0.15-durable-store-import-review-model`、`v0.15-durable-store-import-emission`、`v0.15-durable-store-import-golden` 与 `ahfl-v0.15`

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future real durable store adapter 仍需要知道 durable-store-import-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.15 durable-store-import-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future real durable store adapter / recovery boundary guidance

验收标准：

1. 有独立的 V0.15 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.15.zh.md`，冻结 `StoreImportDescriptor -> DurableStoreImportRequest -> DurableStoreImportReviewSummary` 的 consumer layering、推荐消费顺序、扩展模板与 future real durable store adapter 落点
2. 已新增 `docs/reference/contributor-guide-v0.15.zh.md`，给出本地八条典型路径、按改动类型找入口、最小验证清单、反模式与文档测试联动约束
3. `README.md` 与 `docs/README.md` 已同步纳入 V0.15 durable-store-import-facing consumer guidance 入口，同时保持 root README 只保留少量推荐入口，不回退成全量文档目录

## [x] Issue 12

标题：
建立 V0.15 durable store import prototype regression、CI 与 reference 文档闭环

背景：
durable-store-import-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 request、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 durable store import prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.15 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.15 durable store import prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.15 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `tests/durable_store_import/`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已形成 direct / golden / label 闭环
2. `.github/workflows/ci.yml` 已显式增加 `ctest --preset test-dev --output-on-failure -L 'ahfl-v0.15'` 与 `ctest --preset test-asan --output-on-failure -L 'ahfl-v0.15'`
3. `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md`、`docs/reference/native-consumer-matrix-v0.15.zh.md`、`docs/reference/contributor-guide-v0.15.zh.md`、`README.md`、`docs/README.md`、`docs/plan/roadmap-v0.15.zh.md` 与本 backlog 已形成文档闭环
