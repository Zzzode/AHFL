# AHFL Contributor Guide V0.9

本文给出面向新贡献者的 V0.9 上手路径，重点覆盖 partial runtime session、failure-path execution journal、failure-aware replay view / audit report，以及它们与 V0.6 execution plan / dry-run trace 的协作边界。

关联文档：

- [contributor-guide-v0.8.zh.md](./contributor-guide-v0.8.zh.md)
- [native-consumer-matrix-v0.9.zh.md](./native-consumer-matrix-v0.9.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

## 先跑通的九条路径

建议先在本地确认九条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-execution-plan \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
./build/dev/src/cli/ahflc emit-runtime-session \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001
./build/dev/src/cli/ahflc emit-runtime-session \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-execution-journal \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-replay-view \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
./build/dev/src/cli/ahflc emit-replay-view \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
./build/dev/src/cli/ahflc emit-audit-report \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
./build/dev/src/cli/ahflc emit-audit-report \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.fail.mocks.json \
  --input-fixture fixture.request.failed \
  --run-id run-failed-001
ctest --preset test-dev --output-on-failure -L 'v0.9-(runtime-session-validation|runtime-session-bootstrap|runtime-session-emission|execution-journal-model|execution-journal-bootstrap|replay-view-model|replay-view-emission|audit-report-bootstrap|audit-report-emission)'
```

## 按改动类型找入口

### 1. 改 runtime session 模型 / validation

通常要改的文件：

- `include/ahfl/runtime_session/session.hpp`
- `src/runtime_session/session.cpp`
- `tests/runtime_session/`
- `tests/session/`

### 2. 改 execution journal 模型 / bootstrap

通常要改的文件：

- `include/ahfl/execution_journal/journal.hpp`
- `src/execution_journal/journal.cpp`
- `tests/execution_journal/`
- `tests/journal/`

### 3. 改 replay view 模型 / validation

通常要改的文件：

- `include/ahfl/replay_view/replay.hpp`
- `src/replay_view/replay.cpp`
- `tests/replay_view/`
- `tests/replay/`

### 4. 改 audit report 模型 / bootstrap

通常要改的文件：

- `include/ahfl/audit_report/report.hpp`
- `src/audit_report/report.cpp`
- `tests/audit_report/`
- `tests/audit/`

### 5. 改 runtime-adjacent CLI / backend 输出

通常要改的文件：

- `src/backends/runtime_session.cpp`
- `src/backends/execution_journal.cpp`
- `src/backends/replay_view.cpp`
- `src/backends/audit_report.cpp`
- `src/cli/ahflc.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

## 推荐扩展顺序

V0.9 当前推荐的 consumer 扩展顺序是：

1. 若扩 planning 语义
   - 先改 `ExecutionPlan`
2. 若扩 final state 语义
   - 先改 `RuntimeSession`
3. 若扩 event / failure sequencing 语义
   - 先改 `ExecutionJournal`
4. 若扩 replay consistency / progression 语义
   - 再改 `ReplayView`
5. 若扩 reviewer-facing aggregate summary
   - 最后改 `AuditReport`

这表示：

1. `RuntimeSession` 是 partial / failed state 的第一事实来源。
2. `ExecutionJournal` 是 failure progression / terminal event 的第一事实来源。
3. `ReplayView` 不是 journal 的替代品，而是 journal + session 的投影层。
4. `AuditReport` 不是 trace 的替代品，而是 plan / session / journal / trace / replay 的聚合层。
5. failure-path 扩展应优先落在 session / journal compatibility，而不是直接修改 replay / audit / CLI 结论。

## Richer Scheduler Prototype 推荐依赖顺序

V0.9 当前 richer scheduler prototype 的推荐依赖顺序是：

1. 先读 `ExecutionPlan`
   - 获取 planning DAG、dependency、binding 与 return contract
2. 再读 `RuntimeSession`
   - 获取 workflow / node 当前稳定状态
3. 再读 `ExecutionJournal`
   - 获取 event ordering 与 failure progression
4. 若需要 consistency 视角
   - 再读 `ReplayView`
5. 若需要 reviewer-facing aggregate summary
   - 最后读 `AuditReport`

明确禁止：

1. 直接从 `ReplayView` / `AuditReport` 倒推 scheduler state machine
2. 直接从 `DryRunTrace` 猜测 runtime terminal failure
3. 跳过 `RuntimeSession` / `ExecutionJournal` 在 CLI / 脚本里私造失败语义

## 最小验证清单

只要触及 V0.9 failure-path 主链路，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.9-runtime-session-validation
ctest --preset test-dev --output-on-failure -L v0.9-runtime-session-bootstrap
ctest --preset test-dev --output-on-failure -L v0.9-runtime-session-emission
ctest --preset test-dev --output-on-failure -L v0.9-execution-journal-model
ctest --preset test-dev --output-on-failure -L v0.9-execution-journal-bootstrap
ctest --preset test-dev --output-on-failure -L v0.9-replay-view-model
ctest --preset test-dev --output-on-failure -L v0.9-replay-view-emission
ctest --preset test-dev --output-on-failure -L v0.9-audit-report-bootstrap
ctest --preset test-dev --output-on-failure -L v0.9-audit-report-emission
```

若改动触及 V0.9 上游 planning / trace 输入，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.6-execution-plan-validation
ctest --preset test-dev --output-on-failure -L v0.6-dry-run-trace
```

若想一次跑完整 V0.9 当前面：

```bash
ctest --preset test-dev --output-on-failure -L 'v0.9-(runtime-session-validation|runtime-session-bootstrap|runtime-session-emission|execution-journal-model|execution-journal-bootstrap|replay-view-model|replay-view-emission|audit-report-bootstrap|audit-report-emission)'
```

## 当前反模式

V0.9 当前明确不建议：

1. 把 `DryRunTrace` 当 replay verifier / audit archive / scheduler prototype 的唯一正式输入
2. 跳过 `RuntimeSession`，直接从 trace、AST 或 source 文本重建 node 当前状态
3. 跳过 `ExecutionJournal`，直接从 trace、AST 或 source 文本重建 failure progression
4. 跳过 `ReplayView`，在 audit 输出层重新实现 session / journal consistency 判断
5. 在 replay / audit / CLI / 外部脚本层私造失败 taxonomy，而不先进入 session / journal
6. 为了 failure-path 直接把真实 host log、provider payload、deployment telemetry 塞进 session / journal / replay / audit
7. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张稳定边界

## 文档与测试联动约束

V0.9 当前要求文档、测试和实现保持同步：

1. 改 runtime session 稳定字段时，要同步更新 runtime session compatibility 文档与 `tests/session/`
2. 改 execution journal 稳定字段时，要同步更新 execution journal compatibility 文档与 `tests/journal/`
3. 改 replay 稳定字段时，要同步更新 replay compatibility 文档与 `tests/replay/`
4. 改 audit 稳定字段时，要同步更新 audit compatibility 文档与 `tests/audit/`
5. 改 session / journal / replay / audit 层次关系时，要同步更新 consumer matrix
6. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
7. 改 V0.9 标签切片时，要同步更新 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. runtime session 的 model / validation / bootstrap / emission 已全部落地
2. execution journal 的 model / bootstrap / emission 已全部落地
3. replay view 已正式进入 failure-aware projection 与 CLI emission
4. audit report 已正式进入 failure-aware aggregate summary 与 CLI emission
5. 当前推荐扩展顺序已正式冻结为 `plan -> session -> journal -> replay -> audit`
6. V0.9 failure-path 标签切片、CI 与 reference 文档入口已形成闭环
7. 当前下一步若继续推进，更适合规划 V0.10 / richer scheduler prototype 的下一阶段边界，而不是继续扩张私有 runtime 语义
