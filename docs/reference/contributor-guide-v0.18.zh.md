# AHFL Contributor Guide V0.18

本文给出面向新贡献者的 V0.18 上手路径，重点覆盖 `DurableStoreImportDecisionReceiptPersistenceRequest`、`DurableStoreImportDecisionReceiptPersistenceReviewSummary`、compatibility contract、consumer matrix、golden regression、CI 标签切片，以及它们与 V0.17 durable-receipt-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.17.zh.md](./contributor-guide-v0.17.zh.md)
- [native-consumer-matrix-v0.18.zh.md](./native-consumer-matrix-v0.18.zh.md)
- [durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md](./durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md)
- [native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md](../design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md)

## 先跑通的十条路径

建议先在本地确认十条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-request \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-request \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L 'v0.18-durable-store-import-receipt-persistence-model'
ctest --preset test-dev --output-on-failure -L 'v0.18-durable-store-import-receipt-persistence-review-model'
ctest --preset test-dev --output-on-failure -L 'v0.18-durable-store-import-receipt-persistence-(emission|golden)'
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.18'
```

## 按改动类型找入口

### 1. 改 durable adapter receipt persistence request 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/durable_store_import/receipt_persistence.hpp`
- `src/durable_store_import/receipt_persistence.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-receipt-persistence-request.json`

### 2. 改 durable adapter receipt persistence review summary / review validation

通常要改的文件：

- `include/ahfl/durable_store_import/receipt_persistence_review.hpp`
- `src/durable_store_import/receipt_persistence_review.cpp`
- `src/backends/durable_store_import_receipt_persistence_review.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-receipt-persistence-review`

### 3. 改 durable adapter receipt persistence CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/durable_store_import_receipt_persistence_request.hpp`
- `include/ahfl/backends/durable_store_import_receipt_persistence_review.hpp`
- `src/backends/durable_store_import_receipt_persistence_request.cpp`
- `src/backends/durable_store_import_receipt_persistence_review.cpp`
- `src/backends/CMakeLists.txt`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 4. 改 durable adapter receipt persistence compatibility / consumer guidance

通常要改的文件：

- `docs/reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md`
- `docs/reference/native-consumer-matrix-v0.18.zh.md`
- `docs/reference/contributor-guide-v0.18.zh.md`
- `docs/plan/roadmap-v0.18.zh.md`
- `docs/plan/issue-backlog-v0.18.zh.md`
- `README.md`
- `docs/README.md`

## 推荐扩展顺序

V0.18 当前推荐的 durable-adapter-receipt-persistence-facing 扩展顺序是：

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
10. 若扩 machine-facing durable request identity / adapter blocker
    - 再改 `DurableStoreImportRequest`
11. 若扩 machine-facing durable decision identity / status / outcome / capability gap / blocker
    - 再改 `DurableStoreImportDecision`
12. 若扩 machine-facing durable receipt identity / status / outcome / blocker / boundary
    - 再改 `DurableStoreImportDecisionReceipt`
13. 若扩 machine-facing durable receipt persistence request identity / status / outcome / blocker / boundary
    - 再改 `DurableStoreImportDecisionReceiptPersistenceRequest`
14. 若扩 reviewer-facing persistence preview / contract summary / next-step recommendation
    - 最后改 `DurableStoreImportDecisionReceiptPersistenceReviewSummary`

这表示：

1. `DurableStoreImportDecisionReceipt` 仍是 persistence request 的直接 machine-facing 上游
2. `DurableStoreImportDecisionReceiptPersistenceRequest` 是 future real adapter 稳定消费边界
3. `DurableStoreImportDecisionReceiptPersistenceReviewSummary` 是 projection，不是独立 recovery 状态机

## Future Real Durable Store Boundary Guidance

V0.18 当前 future real durable store adapter / receipt persistence / recovery explorer guidance 是：

1. 允许 future explorer 依赖 persistence request 的 `persistence request identity`、`persistence status / outcome`、`persistence boundary`、`accepted_for_receipt_persistence`、`next_required_adapter_capability`、`receipt_persistence_blocker` 与 source version chain
2. 允许 reviewer tooling 依赖 persistence review 的 `persistence_preview`、`adapter_receipt_persistence_contract_summary`、`next_action` 与 `next_step_recommendation`
3. 不允许在当前版本中引入 import receipt persistence id、resume token、retry token、store URI、object path、database key、credential 或 operator payload
4. 不允许把 host telemetry、provider payload、deployment metadata 塞入 persistence request / review
5. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `DurableStoreImportRequest` / `DurableStoreImportDecision` / `DurableStoreImportDecisionReceipt` / `DurableStoreImportDecisionReceiptPersistenceRequest`，直接从 AST、trace、脚本推导 durable receipt persistence state

## 最小验证清单

只要触及 V0.18 durable-adapter-receipt-persistence-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.18-durable-store-import-receipt-persistence-model
ctest --preset test-dev --output-on-failure -L v0.18-durable-store-import-receipt-persistence-review-model
ctest --preset test-dev --output-on-failure -L v0.18-durable-store-import-receipt-persistence-golden
```

若改动触及 durable adapter receipt persistence CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.18-durable-store-import-receipt-persistence-emission
```

若想一次跑完整 V0.18 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.18
```

## 文档与测试联动约束

V0.18 当前要求文档、测试和实现保持同步：

1. 改 `DurableStoreImportDecisionReceiptPersistenceRequest` 稳定字段时，要同步更新 compatibility 文档与 `tests/durable_store_import/*.durable-store-import-receipt-persistence-request.json`
2. 改 `DurableStoreImportDecisionReceiptPersistenceReviewSummary` 稳定字段时，要同步更新 compatibility 文档与 `tests/durable_store_import/*.durable-store-import-receipt-persistence-review`
3. 改 durable-adapter-receipt-persistence-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 V0.18 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. durable adapter receipt persistence request 的 model / validation / bootstrap / emission 已全部落地
2. durable adapter receipt persistence review 的 model / validation / emission 已全部落地
3. durable adapter receipt persistence compatibility、golden、CI 标签切片与 contributor guidance 已形成最小闭环
