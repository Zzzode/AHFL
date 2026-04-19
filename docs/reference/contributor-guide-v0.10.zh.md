# AHFL Contributor Guide V0.10

本文给出面向新贡献者的 V0.10 上手路径，重点覆盖 scheduler snapshot、scheduler review、compatibility contract、golden regression，以及它们与 V0.9 runtime-adjacent artifact 的协作边界。

关联文档：

- [contributor-guide-v0.9.zh.md](./contributor-guide-v0.9.zh.md)
- [native-consumer-matrix-v0.10.zh.md](./native-consumer-matrix-v0.10.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](./scheduler-prototype-compatibility-v0.10.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)

## 先跑通的六条路径

建议先在本地确认六条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-scheduler-snapshot \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-scheduler-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-scheduler-snapshot \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-scheduler-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-compatibility
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-golden
```

## 按改动类型找入口

### 1. 改 scheduler snapshot 模型 / validation / bootstrap

通常要改的文件：

- `include/ahfl/scheduler_snapshot/snapshot.hpp`
- `src/scheduler_snapshot/snapshot.cpp`
- `tests/scheduler_snapshot/`
- `tests/scheduler/`

### 2. 改 scheduler decision summary / review validation

通常要改的文件：

- `include/ahfl/scheduler_snapshot/review.hpp`
- `src/scheduler_snapshot/review.cpp`
- `src/backends/scheduler_review.cpp`
- `tests/scheduler_snapshot/`
- `tests/scheduler/`

### 3. 改 scheduler CLI / backend 输出

通常要改的文件：

- `include/ahfl/backends/scheduler_snapshot.hpp`
- `include/ahfl/backends/scheduler_review.hpp`
- `src/backends/scheduler_snapshot.cpp`
- `src/backends/scheduler_review.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

### 4. 改 scheduler compatibility / consumer guidance

通常要改的文件：

- `docs/reference/scheduler-prototype-compatibility-v0.10.zh.md`
- `docs/reference/native-consumer-matrix-v0.10.zh.md`
- `docs/reference/contributor-guide-v0.10.zh.md`
- `docs/plan/roadmap-v0.10.zh.md`
- `docs/plan/issue-backlog-v0.10.zh.md`

## 推荐扩展顺序

V0.10 当前推荐的 scheduler-facing 扩展顺序是：

1. 若扩 planning / dependency 语义
   - 先改 `ExecutionPlan`
2. 若扩 workflow / node 当前状态语义
   - 先改 `RuntimeSession`
3. 若扩 event / failure sequencing 语义
   - 再改 `ExecutionJournal`
4. 若扩 consistency / progression 语义
   - 再改 `ReplayView`
5. 若扩 ready-set / blocked-frontier / executed-prefix 语义
   - 再改 `SchedulerSnapshot`
6. 若扩 reviewer-facing next-step / terminal reason
   - 最后改 `SchedulerDecisionSummary`

这表示：

1. `SchedulerSnapshot` 是 scheduler-facing local state 的第一事实来源
2. `SchedulerDecisionSummary` 是 snapshot 的 readable projection，不是独立状态机
3. durable checkpoint / resume protocol 若以后需要，应先扩 snapshot compatibility，而不是先改 review / CLI 文本

## Future Persistence Boundary Guidance

V0.10 当前 future persistence / checkpoint-friendly guidance 是：

1. 允许 future explorer 依赖 `SchedulerSnapshot` 的 ready set、blocked frontier、executed prefix 与 `checkpoint_friendly`
2. 不允许在当前版本中引入 durable checkpoint id、resume token、recovery ABI
3. 不允许把 host telemetry、provider payload、deployment metadata 塞入 snapshot / review
4. 不允许绕过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` 直接从 AST、trace、脚本推导 checkpoint state

推荐依赖顺序：

1. `ExecutionPlan`
2. `RuntimeSession`
3. `ExecutionJournal`
4. `ReplayView`
5. `SchedulerSnapshot`
6. `SchedulerDecisionSummary`

## 最小验证清单

只要触及 V0.10 scheduler-facing 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-compatibility
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-snapshot-model
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-snapshot-bootstrap
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-review-model
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-golden
```

若改动触及 scheduler CLI / output，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-snapshot-emission
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-review-emission
```

若想一次跑完整 V0.10 当前面：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.10
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-regression
```

## 当前反模式

V0.10 当前明确不建议：

1. 跳过 `SchedulerSnapshot`，直接在 `emit-scheduler-review` / 外部脚本里私造 next-step state machine
2. 把 `DryRunTrace` 当 scheduler prototype / persistence explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal`，直接从 AST、source、trace 重建 ready / blocked / prefix
4. 在 scheduler snapshot / review 中塞入 durable checkpoint id、resume token、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张 scheduler-facing 稳定边界

## 文档与测试联动约束

V0.10 当前要求文档、测试和实现保持同步：

1. 改 `SchedulerSnapshot` 稳定字段时，要同步更新 scheduler compatibility 文档与 `tests/scheduler/`
2. 改 `SchedulerDecisionSummary` 稳定字段时，要同步更新 scheduler compatibility 文档与 `tests/scheduler/`
3. 改 scheduler-facing layering / consumer 依赖顺序时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 scheduler 标签切片时，要同步更新 `tests/cmake/LabelTests.cmake`

## 当前状态

截至当前实现：

1. scheduler snapshot 的 model / validation / bootstrap / emission 已全部落地
2. scheduler review 的 model / validation / emission 已全部落地
3. scheduler compatibility、golden 与 regression 标签切片已形成最小闭环
4. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> review`
