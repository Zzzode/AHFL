# AHFL Contributor Guide V0.6

本文给出面向新贡献者的 V0.6 上手路径，重点覆盖 execution plan、capability mock、local dry-run 与 dry-run trace。

关联文档：

- [contributor-guide-v0.5.zh.md](./contributor-guide-v0.5.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [native-consumer-matrix-v0.6.zh.md](./native-consumer-matrix-v0.6.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./dry-run-trace-compatibility-v0.6.zh.md)
- [native-execution-plan-architecture-v0.6.zh.md](../design/native-execution-plan-architecture-v0.6.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](../design/native-dry-run-bootstrap-v0.6.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

## 先跑通的五条路径

建议先在本地确认五条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-execution-plan \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
./build/dev/src/cli/ahflc emit-dry-run-trace \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001
ctest --preset test-dev --output-on-failure -L 'v0.6-execution-plan-validation|v0.6-dry-run-trace'
```

## 按改动类型找入口

### 1. 改 execution plan 模型 / validation

通常要改的文件：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `include/ahfl/backends/execution_plan.hpp`
- `src/backends/execution_plan.cpp`
- `tests/handoff/package_model.cpp`
- `tests/plan/`

### 2. 改 capability mock 输入 / dry-run runner

通常要改的文件：

- `include/ahfl/dry_run/runner.hpp`
- `src/dry_run/mock.cpp`
- `src/dry_run/runner.cpp`
- `tests/dry_run/`

### 3. 改 trace 输出 / CLI 参数面

通常要改的文件：

- `include/ahfl/backends/dry_run_trace.hpp`
- `src/backends/dry_run_trace.cpp`
- `src/cli/ahflc.cpp`
- `tests/trace/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`

## 最小验证清单

只要触及 V0.6 plan / runner / trace 路径，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.6-execution-plan-model
ctest --preset test-dev --output-on-failure -L v0.6-execution-plan-emission
ctest --preset test-dev --output-on-failure -L v0.6-execution-plan-validation
ctest --preset test-dev --output-on-failure -L v0.6-dry-run-model
ctest --preset test-dev --output-on-failure -L v0.6-dry-run-mock-input
ctest --preset test-dev --output-on-failure -L v0.6-dry-run-trace
```

若想一次跑完整 V0.6 当前面：

```bash
ctest --preset test-dev --output-on-failure -L 'v0.6-execution-plan-model|v0.6-execution-plan-emission|v0.6-execution-plan-validation|v0.6-dry-run-model|v0.6-dry-run-mock-input|v0.6-dry-run-trace'
```

## 文档与测试联动约束

V0.6 当前要求文档、测试和实现保持同步：

1. 改 execution plan 稳定字段时，要同步更新 execution plan compatibility 文档与 `tests/plan/`
2. 改 mock 输入稳定字段时，要同步更新 dry-run trace compatibility 文档与 `tests/dry_run/`
3. 改 trace 输出稳定字段时，要同步更新 `tests/trace/`
4. 改 runtime-adjacent 层次关系时，要同步更新 consumer matrix
5. 新增 contributor-facing 路径时，要同步更新 `docs/README.md` 与 `README.md`
