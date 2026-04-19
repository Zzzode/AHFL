# AHFL Contributor Guide V0.11

本文给出面向新贡献者的 V0.11 上手路径，重点覆盖 checkpoint record、checkpoint review、compatibility contract、golden regression，以及它们与 V0.10 scheduler-facing artifact 的协作边界。

关联文档：

- [contributor-guide-v0.10.zh.md](./contributor-guide-v0.10.zh.md)
- [native-consumer-matrix-v0.11.zh.md](./native-consumer-matrix-v0.11.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](./checkpoint-prototype-compatibility-v0.11.zh.md)
- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](../design/native-checkpoint-prototype-bootstrap-v0.11.zh.md)

## 先跑通的六条路径

建议先在本地确认六条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-checkpoint-record \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-checkpoint-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-checkpoint-record \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-checkpoint-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-review-model
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-golden
```

## 按改动类型找入口

### 1. 改 checkpoint record 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/checkpoint_record/record.hpp`
- `src/checkpoint_record/record.cpp`
- `tests/checkpoint_record/`
- `tests/checkpoint/`

### 2. 改 checkpoint review summary / review validation

通常要改的文件：

- `include/ahfl/checkpoint_record/review.hpp`
- `src/checkpoint_record/review.cpp`
- `src/backends/checkpoint_review.cpp`
- `tests/checkpoint_record/`
- `tests/checkpoint/`

### 3. 改 checkpoint CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/checkpoint_record.hpp`
- `include/ahfl/backends/checkpoint_review.hpp`
- `src/backends/checkpoint_record.cpp`
- `src/backends/checkpoint_review.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

### 4. 改 checkpoint compatibility / consumer guidance

通常要改的文件：

- `docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md`
- `docs/reference/native-consumer-matrix-v0.11.zh.md`
- `docs/reference/contributor-guide-v0.11.zh.md`
- `docs/plan/roadmap-v0.11.zh.md`
- `docs/plan/issue-backlog-v0.11.zh.md`

## 推荐扩展顺序

V0.11 当前推荐的 checkpoint-facing 扩展顺序是：

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
7. 若扩 reviewer-facing resume preview / next-step recommendation
   - 最后改 `CheckpointReviewSummary`

这表示：

1. `CheckpointRecord` 是 checkpoint-facing machine state 的第一事实来源
2. `CheckpointReviewSummary` 是 record 的 readable projection，不是独立 recovery 状态机
3. durable checkpoint / resume protocol 若以后需要，应先扩 record compatibility，而不是先改 review / CLI 文本

## Future Recovery Boundary Guidance

V0.11 当前 future recovery / persistence guidance 是：

1. 允许 future explorer 依赖 `CheckpointRecord` 的 persistable prefix、resume blocker 与 boundary kind
2. 允许 reviewer tooling 依赖 `CheckpointReviewSummary` 的 resume preview / next-step recommendation
3. 不允许在当前版本中引入 durable checkpoint id、resume token、recovery launcher ABI
4. 不允许把 host telemetry、store metadata、provider payload 塞入 record / review
5. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord` 直接从 AST、trace、脚本推导 recovery state

推荐依赖顺序：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `CheckpointRecord`
7. `CheckpointReviewSummary`

## 最小验证清单

只要触及 V0.11 checkpoint-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-record-model
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-record-bootstrap
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-review-model
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-golden
```

若改动触及 checkpoint CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-record-emission
ctest --preset test-dev --output-on-failure -L v0.11-checkpoint-review-emission
```

若想一次跑完整 V0.11 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.11
```

## 当前反模式

V0.11 当前明确不建议：

1. 跳过 `CheckpointRecord`，直接在 `emit-checkpoint-review` / 外部脚本里私造 resume preview state machine
2. 把 `DryRunTrace` 当 checkpoint prototype / recovery explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot`，直接从 AST、source、trace 重建 prefix / blocker
4. 在 checkpoint record / review 中塞入 durable checkpoint id、resume token、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张 checkpoint-facing 稳定边界

## 文档与测试联动约束

V0.11 当前要求文档、测试和实现保持同步：

1. 改 `CheckpointRecord` 稳定字段时，要同步更新 checkpoint compatibility 文档与 `tests/checkpoint/`
2. 改 `CheckpointReviewSummary` 稳定字段时，要同步更新 checkpoint compatibility 文档与 `tests/checkpoint/`
3. 改 checkpoint-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 checkpoint 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake`

## 当前状态

截至当前实现：

1. checkpoint record 的 model / validation / bootstrap / emission 已全部落地
2. checkpoint review 的 model / validation / emission 已全部落地
3. checkpoint compatibility、golden 与 regression 标签切片已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> checkpoint-review`
