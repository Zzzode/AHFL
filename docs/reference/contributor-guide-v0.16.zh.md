# AHFL Contributor Guide V0.16

本文给出面向新贡献者的 V0.16 上手路径，重点覆盖 `Decision`、`DecisionReviewSummary`、compatibility contract、consumer matrix、golden regression、CI 标签切片，以及它们与 V0.15 durable-request-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.15.zh.md](./contributor-guide-v0.15.zh.md)
- [native-consumer-matrix-v0.16.zh.md](./native-consumer-matrix-v0.16.zh.md)
- [durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md](./durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md)
- [native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md](../design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md)

## 先跑通的十条路径

建议先在本地确认十条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-decision \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-decision-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-decision \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-durable-store-import-decision-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L 'v0.16-durable-store-import-decision-model'
ctest --preset test-dev --output-on-failure -L 'v0.16-durable-store-import-decision-bootstrap'
ctest --preset test-dev --output-on-failure -L 'v0.16-durable-store-import-decision-review-model'
ctest --preset test-dev --output-on-failure -L 'v0.16-durable-store-import-decision-(emission|golden)'
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.16'
```

## 按改动类型找入口

### 1. 改 durable adapter decision 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/durable_store_import/decision.hpp`
- `src/durable_store_import/decision.cpp`
- `src/backends/durable_store_import_decision.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-decision.json`

### 2. 改 durable adapter decision review summary / review validation

通常要改的文件：

- `include/ahfl/durable_store_import/decision_review.hpp`
- `src/durable_store_import/decision_review.cpp`
- `src/backends/durable_store_import_decision_review.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-decision-review`

### 3. 改 durable adapter decision CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/durable_store_import_decision.hpp`
- `include/ahfl/backends/durable_store_import_decision_review.hpp`
- `src/backends/durable_store_import_decision.cpp`
- `src/backends/durable_store_import_decision_review.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 4. 改 durable adapter decision compatibility / consumer guidance

通常要改的文件：

- `docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md`
- `docs/reference/native-consumer-matrix-v0.16.zh.md`
- `docs/reference/contributor-guide-v0.16.zh.md`
- `docs/plan/roadmap-v0.16.zh.md`
- `docs/plan/issue-backlog-v0.16.zh.md`
- `README.md`
- `docs/README.md`

## 推荐扩展顺序

V0.16 当前推荐的 durable-adapter-decision-facing 扩展顺序是：

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
11. 若扩 machine-facing durable decision identity / status / outcome / capability gap / blocker
   - 再改 `Decision`
12. 若扩 reviewer-facing decision preview / adapter contract / next action / next-step recommendation
   - 最后改 `DecisionReviewSummary`

这表示：

1. `Request` 仍是 decision 的直接上游 machine artifact
2. `Decision` 是 future real durable adapter 语义的第一事实来源
3. `DecisionReviewSummary` 是 reviewer-facing projection，不是独立 durable adapter / recovery 状态机

## Future Real Durable Store Boundary Guidance

V0.16 当前 future real durable store adapter / receipt / recovery explorer guidance 是：

1. 允许 future explorer 依赖 `Decision` 的 decision identity、decision status / outcome、decision boundary、accepted-for-future-execution、next required adapter capability、decision blocker 与 source version chain
2. 允许 reviewer tooling 依赖 `DecisionReviewSummary` 的 decision preview、adapter contract summary、next action 与 next-step recommendation
3. 不允许在当前版本中引入 import receipt id、resume token、retry token、store URI、object path、database key、credential 或 operator payload
4. 不允许把 host telemetry、provider payload、deployment metadata 塞入 decision / review
5. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `Request` / `Decision` 直接从 AST、trace、脚本推导 durable decision state

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
11. `Decision`
12. `DecisionReviewSummary`

## 最小验证清单

只要触及 V0.16 durable-adapter-decision-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.16-durable-store-import-decision-model
ctest --preset test-dev --output-on-failure -L v0.16-durable-store-import-decision-bootstrap
ctest --preset test-dev --output-on-failure -L v0.16-durable-store-import-decision-review-model
ctest --preset test-dev --output-on-failure -L v0.16-durable-store-import-decision-golden
```

若改动触及 durable adapter decision CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.16-durable-store-import-decision-emission
```

若想一次跑完整 V0.16 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.16
```

## 当前反模式

V0.16 当前明确不建议：

1. 跳过 `Decision`，直接在 `emit-durable-store-import-decision-review` / 外部脚本里私造 decision preview state machine
2. 把 `DecisionReviewSummary` 当 durable adapter decision 的第一输入
3. 把 `DryRunTrace` 当 durable adapter decision prototype / recovery explorer 的正式第一输入
4. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` / `PersistenceExportManifest` / `StoreImportDescriptor` / `Request` / `Decision`，直接从 AST、source、trace 重建 decision identity / capability gap / blocker
5. 在 decision / review 中塞入 import receipt id、resume token、store URI、object path、credential、host telemetry 或 provider payload
6. 在未更新 compatibility / matrix / contributor docs / golden / labels / CI 的情况下静默扩张 durable-adapter-decision-facing 稳定边界

## 文档与测试联动约束

V0.16 当前要求文档、测试和实现保持同步：

1. 改 `Decision` 稳定字段时，要同步更新 durable adapter decision compatibility 文档与 `tests/durable_store_import/*.durable-store-import-decision.json`
2. 改 `DecisionReviewSummary` 稳定字段时，要同步更新 durable adapter decision compatibility 文档与 `tests/durable_store_import/*.durable-store-import-decision-review`
3. 改 durable-adapter-decision-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 V0.16 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake` 与 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. durable adapter decision 的 model / validation / bootstrap / emission 已全部落地
2. durable adapter decision review 的 model / validation / emission 已全部落地
3. durable adapter decision compatibility、golden、CI 标签切片与 contributor guidance 已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export-manifest -> store-import-descriptor -> durable-request -> durable-decision -> durable-decision-review`
