# AHFL Contributor Guide V0.19

本文给出面向新贡献者的 V0.19 上手路径，重点覆盖 `Response`、`ResponseReviewSummary`、compatibility contract、consumer matrix、golden regression、CI 标签切片，以及它们与 V0.18 durable-receipt-persistence-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.18.zh.md](./contributor-guide-v0.18.zh.md)
- [native-consumer-matrix-v0.19.zh.md](./native-consumer-matrix-v0.19.zh.md)
- [durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md](./durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md)
- [native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md](../design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md)

## 先跑通的十条路径

建议先在本地确认十条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L 'v0.19-durable-store-import-receipt-persistence-response-model'
ctest --preset test-dev --output-on-failure -L 'v0.19-durable-store-import-receipt-persistence-response-bootstrap'
ctest --preset test-dev --output-on-failure -L 'v0.19-durable-store-import-receipt-persistence-response-review-model'
ctest --preset test-dev --output-on-failure -L 'v0.19-durable-store-import-receipt-persistence-response-(emission|golden)'
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.19'
```

## 按改动类型找入口

### 1. 改 durable adapter receipt persistence response 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/durable_store_import/receipt_persistence_response.hpp`
- `src/durable_store_import/receipt_persistence_response.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-receipt-persistence-response.json`

### 2. 改 durable adapter receipt persistence response review summary / review validation

通常要改的文件：

- `include/ahfl/durable_store_import/receipt_persistence_response_review.hpp`
- `src/durable_store_import/receipt_persistence_response_review.cpp`
- `src/backends/durable_store_import_receipt_persistence_response_review.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-receipt-persistence-response-review`

### 3. 改 durable adapter receipt persistence response CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/durable_store_import_receipt_persistence_response.hpp`
- `include/ahfl/backends/durable_store_import_receipt_persistence_response_review.hpp`
- `src/backends/durable_store_import_receipt_persistence_response.cpp`
- `src/backends/durable_store_import_receipt_persistence_response_review.cpp`
- `src/backends/CMakeLists.txt`
- `src/cli/pipeline_runner.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 4. 改 durable adapter receipt persistence response compatibility / consumer guidance

通常要改的文件：

- `docs/reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md`
- `docs/reference/native-consumer-matrix-v0.19.zh.md`
- `docs/reference/contributor-guide-v0.19.zh.md`
- `docs/plan/roadmap-v0.19.zh.md`
- `docs/plan/issue-backlog-v0.19.zh.md`
- `README.md`
- `docs/README.md`

## 推荐扩展顺序

V0.19 当前推荐的 durable-adapter-receipt-persistence-response-facing 扩展顺序是：

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
    - 再改 `Request`
11. 若扩 machine-facing durable decision identity / status / outcome / capability gap / blocker
    - 再改 `Decision`
12. 若扩 machine-facing durable receipt identity / status / outcome / blocker / boundary
    - 再改 `Receipt`
13. 若扩 machine-facing durable receipt persistence request identity / status / outcome / blocker / boundary
    - 再改 `PersistenceRequest`
14. 若扩 machine-facing durable receipt persistence response identity / status / outcome / blocker / boundary
    - 再改 `Response`
15. 若扩 reviewer-facing response preview / contract summary / next-step recommendation
    - 最后改 `ResponseReviewSummary`

这表示：

1. `PersistenceRequest` 仍是 persistence response 的直接 machine-facing 上游
2. `Response` 是 future real adapter 稳定消费边界
3. `ResponseReviewSummary` 是 projection，不是独立 recovery 状态机

## Future Real Durable Store Boundary Guidance

V0.19 当前 future real durable store adapter / receipt persistence response / recovery explorer guidance 是：

1. 允许 future explorer 依赖 persistence response 的 `response identity`、`response status / outcome`、`response boundary`、`acknowledged_for_response`、`next_required_adapter_capability`、`response_blocker` 与 source version chain
2. 允许 reviewer tooling 依赖 response review 的 `response_preview`、`adapter_response_contract_summary`、`next_action` 与 `next_step_recommendation`
3. 不允许在当前版本中引入 import receipt persistence response id、resume token、retry token、store URI、object path、database key、credential 或 operator payload
4. 不允许把 host telemetry、provider payload、deployment metadata 塞入 persistence response / review
5. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `Request` / `Decision` / `Receipt` / `PersistenceRequest` / `Response`，直接从 AST、trace、脚本推导 durable receipt persistence response state

## 最小验证清单

只要触及 V0.19 durable-adapter-receipt-persistence-response-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.19-durable-store-import-receipt-persistence-response-model
ctest --preset test-dev --output-on-failure -L v0.19-durable-store-import-receipt-persistence-response-bootstrap
ctest --preset test-dev --output-on-failure -L v0.19-durable-store-import-receipt-persistence-response-review-model
ctest --preset test-dev --output-on-failure -L v0.19-durable-store-import-receipt-persistence-response-golden
```

若改动触及 durable adapter receipt persistence response CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.19-durable-store-import-receipt-persistence-response-emission
```

若想一次跑完整 V0.19 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.19
```

## 文档与测试联动约束

V0.19 当前要求文档、测试和实现保持同步：

1. 改 `Response` 稳定字段时，要同步更新 compatibility 文档与 `tests/durable_store_import/*.durable-store-import-receipt-persistence-response.json`
2. 改 `ResponseReviewSummary` 稳定字段时，要同步更新 compatibility 文档与 `tests/durable_store_import/*.durable-store-import-receipt-persistence-response-review`
3. 改 durable-adapter-receipt-persistence-response-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 V0.19 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. durable adapter receipt persistence response 的 model / validation / bootstrap / emission 已全部落地
2. durable adapter receipt persistence response review 的 model / validation / emission 已全部落地
3. durable adapter receipt persistence response compatibility、golden、CI 标签切片与 contributor guidance 已形成最小闭环
