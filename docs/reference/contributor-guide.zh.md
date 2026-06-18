# AHFL Contributor Guide

本文是 `docs/reference` 中 contributor guide 的合并入口，覆盖历史各版本贡献指南。当前维护最新口径；旧版本的贡献路径、验证标签与扩展顺序已合并为摘要，不再保留独立入口。

关联文档：

- [native-runtime-artifacts.zh.md](./native-runtime-artifacts.zh.md)
- [native-runtime-architecture.zh.md](../design/native-runtime-architecture.zh.md)

## 合并范围

| 合并后保留的信息 |
|------------------|
| project model、diagnostics、IR、backend extension 的基础贡献入口。 |
| package authoring、native package、review / reference consumer helper。 |
| execution plan、dry-run runner、trace 输出。 |
| runtime session、execution journal。 |
| replay view、audit report。 |
| partial / failed session 与 failure-aware journal / replay / audit。 |
| scheduler snapshot。 |
| checkpoint record。 |
| persistence descriptor。 |
| export manifest / export review。 |
| store import descriptor / review。 |

## 当前口径摘要

1. 新贡献者应先跑通当前 store-import-facing 路径，再回溯上游 consumer。
2. 改稳定 artifact 字段时，必须同步模型、validator、emitter、golden、compatibility 文档、consumer matrix、贡献指南和 CI 标签。
3. 扩展顺序应沿 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> store-import-review` 前进。
4. Review / projection artifact 不能私造状态机；machine-facing artifact 是下游稳定依赖的第一事实来源。
5. 仓库当前不维护 immature 语义的向前兼容；breaking change 要通过文档、测试和 commit footer 显式标出。

## 先跑通的八条路径

建议先在本地确认八条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_store_import_tests
./build/dev/src/tooling/cli/ahflc emit-store-import-descriptor \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/tooling/cli/ahflc emit-store-import-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/tooling/cli/ahflc emit-store-import-descriptor \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/tooling/cli/ahflc emit-store-import-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L 'store-import-(descriptor-model|descriptor-bootstrap|review-model)'
ctest --preset test-dev --output-on-failure -L 'store-import-(emission|golden)'
ctest --preset test-dev --output-on-failure -L 'store-import-.*'
```

## 按改动类型找入口

### 1. 改 store import descriptor 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/store_import/descriptor.hpp`
- `src/pipeline/persistence/store_import/descriptor.cpp`
- `src/compiler/backends/store_import_descriptor.cpp`
- `tests/store_import/descriptor.cpp`
- `tests/store_import/*.store-import-descriptor.json`

### 2. 改 store import review summary / review validation

通常要改的文件：

- `include/ahfl/store_import/review.hpp`
- `src/pipeline/persistence/store_import/review.cpp`
- `src/compiler/backends/store_import_review.cpp`
- `tests/store_import/descriptor.cpp`
- `tests/store_import/*.store-import-review`

### 3. 改 store import CLI / backend 输出

通常要改的文件：

