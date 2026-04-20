# AHFL V0.14 Issue Backlog

本文给出与 [roadmap-v0.14.zh.md](./roadmap-v0.14.zh.md) 对齐的 issue-ready backlog。目标是把 V0.13 已完成的 export manifest / review artifact，继续推进为 future durable store adapter 可稳定消费的 local store-import descriptor 边界，而不是直接进入 production durable persistence / recovery 系统。

## [x] Issue 01

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

1. 已新增 `docs/design/native-store-import-prototype-bootstrap-v0.14.zh.md`，明确 V0.14 当前只交付 store-import-facing local prototype artifact，不交付 durable store import executor、resume token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 store import prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor` 这条正式链路之上，并显式禁止回退依赖 export review、persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.14.zh.md` 当前已同步把 V0.14 的 next step 切到 `Issue 04-06`，确保后续先进入 store import descriptor 最小模型实现，而不是继续在 scope 未冻结前扩张私有 durable store 语义

## [x] Issue 02

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

1. 已新增 `docs/reference/store-import-prototype-compatibility-v0.14.zh.md`，给出 `StoreImportDescriptor` 的 `ahfl.store-import-descriptor.v1` 与 future `StoreImportReviewSummary` 的 `ahfl.store-import-review.v1` 计划版本入口基线，同时明确 store-import-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：store import candidate identity、staging artifact set entries、descriptor boundary、import readiness、staging blocker 与 source artifact version chain，并明确 durable checkpoint id、resume token、store URI、object path、database key、import receipt、store metadata 仍不属于当前版本稳定承诺
3. `docs/plan/roadmap-v0.14.zh.md` 当前已同步收口 M0，并把 V0.14 的 next step 切到 `Issue 04-06`，确保后续进入 direct model、validation 与 bootstrap 实现，而不是在 versioning 入口未冻结前扩张私有 staging 语义

## [x] Issue 03

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

1. `docs/design/native-store-import-prototype-bootstrap-v0.14.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `PersistenceExportManifest` 仍是 store import descriptor 的直接 machine-facing 上游，而 `StoreImportReviewSummary` 仍只是 projection，不得承接 durable store mutation、resume token 派发、import receipt synthesis 或 recovery command synthesis
2. `docs/reference/store-import-prototype-compatibility-v0.14.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 export review / persistence review / checkpoint review / scheduler review / audit / trace 反向成为 store import descriptor 的第一输入，或让 descriptor / review 提前承诺 durable store / recovery ABI，通常就应视为 versioning 事件
3. `docs/plan/roadmap-v0.14.zh.md` 当前已同步把 V0.14 的 next step 切到 `Issue 04-06`，确保后续先进入 store import descriptor 的 direct model、validation 与 bootstrap，而不是继续在 layering 未冻结前扩张私有 store 语义

## [x] Issue 04

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

1. 已新增 `include/ahfl/store_import/descriptor.hpp`，定义 `StoreImportDescriptor`、`StagingArtifactSet`、`StagingArtifactEntry`、`StagingBlocker`、顶层 `StoreImportDescriptorStatus` / `StoreImportBoundaryKind` / `StagingArtifactKind` / `StagingBlockerKind`，以及单一 `format_version` 入口 `ahfl.store-import-descriptor.v1`
2. 已新增 `src/store_import/descriptor.cpp` 与 `src/store_import/CMakeLists.txt`，提供 store-import-facing direct model validation 入口，并把 `ahfl_store_import` 接入主编译链路，作为后续 backend / CLI / review 的正式落点
3. 已新增 `tests/store_import/descriptor.cpp`、`tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 覆盖基础 direct model regression，并接入 `ahfl-v0.14` / `v0.14-store-import-descriptor-model` 标签

## [x] Issue 05

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

1. `src/store_import/descriptor.cpp` 已补齐 store import descriptor validation 的核心一致性约束：包括 source artifact format version gate、store import candidate identity / export package identity / planned durable identity 非空、staging artifact set entry count 与 logical name hygiene、import-ready 与 staging blocker 互斥、descriptor status 与 manifest status / workflow status / checkpoint status / persistence status 的对齐关系
2. 同一 validator 已把 terminal failed 必须带 workflow failure summary、terminal blocked 状态禁止 next required staging artifact、adapter-consumable boundary 只能出现在 ready 或 completed descriptor status 中显式化，避免这些规则被后续 CLI / review 层隐式吞掉
3. `tests/store_import/descriptor.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 direct regression 扩展到 ready、blocked、terminal failed、terminal partial 四类正例，以及 missing candidate identity、duplicate artifact name、import-ready + blocker、ready from blocked manifest、unsupported source manifest format、adapter-consumable blocked、terminal failed without failure summary 七类负例；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.14-store-import-descriptor-model'` 稳定回归

