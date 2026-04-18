# AHFL Contributor Guide V0.8

本文给出面向新贡献者的 V0.8 上手路径，重点覆盖 replay view、audit report 以及它们与 V0.7 runtime session / execution journal、V0.6 execution plan / dry-run trace 的协作边界。

关联文档：

- [contributor-guide-v0.7.zh.md](./contributor-guide-v0.7.zh.md)
- [native-consumer-matrix-v0.8.zh.md](./native-consumer-matrix-v0.8.zh.md)
- [replay-view-compatibility-v0.8.zh.md](./replay-view-compatibility-v0.8.zh.md)
- [audit-report-compatibility-v0.8.zh.md](./audit-report-compatibility-v0.8.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./execution-journal-compatibility-v0.7.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](../design/native-replay-audit-bootstrap-v0.8.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

## 先跑通的八条路径

建议先在本地确认八条典型路径都可运行：

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
./build/dev/src/cli/ahflc emit-execution-journal \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001
./build/dev/src/cli/ahflc emit-replay-view \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001
./build/dev/src/cli/ahflc emit-audit-report \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001
ctest --preset test-dev --output-on-failure -L 'v0.8-(replay-view-model|replay-view-validation|replay-view-emission|audit-report-model|audit-report-bootstrap|audit-report-emission)'
```

## 按改动类型找入口

### 1. 改 replay view 模型 / validation

通常要改的文件：

- `include/ahfl/replay_view/replay.hpp`
- `src/replay_view/replay.cpp`
- `tests/replay_view/`

### 2. 改 replay 输出 / CLI 参数面

通常要改的文件：

- `include/ahfl/backends/replay_view.hpp`
- `src/backends/replay_view.cpp`
- `src/cli/ahflc.cpp`
- `tests/replay/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`

### 3. 改 audit report 模型 / bootstrap

通常要改的文件：

- `include/ahfl/audit_report/report.hpp`
- `src/audit_report/report.cpp`
- `tests/audit_report/`

### 4. 改 audit 输出 / CLI 参数面

通常要改的文件：

- `include/ahfl/backends/audit_report.hpp`
- `src/backends/audit_report.cpp`
- `src/cli/ahflc.cpp`
- `tests/audit/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

## 推荐扩展顺序

V0.8 当前推荐的 consumer 扩展顺序是：

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

1. `ReplayView` 不是 journal 的替代品，而是 journal + session 的投影层。
2. `AuditReport` 不是 trace 的替代品，而是 plan / session / journal / trace / replay 的聚合层。
3. failure-path 扩展应优先落在 journal / session compatibility，而不是直接修改 replay / audit 结论。

## 最小验证清单

只要触及 V0.8 replay / audit 路径，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.8-replay-view-model
ctest --preset test-dev --output-on-failure -L v0.8-replay-view-validation
ctest --preset test-dev --output-on-failure -L v0.8-replay-view-emission
ctest --preset test-dev --output-on-failure -L v0.8-audit-report-model
ctest --preset test-dev --output-on-failure -L v0.8-audit-report-bootstrap
ctest --preset test-dev --output-on-failure -L v0.8-audit-report-emission
```

若改动触及 replay / audit 的上游输入，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L v0.7-runtime-session-emission
ctest --preset test-dev --output-on-failure -L v0.7-execution-journal-emission
```

若想一次跑完整 V0.8 当前面：

```bash
ctest --preset test-dev --output-on-failure -L 'v0.8-(replay-view-model|replay-view-validation|replay-view-emission|audit-report-model|audit-report-bootstrap|audit-report-emission)'
```

## 当前反模式

V0.8 当前明确不建议：

1. 把 `DryRunTrace` 当 replay verifier / audit archive 的唯一正式输入
2. 跳过 `ExecutionJournal`，直接从 trace、AST 或 source 文本重建 replay progression
3. 跳过 `ReplayView`，在 audit 输出层重新实现 session / journal consistency 判断
4. 为了 failure-path 直接把真实 host log、provider payload、deployment telemetry 塞进 replay / audit
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张稳定边界

## 文档与测试联动约束

V0.8 当前要求文档、测试和实现保持同步：

1. 改 replay 稳定字段时，要同步更新 replay compatibility 文档与 `tests/replay/`
2. 改 audit 稳定字段时，要同步更新 audit compatibility 文档与 `tests/audit/`
3. 改 replay / audit 层次关系时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、`README.md`、roadmap 与 backlog
5. 改 V0.8 标签切片时，要同步更新 `.github/workflows/ci.yml`

## 当前状态

截至当前实现：

1. replay view 的 model / validation / emission 已全部落地
2. audit report 的 model / bootstrap / emission 已全部落地
3. replay / audit compatibility 文档已冻结版本与 failure-path 演进边界
4. 当前下一步若继续推进，应优先进入 V0.8 regression / CI / reference 闭环，而不是继续扩张私有 runtime 语义
