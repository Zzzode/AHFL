# AHFL Contributor Guide V0.15

本文给出面向新贡献者的 V0.15 上手路径，重点覆盖 `Request`、`ReviewSummary`、compatibility contract、consumer matrix、golden regression、CI 标签切片，以及它们与 V0.14 store-import-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.14.zh.md](./contributor-guide-v0.14.zh.md)
- [native-consumer-matrix-v0.15.zh.md](./native-consumer-matrix-v0.15.zh.md)
- [durable-store-import-prototype-compatibility-v0.15.zh.md](./durable-store-import-prototype-compatibility-v0.15.zh.md)
- [native-durable-store-import-prototype-bootstrap-v0.15.zh.md](../design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)

## 先跑通的八条路径

建议先在本地确认八条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_tests
./build/dev/src/cli/ahflc emit-durable-store-import-request \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-request \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-durable-store-import-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L 'v0.15-durable-store-import-(request-model|request-bootstrap|review-model)'
ctest --preset test-dev --output-on-failure -L 'v0.15-durable-store-import-(emission|golden)'
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.15'
```

## 按改动类型找入口

### 1. 改 durable store import request 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/durable_store_import/request.hpp`
- `src/durable_store_import/request.cpp`
- `src/backends/durable_store_import_request.cpp`
- `tests/durable_store_import/request.cpp`
- `tests/durable_store_import/*.durable-store-import-request.json`

### 2. 改 durable store import review summary / review validation

通常要改的文件：

- `include/ahfl/durable_store_import/review.hpp`
- `src/durable_store_import/review.cpp`
- `src/backends/durable_store_import_review.cpp`
- `tests/durable_store_import/request.cpp`
- `tests/durable_store_import/*.durable-store-import-review`

### 3. 改 durable store import CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/durable_store_import_request.hpp`
- `include/ahfl/backends/durable_store_import_review.hpp`
- `src/backends/durable_store_import_request.cpp`
- `src/backends/durable_store_import_review.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 4. 改 durable store import compatibility / consumer guidance

通常要改的文件：

- `docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md`
- `docs/reference/native-consumer-matrix-v0.15.zh.md`
- `docs/reference/contributor-guide-v0.15.zh.md`
- `docs/plan/roadmap-v0.15.zh.md`
- `docs/plan/issue-backlog-v0.15.zh.md`
- `README.md`
- `docs/README.md`

## 推荐扩展顺序

V0.15 当前推荐的 durable-store-import-facing 扩展顺序是：

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
10. 若扩 machine-facing durable request identity / requested artifact set / adapter blocker
   - 再改 `Request`
11. 若扩 reviewer-facing request preview / requested artifact preview / next-step recommendation
   - 最后改 `ReviewSummary`

这表示：

1. `StoreImportDescriptor` 是 durable-store-import-facing machine state 的直接上游事实来源
2. `Request` 是 durable adapter-facing state 的第一事实来源
3. `ReviewSummary` 是 request 的 readable projection，不是独立 durable store 状态机
4. durable store / recovery protocol 若以后需要，应先扩 durable store import compatibility，而不是先改 review / CLI 文本

## Future Real Durable Store Boundary Guidance

V0.15 当前 future real durable store adapter / recovery explorer guidance 是：

1. 允许 future explorer 依赖 `CheckpointRecord` 的 persistable prefix、resume blocker 与 checkpoint boundary kind
2. 允许 future explorer 依赖 `CheckpointPersistenceDescriptor` 的 planned durable identity、exportable prefix、persistence blocker 与 basis kind
3. 允许 future explorer 依赖 `PersistenceExportManifest` 的 export package identity、artifact bundle、manifest readiness、store-import blocker 与 boundary kind
4. 允许 future explorer 依赖 `StoreImportDescriptor` 的 store import candidate identity、staging artifact set、descriptor boundary、import readiness、descriptor status 与 staging blocker
5. 允许 future explorer 依赖 `Request` 的 durable request identity、requested artifact set、request boundary、adapter readiness、request status、adapter blocker 与 source version chain
6. 允许 reviewer tooling 依赖 `ReviewSummary` 的 request preview、requested artifact preview、adapter boundary summary 与 next-step recommendation
7. 不允许在当前版本中引入 durable checkpoint id、resume token、store mutation ABI、store URI、object path、database key、credential、import receipt 或 operator payload
8. 不允许把 host telemetry、store metadata、provider payload 塞入 request / review
9. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `Request` 直接从 AST、trace、脚本推导 durable store state

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
10. `Request`
11. `ReviewSummary`

## 最小验证清单

只要触及 V0.15 durable-store-import-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-request-model
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-request-bootstrap
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-review-model
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-golden
```

若改动触及 durable store import CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.15-durable-store-import-emission
```

若想一次跑完整 V0.15 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.15
```

## 当前反模式

V0.15 当前明确不建议：

1. 跳过 `Request`，直接在 `emit-durable-store-import-review` / 外部脚本里私造 request preview state machine
2. 把 `StoreImportReviewSummary` 当 durable store import request 的正式第一输入
3. 把 `DryRunTrace` 当 durable store import prototype / recovery explorer 的正式第一输入
4. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `Request`，直接从 AST、source、trace 重建 durable request identity / requested artifact set / blocker
5. 在 request / review 中塞入 durable checkpoint id、resume token、store URI、object path、host telemetry、provider payload、credential 或 import receipt
6. 在未更新 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 durable-store-import-facing 稳定边界

## 文档与测试联动约束

V0.15 当前要求文档、测试和实现保持同步：

1. 改 `Request` 稳定字段时，要同步更新 durable store import compatibility 文档与 `tests/durable_store_import/*.durable-store-import-request.json`
2. 改 `ReviewSummary` 稳定字段时，要同步更新 durable store import compatibility 文档与 `tests/durable_store_import/*.durable-store-import-review`
3. 改 durable-store-import-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 durable store import 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. durable store import request 的 model / validation / bootstrap / emission 已全部落地
2. durable store import review 的 model / validation / emission 已全部落地
3. durable store import compatibility、golden、CI 标签切片与 contributor guidance 已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-store-import-request -> durable-store-import-review`
