# AHFL V0.13 Issue Backlog

本文给出与 [roadmap-v0.13.zh.md](./roadmap-v0.13.zh.md) 对齐的 issue-ready backlog。目标是把 V0.12 已完成的 persistence descriptor / review artifact，继续推进为 future durable store prototype 可稳定消费的 local export package manifest 边界，而不是直接进入 production durable persistence / recovery 系统。

## [x] Issue 01

标题：
冻结 V0.13 export package manifest scope 与 non-goals

背景：
V0.12 已经把 persistence descriptor / review 做成正式 artifact；如果下一阶段不先冻结 scope，很容易把真实 durable checkpoint store、resume token、object storage layout 或 production recovery 目标重新混入本地 prototype。

目标：

1. 明确 V0.13 的成功标准是 export-package-facing local prototype artifact
2. 明确 V0.13 不交付 durable store、crash recovery、distributed recovery daemon、object storage layout 或 host log pipeline
3. 明确 export package manifest 仍然只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor` 之上

验收标准：

1. roadmap 与 backlog 使用同一组 export manifest / artifact bundle / store-import blocker 术语
2. 文档明确 store prototype consumer 不得跳过现有 runtime、checkpoint 与 persistence artifact
3. backlog 不把 deployment、telemetry、durable persistence、provider connector 或 distributed runtime 混入当前阶段

主要涉及文件：

- `docs/plan/`
- `docs/design/`

当前实现备注：

1. 已新增 `docs/design/native-export-package-prototype-bootstrap-v0.13.zh.md`，明确 V0.13 当前只交付 export-package-facing local prototype artifact，不交付 durable store、resume token、crash recovery、distributed recovery daemon、object storage layout 或 host telemetry
2. 同一设计文档已明确 export package prototype 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest` 这条正式链路之上，并显式禁止回退依赖 persistence review、checkpoint review、scheduler review、trace、AST、source 或 host log
3. `docs/plan/roadmap-v0.13.zh.md` 当前已同步把 V0.13 的 next step 切到 Issue 02-03，确保后续先冻结 export manifest 最小模型与 layering，再进入代码实现

## [x] Issue 02

标题：
冻结 export manifest / artifact bundle layout 的最小模型与 store-import 边界

背景：
当前仓库虽已有 `CheckpointPersistenceDescriptor` / `PersistenceReviewSummary`，但还没有正式的 export-package-facing 状态对象；future durable store prototype 很容易私造 bundle manifest、artifact path、store import readiness 与 blocker 语义。

目标：

1. 定义 export manifest 顶层状态集合
2. 定义 export package identity、artifact bundle、store-import blocker 与 local/store boundary 的最小字段
3. 冻结 export-package-facing `format_version` 的兼容入口与 future durable store / recovery 边界

验收标准：

1. 文档明确哪些字段属于 export-package-facing 稳定输入
2. manifest 与 persistence descriptor / review / future store adapter 的职责关系清晰
3. 后续实现有单一版本入口与明确 breaking-change 触发条件

主要涉及文件：

- `docs/design/`
- `docs/reference/`
- `include/ahfl/`

当前实现备注：

