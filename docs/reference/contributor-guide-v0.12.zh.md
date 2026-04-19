# AHFL Contributor Guide V0.12

本文给出面向新贡献者的 V0.12 上手路径，重点覆盖 persistence descriptor、persistence review、compatibility contract、golden regression，以及它们与 V0.11 checkpoint-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.11.zh.md](./contributor-guide-v0.11.zh.md)
- [native-consumer-matrix-v0.12.zh.md](./native-consumer-matrix-v0.12.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](./persistence-prototype-compatibility-v0.12.zh.md)
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)

## 先跑通的六条路径

建议先在本地确认六条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-persistence-descriptor \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-persistence-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-persistence-descriptor \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-persistence-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L v0.12-persistence-review-model
ctest --preset test-dev --output-on-failure -L v0.12-persistence-golden
```

## 按改动类型找入口

### 1. 改 persistence descriptor 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/persistence_descriptor/descriptor.hpp`
- `src/persistence_descriptor/descriptor.cpp`
- `src/backends/persistence_descriptor.cpp`
- `tests/persistence_descriptor/`
- `tests/persistence/`

### 2. 改 persistence review summary / review validation

通常要改的文件：

- `include/ahfl/persistence_descriptor/review.hpp`
- `src/persistence_descriptor/review.cpp`
- `src/backends/persistence_review.cpp`
- `tests/persistence_descriptor/`
- `tests/persistence/`

### 3. 改 persistence CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/persistence_descriptor.hpp`
- `include/ahfl/backends/persistence_review.hpp`
- `src/backends/persistence_descriptor.cpp`
- `src/backends/persistence_review.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

### 4. 改 persistence compatibility / consumer guidance

通常要改的文件：

- `docs/reference/persistence-prototype-compatibility-v0.12.zh.md`
- `docs/reference/native-consumer-matrix-v0.12.zh.md`
- `docs/reference/contributor-guide-v0.12.zh.md`
- `docs/plan/roadmap-v0.12.zh.md`
- `docs/plan/issue-backlog-v0.12.zh.md`

## 推荐扩展顺序

V0.12 当前推荐的 persistence-facing 扩展顺序是：

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
8. 若扩 reviewer-facing export preview / store boundary / next-step recommendation
   - 最后改 `PersistenceReviewSummary`

这表示：

1. `CheckpointPersistenceDescriptor` 是 persistence-facing machine state 的第一事实来源
2. `PersistenceReviewSummary` 是 descriptor 的 readable projection，不是独立 durable store 状态机
3. durable checkpoint / recovery protocol 若以后需要，应先扩 descriptor compatibility，而不是先改 review / CLI 文本

## Future Durable Store Boundary Guidance

V0.12 当前 future durable store / recovery guidance 是：

1. 允许 future explorer 依赖 `CheckpointRecord` 的 persistable prefix、resume blocker 与 checkpoint boundary kind
2. 允许 future explorer 依赖 `CheckpointPersistenceDescriptor` 的 planned durable identity、exportable prefix、persistence blocker 与 basis kind
3. 允许 reviewer tooling 依赖 `PersistenceReviewSummary` 的 export preview / store boundary / next-step recommendation
4. 不允许在当前版本中引入 durable checkpoint id、resume token、store mutation ABI、store URI、operator payload
5. 不允许把 host telemetry、store metadata、provider payload 塞入 descriptor / review
6. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` / `CheckpointPersistenceDescriptor` 直接从 AST、trace、脚本推导 durable store state

推荐依赖顺序：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointPersistenceDescriptor`
8. `PersistenceReviewSummary`

## 最小验证清单

只要触及 V0.12 persistence-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.12-persistence-descriptor-model
ctest --preset test-dev --output-on-failure -L v0.12-persistence-descriptor-bootstrap
ctest --preset test-dev --output-on-failure -L v0.12-persistence-review-model
ctest --preset test-dev --output-on-failure -L v0.12-persistence-golden
```

若改动触及 persistence CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.12-persistence-descriptor-emission
ctest --preset test-dev --output-on-failure -L v0.12-persistence-review-emission
```

若想一次跑完整 V0.12 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.12
```

## 当前反模式

V0.12 当前明确不建议：

1. 跳过 `CheckpointPersistenceDescriptor`，直接在 `emit-persistence-review` / 外部脚本里私造 export preview state machine
2. 把 `DryRunTrace` 当 persistence prototype / durable recovery explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord`，直接从 AST、source、trace 重建 planned durable identity / exportable prefix / blocker
4. 在 persistence descriptor / review 中塞入 durable checkpoint id、resume token、store URI、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张 persistence-facing 稳定边界

## 文档与测试联动约束

V0.12 当前要求文档、测试和实现保持同步：

1. 改 `CheckpointPersistenceDescriptor` 稳定字段时，要同步更新 persistence compatibility 文档与 `tests/persistence/`
2. 改 `PersistenceReviewSummary` 稳定字段时，要同步更新 persistence compatibility 文档与 `tests/persistence/`
3. 改 persistence-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 persistence 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake`

## 当前状态

截至当前实现：

1. persistence descriptor 的 model / validation / bootstrap / emission 已全部落地
2. persistence review 的 model / validation / emission 已全部落地
3. persistence compatibility、golden 与 regression 标签切片已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> persistence-review`
