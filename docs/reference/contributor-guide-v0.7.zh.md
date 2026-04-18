# AHFL Contributor Guide V0.7

本文给出面向新贡献者的 V0.7 上手路径，重点覆盖 runtime session、execution journal 以及与 V0.6 execution plan / dry-run trace 的协作边界。

关联文档：

- [contributor-guide-v0.6.zh.md](./contributor-guide-v0.6.zh.md)
- [native-consumer-matrix-v0.7.zh.md](./native-consumer-matrix-v0.7.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./execution-journal-compatibility-v0.7.zh.md)
- [native-runtime-session-bootstrap-v0.7.zh.md](../design/native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](../design/native-execution-journal-v0.7.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

## 先跑通的六条路径

建议先在本地确认六条典型路径都可运行：

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
ctest --preset test-dev --output-on-failure -L 'v0.7-(runtime-session-model|runtime-session-validation|runtime-session-emission|execution-journal-model|execution-journal-bootstrap|execution-journal-emission)'
```

## 按改动类型找入口

### 1. 改 runtime session 模型 / validation

通常要改的文件：

- `include/ahfl/runtime_session/session.hpp`
- `src/runtime_session/session.cpp`
- `include/ahfl/backends/runtime_session.hpp`
- `src/backends/runtime_session.cpp`
- `tests/runtime_session/`
- `tests/session/`

### 2. 改 execution journal 模型 / bootstrap

通常要改的文件：

- `include/ahfl/execution_journal/journal.hpp`
- `src/execution_journal/journal.cpp`
- `tests/execution_journal/`

### 3. 改 journal 输出 / CLI 参数面

通常要改的文件：

- `include/ahfl/backends/execution_journal.hpp`
- `src/backends/execution_journal.cpp`
- `src/cli/ahflc.cpp`
- `tests/journal/`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`

## 最小验证清单

只要触及 V0.7 session / journal 路径，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.7-runtime-session-model
ctest --preset test-dev --output-on-failure -L v0.7-runtime-session-validation
ctest --preset test-dev --output-on-failure -L v0.7-runtime-session-emission
ctest --preset test-dev --output-on-failure -L v0.7-execution-journal-model
ctest --preset test-dev --output-on-failure -L v0.7-execution-journal-bootstrap
ctest --preset test-dev --output-on-failure -L v0.7-execution-journal-emission
```

若想一次跑完整 V0.7 当前面：

```bash
ctest --preset test-dev --output-on-failure -L 'v0.7-(runtime-session-model|runtime-session-validation|runtime-session-emission|execution-journal-model|execution-journal-bootstrap|execution-journal-emission)'
```

## 文档与测试联动约束

V0.7 当前要求文档、测试和实现保持同步：

1. 改 runtime session 稳定字段时，要同步更新 runtime session compatibility 文档与 `tests/session/`
2. 改 execution journal 稳定字段时，要同步更新 execution journal compatibility 文档与 `tests/journal/`
3. 改 session / journal 层次关系时，要同步更新 consumer matrix
4. 改 contributor-facing 扩展路径时，要同步更新 `docs/README.md`、roadmap 与 backlog
5. 改 V0.7 标签切片时，要同步更新 `.github/workflows/ci.yml`