1. 已新增 `docs/reference/export-package-prototype-compatibility-v0.13.zh.md`，给出 `PersistenceExportManifest` 的 `ahfl.persistence-export-manifest.v1` 与 future `PersistenceExportReviewSummary` 的 `ahfl.persistence-export-review.v1` 计划版本入口基线，同时明确 export-package-facing artifact 只能建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest` 链路之上
2. 同一 reference 文档已列出当前最小稳定输入边界：export package identity、artifact bundle entries、manifest boundary、manifest readiness、store-import blocker 与 source artifact version chain，并明确 durable checkpoint id、resume token、store URI、object path、store metadata 仍不属于当前版本稳定承诺
3. `docs/plan/roadmap-v0.13.zh.md` 当前已同步把 V0.13 的 next step 切到 `Issue 03`，确保后续先冻结 persistence descriptor / export manifest / future store adapter 的 layering，再进入具体模型实现

## [x] Issue 03

标题：
冻结 persistence descriptor、export manifest 与 future durable store adapter 的分层关系

背景：
若 V0.13 没先冻结 layering，后续很容易在 export review 输出或 CLI 层直接偷塞 store URI、object path、resume token、recovery handle、replication metadata 或 host-side payload。

目标：

1. 明确 export manifest 如何消费 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor
2. 明确 export review summary 与 persistence review / checkpoint review / scheduler review / audit / trace 的边界
3. 明确 export manifest 与 future durable store adapter / recovery protocol 的兼容关系

验收标准：

1. persistence descriptor / export manifest / review / future store adapter 的 layering 清晰
2. 文档明确哪些变化兼容，哪些需要 bump version
3. 文档显式禁止回退依赖 persistence review、checkpoint review、scheduler review、trace、AST、host log 或 provider payload

主要涉及文件：

- `docs/design/`
- `docs/reference/`

当前实现备注：

1. `docs/design/native-export-package-prototype-bootstrap-v0.13.zh.md` 已补充 layering-sensitive compatibility 规则，明确 `CheckpointPersistenceDescriptor` 仍是 export manifest 的直接 machine-facing 上游，而 `PersistenceExportReviewSummary` 仍只是 projection，不得承接 durable store mutation、resume token 派发或 recovery command synthesis
2. `docs/reference/export-package-prototype-compatibility-v0.13.zh.md` 已补充 layering-sensitive breaking-change 规则，明确只要让 persistence review / checkpoint review / scheduler review / audit / trace 反向成为 export manifest 的第一输入，或让 export manifest / review 提前承诺 durable store / recovery ABI，通常就应视为 versioning 事件
3. `docs/plan/roadmap-v0.13.zh.md` 当前已同步把 V0.13 的 next step 切到 `Issue 04-06`，确保后续先进入 export manifest 的 direct model、validation 与 bootstrap，而不是继续在 layering 未冻结前扩张私有 store 语义


## [x] Issue 04

标题：
引入 persistence export manifest 数据模型

背景：
要让 durable store prototype 成为正式 consumer，首先需要稳定的 direct model，而不是让测试、CLI 与外部脚本分别重建 export package identity / artifact bundle / store-import blocker 状态。

目标：

1. 引入 export-package-facing 顶层 manifest / artifact bundle / cursor 对象
2. 覆盖 export package identity、source version chain、artifact entries、manifest-ready 与 store-import blocker 的最小字段
3. 提供 direct model validation 的数据入口

验收标准：

1. 有 direct export manifest model regression
2. export manifest 不回退依赖 review / trace / audit 作为第一事实来源
3. diagnostics 能稳定区分 model validation 错误与 bootstrap 输入错误

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/persistence_export/manifest.hpp`，定义 `PersistenceExportManifest`、`ExportArtifactBundle`、`ExportArtifactEntry`、`StoreImportBlocker`、顶层 `PersistenceExportManifestStatus` / `ManifestBoundaryKind` / `ExportArtifactKind` / `StoreImportBlockerKind`，以及单一 `format_version` 入口 `ahfl.persistence-export-manifest.v1`
2. 已新增 `src/persistence_export/manifest.cpp` 与 `src/persistence_export/CMakeLists.txt`，提供 export-package-facing direct model validation 入口，并把 `ahfl_persistence_export` 接入主编译链路，作为后续 bootstrap / backend / CLI 的正式落点
3. 已新增 `tests/persistence_export/manifest.cpp`、`tests/cmake/TestTargets.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 覆盖基础 direct model regression，并接入 `ahfl-v0.13` / `v0.13-persistence-export-manifest-model` 标签

## [x] Issue 05

标题：
增加 export manifest validation 与 direct regression

背景：
只有模型还不够；仓库需要最小 validation 保证 export package identity、artifact bundle 与上游 persistence descriptor 保持一致。

目标：

1. 为 export manifest 增加 direct validation
2. 覆盖 ready / blocked / terminal / partial / failed 场景
3. 固定 export-package-facing diagnostics 文本与断言点

验收标准：

1. 有 direct validation 正例 / 负例回归
2. export manifest 与 persistence descriptor / checkpoint / snapshot / session / journal / replay 一致
3. diagnostics 不会被 CLI / review 输出层吞掉

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. `src/persistence_export/manifest.cpp` 已补齐 export manifest validation 的核心一致性约束：包括 source artifact format version gate、export package identity / planned durable identity 非空、artifact bundle entry count 与 logical name hygiene、manifest-ready 与 blocker 互斥、ReadyToImport 必须来自 ReadyToExport persistence status，以及 terminal completed / failed / partial 与上游 workflow / checkpoint / persistence status 的对齐关系
2. 同一 validator 已把 terminal failed 必须带 workflow failure summary、terminal blocked 状态禁止 manifest-ready / next required artifact、store-import-adjacent boundary 只能出现在 ready 或 completed manifest status 中显式化，避免这些规则被后续 CLI / review 层隐式吞掉
3. `tests/persistence_export/manifest.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 direct regression 扩展到 ready、blocked、terminal failed、terminal partial 四类正例，以及 missing export package identity、duplicate artifact name、manifest-ready + blocker、ready from blocked persistence、unsupported source descriptor format、store-import-adjacent blocked、terminal failed without failure summary 七类负例；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.13-persistence-export-manifest-model'` 稳定回归

