# AHFL Contributor Guide V0.20

本文给出面向贡献者的 V0.20 durable store adapter execution 上手路径，重点覆盖 local fake durable store contract、adapter execution receipt、recovery preview、golden regression 与 CI 标签。

关联文档：

- [native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md](../design/native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md)
- [durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md](./durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md)
- [native-consumer-matrix-v0.20.zh.md](./native-consumer-matrix-v0.20.zh.md)

## 先跑通的路径

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-adapter-execution \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-recovery-preview \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
ctest --preset test-dev --output-on-failure -L ahfl-v0.20
```

## 按改动类型找入口

### 1. 改 adapter execution model / validation / bootstrap

- `include/ahfl/durable_store_import/adapter_execution.hpp`
- `src/durable_store_import/adapter_execution.cpp`
- `tests/durable_store_import/decision.cpp`
- `tests/durable_store_import/*.durable-store-import-adapter-execution.json`

### 2. 改 recovery preview model / validation / output

- `include/ahfl/durable_store_import/recovery_preview.hpp`
- `src/durable_store_import/recovery_preview.cpp`
- `src/backends/durable_store_import_recovery_preview.cpp`
- `tests/durable_store_import/*.durable-store-import-recovery-preview`

### 3. 改 CLI / backend 输出

- `include/ahfl/backends/durable_store_import_adapter_execution.hpp`
- `include/ahfl/backends/durable_store_import_recovery_preview.hpp`
- `src/backends/CMakeLists.txt`
- `src/cli/command_catalog.cpp`
- `src/cli/pipeline_runner.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

### 4. 改 compatibility / consumer guidance

- `docs/design/native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md`
- `docs/reference/durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md`
- `docs/reference/native-consumer-matrix-v0.20.zh.md`
- `docs/reference/contributor-guide-v0.20.zh.md`
- `README.md`
- `MIGRATION.md`
- `docs/README.md`

## 最小验证清单

```bash
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-adapter-execution-model
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-adapter-execution-bootstrap
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-recovery-preview-model
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-adapter-execution-golden
```

若改 CLI 输出，再跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-adapter-execution-emission
```

## Boundary Rules

1. `AdapterExecutionReceipt` 只能从 V0.19 `Response` 构建。
2. `RecoveryCommandPreview` 只能从 `AdapterExecutionReceipt` 构建。
3. fake store contract 不等价于真实 provider adapter。
4. 当前版本不得加入 credential、真实 store URI、object path、database key、host telemetry 或 recovery daemon ABI。
5. 改稳定字段时必须同步 compatibility 文档、golden、label 与 CI。

## 当前状态

截至 V0.20：

1. adapter execution receipt model / validation / bootstrap / emission 已落地
2. recovery preview model / validation / emission 已落地
3. single-file accepted、project rejected、workspace partial accepted 已有 golden
4. `ahfl-v0.20` 已覆盖 direct、emission 与 golden regression