## [x] Issue 06

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

1. 已在 `include/ahfl/store_import/descriptor.hpp` 与 `src/store_import/descriptor.cpp` 新增 `StoreImportDescriptorResult` / `build_store_import_descriptor(...)`，把 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor` 与 `PersistenceExportManifest` 作为正式 deterministic bootstrap 输入，并继续复用 direct validator 区分 input artifact error、derived manifest mismatch 与 descriptor-facing inconsistency
2. 同一 bootstrap 会先重建并校验 derived `PersistenceExportManifest`，再显式比对输入 manifest 与 derived manifest 的 source version chain、workflow / session / run / input / failure summary、export package identity、planned durable identity、artifact bundle、manifest readiness / blocker，最后把 export manifest 投影为 store-import-facing `store_import_candidate_identity`、`staging_artifact_set`、`descriptor_status` 与 `staging_blocker`
3. `tests/store_import/descriptor.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 `v0.14-store-import-descriptor-bootstrap` 回归切片，覆盖 completed / failed / partial 三类 bootstrap 正例，以及 invalid manifest / manifest workflow mismatch 两类负例；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.14-store-import-descriptor-(model|bootstrap)'` 稳定回归

## [x] Issue 07

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

1. `src/cli/ahflc.cpp` 已新增 `emit-store-import-descriptor` 命令，并沿用 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> StoreImportDescriptor` 的 deterministic pipeline，覆盖 single-file、project、workspace 与 `--package` 入口
2. 已新增 `include/ahfl/backends/store_import_descriptor.hpp` 与 `src/backends/store_import_descriptor.cpp`，把 `StoreImportDescriptor` 固定输出为 machine-facing JSON，包括 source version chain、candidate identity、staging artifact set、boundary、import readiness、descriptor status 与 staging blocker
3. 已新增 `tests/store_import/*.store-import-descriptor.json` golden 基线，以及 `tests/cmake/SingleFileCliTests.cmake` / `tests/cmake/ProjectTests.cmake` / `tests/cmake/LabelTests.cmake` 中的 `v0.14-store-import-emission` / `v0.14-store-import-golden` 覆盖，固定 completed / failed / partial 三条 CLI 输出路径

## [x] Issue 08

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

1. 已新增 `include/ahfl/store_import/review.hpp` 与 `src/store_import/review.cpp`，定义 `StoreImportReviewSummary`、`StoreImportReviewNextActionKind`、validation 与从 `StoreImportDescriptor` 投影 review summary 的 deterministic builder，确保 review surface 只消费正式 descriptor model，而不私造 durable store / recovery 状态
2. review summary 当前稳定覆盖 source descriptor / manifest version、descriptor status、store import candidate identity、staging artifact preview、staging blocker、boundary summary、staging preview 与 next-step recommendation，并通过 `validate-store-import-review-ok` / `build-store-import-review-*` direct regression 固定 completed / failed / partial 场景
3. 已新增 `include/ahfl/backends/store_import_review.hpp` 与 `src/backends/store_import_review.cpp`，并在 `src/cli/ahflc.cpp` 接入 `emit-store-import-review` 命令，提供 contributor-facing staging preview 输出

## [x] Issue 09

标题：
增加 single-file / project / workspace store import golden 回归

背景：
若 store import prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 为 store import descriptor / review 增加 single-file、project、workspace golden 基线
2. 固定 completed、failed、partial 三类 store-import-facing CLI 输出
3. 把 V0.14 store import regression 接入统一标签，避免后续修改绕过回归

验收标准：

1. `emit-store-import-descriptor` 与 `emit-store-import-review` 在 single-file / project / workspace 三条路径均有 golden 回归
2. golden 覆盖 candidate identity、staging artifact set / preview、boundary、blocker 与 next-step recommendation
3. store import CLI 输出已纳入 `ahfl-v0.14` 与专用 `v0.14-store-import-*` 标签，可用单一 label 回归

主要涉及文件：

- `tests/store_import/`
- `tests/cmake/`

当前实现备注：

1. 已新增 `tests/store_import/ok_workflow_value_flow.with_package.store-import-descriptor.json`、`tests/store_import/ok_workflow_value_flow.with_package.store-import-review`、`tests/store_import/project_workflow_value_flow.failed.with_package.store-import-descriptor.json`、`tests/store_import/project_workflow_value_flow.failed.with_package.store-import-review`、`tests/store_import/project_workflow_value_flow.partial.with_package.store-import-descriptor.json` 与 `tests/store_import/project_workflow_value_flow.partial.with_package.store-import-review` 六份 golden 基线
2. `tests/cmake/SingleFileCliTests.cmake` 与 `tests/cmake/ProjectTests.cmake` 已接入 `ahflc.emit_store_import_descriptor.*` / `ahflc.emit_store_import_review.*` 六条 CLI 输出测试，覆盖 single-file completed、project failed、workspace partial 三种代表性路径
3. `tests/cmake/LabelTests.cmake` 已新增 `v0.14-store-import-review-model`、`v0.14-store-import-emission` 与 `v0.14-store-import-golden` 标签，可直接通过 `ctest --test-dir build/dev --output-on-failure -L 'v0.14-store-import-.*'` 回归

## [x] Issue 10

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

1. `docs/reference/store-import-prototype-compatibility-v0.14.zh.md` 已对齐当前实现，按 `StoreImportDescriptor` / `StoreImportReviewSummary` 两个 artifact 分节冻结 `ahfl.store-import-descriptor.v1` 与 `ahfl.store-import-review.v1` 的版本入口、source version 校验顺序、最小稳定字段边界、breaking-change 候选与 non-breaking extension 候选
2. 同一 compatibility 文档已把 descriptor / review 的当前实现入口、CLI 命令、golden / emission 标签与文档同步要求显式化，避免继续保留“规划中”表述导致的边界漂移
3. `docs/plan/roadmap-v0.14.zh.md` 与本 backlog 已同步把 V0.14 next step 从 M3/M4 收口为完成状态，确保 compatibility contract 不再停留在未完成描述
## [x] Issue 11

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

1. 已新增 `docs/reference/native-consumer-matrix-v0.14.zh.md`，正式冻结 V0.14 store-import-facing consumer matrix，覆盖 `PersistenceExportManifest`、`StoreImportDescriptor`、`StoreImportReviewSummary` 与 future durable store adapter 的推荐消费顺序、共享入口、扩展模板与反模式
2. 已新增 `docs/reference/contributor-guide-v0.14.zh.md`，给出 store import descriptor / review 的本地跑通路径、最小验证清单、扩展顺序、边界 guidance 与文档/测试联动约束
3. `README.md` 与 `docs/README.md` 已新增 V0.14 store import consumer matrix / contributor guide 入口，确保新贡献者可从 repo 入口直接定位到当前 store-import-facing reference 文档
## [x] Issue 12

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

1. `tests/store_import/`、`tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已形成 V0.14 store import prototype 的 direct / golden / label 分层闭环，并可通过 `ctest --test-dir build/dev --output-on-failure -L 'v0.14-store-import-.*'` 独立回归
2. `.github/workflows/ci.yml` 已新增 build-dev 与 ASan 两条流水线中的 `ahfl-v0.14` 显式回归步骤，保证 store-import-facing 主链路在全量 `ctest` 之前先被单独执行
3. `docs/plan/roadmap-v0.14.zh.md`、`docs/plan/issue-backlog-v0.14.zh.md`、`docs/reference/store-import-prototype-compatibility-v0.14.zh.md`、`docs/reference/native-consumer-matrix-v0.14.zh.md`、`docs/reference/contributor-guide-v0.14.zh.md`、`docs/README.md` 与 `README.md` 已形成 V0.14 reference / README / roadmap / backlog 的最小闭环