## [x] Issue 06

标题：
增加 deterministic export manifest bootstrap

背景：
只有 model / validation 还不够；仓库需要最小 bootstrap 把 `CheckpointPersistenceDescriptor` 及其上游 artifact 投影为 export-package-facing manifest。

目标：

1. 基于 V0.12 已冻结 artifact 构建 deterministic export manifest
2. 覆盖 success、partial 与 failure-path 的 export-package-facing 分支
3. 区分 input artifact error、bootstrap construction error 与 manifest-facing inconsistency

验收标准：

1. 有 direct bootstrap 正例 / 负例回归
2. export manifest 与 plan / session / journal / replay / snapshot / checkpoint / persistence descriptor 保持一致
3. failure 归因不会被 persistence review / checkpoint review / CLI 层混淆

主要涉及文件：

- `src/`
- `tests/`

当前实现备注：

1. 已在 `include/ahfl/persistence_export/manifest.hpp` 与 `src/persistence_export/manifest.cpp` 新增 `PersistenceExportManifestResult` / `build_persistence_export_manifest(...)`，把 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord` 与 `CheckpointPersistenceDescriptor` 作为正式 deterministic bootstrap 输入，并继续复用 direct validator 区分 input artifact error 与 bootstrap mismatch
2. 同一 bootstrap 已显式校验 source format version chain、package identity、workflow / session / run / input / failure summary 对齐关系，以及 replay / snapshot / checkpoint / descriptor execution-order 与 prefix 一致性，然后再把 `CheckpointPersistenceDescriptor` 投影为 export-package-facing `export_package_identity`、`artifact_bundle`、`manifest_status` 与 `store_import_blocker`
3. `tests/persistence_export/manifest.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 `v0.13-persistence-export-manifest-bootstrap` 回归切片，覆盖 completed / failed / partial 三类 bootstrap 正例，以及 invalid descriptor / descriptor workflow mismatch 两类负例；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.13-persistence-export-manifest-(model|bootstrap)'` 稳定回归

## [x] Issue 07

标题：
增加 export manifest 输出路径

背景：
如果 export-package-facing 状态只停留在内部对象，future durable store prototype、CI 与 reviewer 都无法稳定消费 artifact bundle 语义。

目标：

1. 扩展 CLI / backend 输出 export manifest
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 success / partial / failure-path 的 export manifest golden

验收标准：

1. CLI 可输出稳定 export manifest 结果
2. 输出覆盖 export package identity、artifact bundle、store-import blocker 与 boundary summary
3. 有 single-file / project / workspace golden 回归

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/backends/`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/backends/persistence_export_manifest.hpp` 与 `src/backends/persistence_export_manifest.cpp`，提供 `print_persistence_export_manifest_json(...)`，把 source format chain、workflow / checkpoint / persistence / manifest status、export package identity、planned durable identity、artifact bundle、next required artifact 与 store-import blocker 以稳定 JSON 形式输出
2. `src/cli/ahflc.cpp` 已新增 `emit-export-manifest` 子命令，沿用 `emit-persistence-descriptor` 的 deterministic 链路构建 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest`，并把 `--package` / `--capability-mocks` / `--input-fixture` / `--run-id` / `--workflow` 的参数校验同步扩展到新命令
3. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake`、`tests/cmake/LabelTests.cmake` 与 `tests/export/` 已新增 single-file success、project failed、workspace partial 三类 emission regression，并接入 `ahfl-v0.13` / `v0.13-persistence-export-manifest-emission` 标签，确保输出路径不回退成隐式约定

## [x] Issue 08

标题：
增加 export package review / import preview surface

背景：
export package prototype 若只有 machine-facing manifest，而没有 reviewer-facing summary，新贡献者仍会回退依赖 grep / trace 来判断“现在能不能形成 stable store import handoff、后续 durable store adapter 应看什么”。