- `include/ahfl/compiler/backends/store_import_descriptor.hpp`
- `include/ahfl/compiler/backends/store_import_review.hpp`
- `src/compiler/backends/store_import_descriptor.cpp`
- `src/compiler/backends/store_import_review.cpp`
- `src/tooling/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 4. 改 store import compatibility / consumer guidance

通常要改的文件：

- `docs/reference/native-runtime-artifacts.zh.md`
- `docs/reference/contributor-guide.zh.md`
- `docs/plans/project-status.zh.md`
- `docs/plans/issue-backlog-global-gaps.zh.md`
- `README.md`
- `docs/README.md`

## 推荐扩展顺序

当前推荐的 store-import-facing 扩展顺序是：

1. 若扩 planning / dependency 语义
   - 先改 `ExecutionPlan`
2. 若扩 workflow / node 当前状态语义
   - 先改 `RuntimeSession`
3. 若扩 event / failure sequencing 语义
   - 再改 `ExecutionJournal`
4. 若扩 consistency / progression 语义
   - 再改 `ReplayView`
5. 若扩 ready-set / blocked-frontier / checkpoint-friendly local state
   - 再改 `SchedulerSnapshot`
6. 若扩 machine-facing persistable basis / blocker
   - 再改 `CheckpointRecord`
7. 若扩 machine-facing planned durable identity / export blocker
   - 再改 `CheckpointPersistenceDescriptor`
8. 若扩 machine-facing export package identity / artifact bundle / manifest blocker
   - 再改 `PersistenceExportManifest`
9. 若扩 machine-facing store import candidate identity / staging artifact set / descriptor blocker
   - 再改 `StoreImportDescriptor`
10. 若扩 reviewer-facing staging preview / boundary / next-step recommendation
   - 最后改 `StoreImportReviewSummary`

这表示：

1. `PersistenceExportManifest` 是 store-import-facing machine state 的直接上游事实来源
2. `StoreImportDescriptor` 是 store import machine-facing state 的第一事实来源
3. `StoreImportReviewSummary` 是 descriptor 的 readable projection，不是独立 durable store 状态机
4. durable store / recovery protocol 若以后需要，应先扩 store import compatibility，而不是先改 review / CLI 文本

## Future Durable Store Boundary Guidance

当前 future durable store adapter / recovery explorer guidance 是：

1. 允许 future explorer 依赖 `CheckpointRecord` 的 persistable prefix、resume blocker 与 checkpoint boundary kind
2. 允许 future explorer 依赖 `CheckpointPersistenceDescriptor` 的 planned durable identity、exportable prefix、persistence blocker 与 basis kind
3. 允许 future explorer 依赖 `PersistenceExportManifest` 的 export package identity、artifact bundle、manifest readiness、store-import blocker 与 boundary kind
4. 允许 future explorer 依赖 `StoreImportDescriptor` 的 store import candidate identity、staging artifact set、descriptor boundary、import readiness、descriptor status 与 staging blocker
5. 允许 reviewer tooling 依赖 `StoreImportReviewSummary` 的 staging preview / store boundary / next-step recommendation
6. 不允许在当前版本中引入 durable checkpoint id、resume token、store mutation ABI、store URI、object path、import receipt、operator payload
7. 不允许把 host telemetry、store metadata、provider payload 塞入 descriptor / review
8. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` 直接从 AST、trace、脚本推导 durable store state

推荐依赖顺序：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointPersistenceDescriptor`
8. `PersistenceExportManifest`
9. `StoreImportDescriptor`
10. `StoreImportReviewSummary`

## 最小验证清单

只要触及 store-import-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L store-import-descriptor-model
ctest --preset test-dev --output-on-failure -L store-import-descriptor-bootstrap
ctest --preset test-dev --output-on-failure -L store-import-review-model
ctest --preset test-dev --output-on-failure -L store-import-golden
```

若改动触及 store import CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L store-import-emission
```

若想一次跑完整当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl
```

## 当前反模式

当前明确不建议：

1. 跳过 `StoreImportDescriptor`，直接在 `emit-store-import-review` / 外部脚本里私造 staging preview state machine
2. 把 `DryRunTrace` 当 store import prototype / durable store explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor`，直接从 AST、source、trace 重建 store import candidate identity / staging artifact set / blocker
4. 在 descriptor / review 中塞入 durable checkpoint id、resume token、store URI、object path、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 store-import-facing 稳定边界

## 文档与测试联动约束

当前要求文档、测试和实现保持同步：

1. 改 `StoreImportDescriptor` 稳定字段时，要同步更新 store import compatibility 文档与 `tests/store_import/*.store-import-descriptor.json`
2. 改 `StoreImportReviewSummary` 稳定字段时，要同步更新 store import compatibility 文档与 `tests/store_import/*.store-import-review`
3. 改 store-import-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 store import 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. store import descriptor 的 model / validation / bootstrap / emission 已全部落地
2. store import review 的 model / validation / emission 已全部落地
3. store import compatibility、golden、CI 标签切片与 contributor guidance 已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> store-import-review`