目标：

1. 增加 export-package-facing reviewer summary
2. 覆盖 export package identity、artifact bundle、store-import blocker、store boundary 与 next-step recommendation
3. 保持 summary 只消费正式 export manifest model，而不私造 durable store / recovery 状态机

验收标准：

1. 有 direct / CLI review regression
2. summary 与 source export manifest 一致
3. review surface 不倒灌 host telemetry、store metadata、replication metadata、object path、credential 或 private payload

主要涉及文件：

- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/`

当前实现备注：

1. 已新增 `include/ahfl/persistence_export/review.hpp` 与 `src/persistence_export/review.cpp`，定义 `PersistenceExportReviewSummary` / `PersistenceExportReviewNextActionKind` 与 `build_persistence_export_review_summary(...)`，并把 reviewer-facing summary 明确约束为只消费 `PersistenceExportManifest`，稳定输出 export package identity、artifact bundle preview、store boundary、import preview、next action 与 next-step recommendation
2. 已新增 `include/ahfl/backends/persistence_export_review.hpp` 与 `src/backends/persistence_export_review.cpp`，提供 `print_persistence_export_review(...)` 文本输出；`src/cli/ahflc.cpp` 同步接入 `emit-export-review` 子命令，并沿用 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> PersistenceExportReviewSummary` 的 deterministic 链路
3. `tests/persistence_export/manifest.cpp`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已新增 direct review regression 与 CLI review regression，覆盖 validate ok、unsupported source manifest format、invalid manifest、completed / failed / partial 三类 bootstrap 场景，以及 single-file / project / workspace 三类 emission 路径；当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.13-persistence-export-review-(model|emission)'` 稳定回归

## [x] Issue 09

标题：
增加 single-file / project / workspace export package golden 回归

背景：
若 export package prototype 没有单独 golden 与 CLI regression，很快会回退成隐式约定。

目标：

1. 固定 single-file export package golden
2. 固定 project / workspace export package golden
3. 保持 success / partial / failure-path 都被覆盖

验收标准：

1. golden 可独立运行
2. `--package` 路径与 project / workspace 路径都被覆盖
3. export-package-facing 变更能被稳定 diff

主要涉及文件：

- `tests/`
- `tests/cmake/`

当前实现备注：

1. `tests/export/` 已新增 `ok_workflow_value_flow.with_package.export-review`、`project_workflow_value_flow.failed.with_package.export-review` 与 `project_workflow_value_flow.partial.with_package.export-review` 三类 export review golden，并继续保留既有 manifest golden，形成 export package success / failure / partial 三态 diff 基线
2. `tests/cmake/SingleFileCliTests.cmake`、`tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake` 已把 export package golden 扩展为 6 个稳定输出用例：3 个 manifest golden + 3 个 export review golden，覆盖 single-file、project 与 workspace 的 `--package` 路径
3. 当前可通过 `ctest --preset test-dev --output-on-failure -L 'v0.13-export-golden'` 独立执行 export package golden 切片，也可通过 `ctest --preset test-dev --output-on-failure -L 'ahfl-v0.13'` 一次性回归 manifest / review model、emission 与 golden 闭环

## [x] Issue 10

标题：
冻结 export manifest / review compatibility 契约

背景：
当 manifest / review 的模型、bootstrap、CLI 与 golden 已落地后，若没有逐 artifact compatibility contract，后续很容易重新把 review 变成私有状态机，或把 durable store / recovery 字段静默塞回当前 export package prototype。

目标：

1. 冻结 `PersistenceExportManifest` 与 export review summary 的正式版本入口
2. 冻结 source artifact 校验顺序、稳定字段边界与 breaking-change 基线
3. 明确 docs / code / golden / tests 的同步流程

验收标准：

1. compatibility 文档按 artifact 分节说明 manifest / review 契约
2. consumer 能明确知道该检查哪些 source version，而不是靠字段猜语义
3. 文档明确指出哪些变化必须 bump version

主要涉及文件：

- `docs/reference/`
- `docs/plan/`

当前实现备注：

1. `docs/reference/export-package-prototype-compatibility-v0.13.zh.md` 已从 baseline 提升为正式 compatibility contract，按 `PersistenceExportManifest` / `PersistenceExportReviewSummary` 分节冻结 `ahfl.persistence-export-manifest.v1` 与 `ahfl.persistence-export-review.v1` 的正式版本入口、source version 检查顺序、最小稳定字段边界与 non-goal 范围
2. 同一 compatibility 文档已把 implementation entry、sync 要求、breaking-change / non-breaking extension 候选与 layering-sensitive 规则显式化，明确 manifest 仍是 machine-facing export package 的第一事实来源，而 export review 仍只是 reviewer-facing projection
3. 当前可通过阅读 compatibility contract 直接映射代码与回归入口：`include/ahfl/persistence_export/manifest.hpp`、`include/ahfl/persistence_export/review.hpp`、`ahflc emit-export-manifest`、`ahflc emit-export-review` 以及 `v0.13-export-golden`

## [x] Issue 11

标题：
更新 native consumer matrix、contributor guide 与 future store adapter boundary guidance

背景：
只有 compatibility contract 还不够；新贡献者与 future durable store prototype 仍需要知道 export-package-facing artifact 该落在哪一层、最小要跑哪些回归，以及当前阶段不能越界做什么。

目标：

1. 更新 V0.13 export-package-facing consumer matrix
2. 更新 contributor guide 与推荐扩展顺序
3. 明确 future durable store adapter / recovery boundary guidance

验收标准：

1. 有独立的 V0.13 consumer matrix / contributor guide
2. 文档明确推荐消费顺序与反模式
3. 最小验证清单能直接映射到当前标签与 golden

主要涉及文件：

- `docs/reference/`
- `README.md`
- `docs/README.md`

当前实现备注：

1. 已新增 `docs/reference/native-consumer-matrix-v0.13.zh.md`，把 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> PersistenceExportReviewSummary` 的推荐消费顺序、共享入口、扩展模板与 future durable store adapter 依赖边界正式冻结
2. 已新增 `docs/reference/contributor-guide-v0.13.zh.md`，给出 single-file / project / workspace 的 manifest / review 命令路径、改动入口、推荐扩展顺序、future store boundary guidance 与最小验证清单，确保新贡献者可直接按标签切片回归
3. `README.md`、`docs/README.md` 与 `docs/plan/roadmap-v0.13.zh.md` 已同步接入 V0.13 compatibility / consumer matrix / contributor guide 链接，并把 next step 切到 `Issue 12`，避免后续 contributor 继续误读 V0.12 persistence 文档作为当前 export package 主入口

## [x] Issue 12

标题：
建立 V0.13 export package prototype regression、CI 与 reference 文档闭环

背景：
export-package-facing artifact 一旦进入主线而没有单独标签切片、golden 与 CI 覆盖，很快会在 manifest、review 与 upstream artifact 的链路上产生隐式回退。

目标：

1. 增加 export package prototype 的 direct tests、golden 与 CLI regression
2. 在 CI 中显式执行 V0.13 标签切片
3. 补齐 roadmap、backlog、README、docs index 与 reference 链接

验收标准：

1. `tests/`、`docs/` 与 `.github/workflows/ci.yml` 形成 direct / golden / doc / CI 分层
2. CI 显式执行 V0.13 export package prototype 切片后，再继续全量回归
3. 新贡献者可按文档跑通 V0.13 主链路并定位失败归因

主要涉及文件：

- `tests/`
- `.github/workflows/ci.yml`
- `README.md`
- `docs/README.md`
- `docs/plan/`
- `docs/reference/`

当前实现备注：

1. `.github/workflows/ci.yml` 已在 dev 与 asan 两条主回归路径中显式新增 `Run V0.13 Export Package Prototype Regression` 步骤，直接执行 `ctest --preset test-dev --output-on-failure -L 'ahfl-v0.13'` 与 `ctest --preset test-asan --output-on-failure -L 'ahfl-v0.13'`，确保 export-package-facing direct / emission / golden 切片在全量回归前先被显式收口
2. `README.md`、`docs/README.md`、`docs/plan/roadmap-v0.13.zh.md` 与 `docs/reference/` 当前已形成 V0.13 export package prototype 的 scope / compatibility / consumer matrix / contributor guide / roadmap / backlog 文档入口，避免新贡献者继续误把 V0.12 persistence 文档当作当前主入口
3. 当前 `tests/`、`docs/` 与 `.github/workflows/ci.yml` 已形成 direct / golden / doc / CI 分层闭环；V0.13 export package prototype 的最小验证入口可稳定映射到 `ahfl-v0.13`、`v0.13-export-golden` 与相关 manifest / review 标签切片
