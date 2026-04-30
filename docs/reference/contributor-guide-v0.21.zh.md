# AHFL Contributor Guide V0.21

本文给出面向贡献者的 V0.21 provider-neutral durable store adapter 上手路径，重点覆盖 secret-free provider config、capability matrix、provider write attempt、recovery handoff、golden regression 与 CI 标签。

关联文档：

- [native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md](../design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md)
- [durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md](./durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md)
- [native-consumer-matrix-v0.21.zh.md](./native-consumer-matrix-v0.21.zh.md)

## 先跑通的路径

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-attempt \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-recovery-handoff \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
ctest --preset test-dev --output-on-failure -L ahfl-v0.21
```

## 按改动类型找入口

### 1. 改 provider adapter model / validation / bootstrap

- `include/ahfl/durable_store_import/provider_adapter.hpp`
- `src/durable_store_import/provider_adapter.cpp`
- `tests/durable_store_import/decision.cpp`

### 2. 改 provider write attempt JSON 输出

- `include/ahfl/backends/durable_store_import_provider_write_attempt.hpp`
- `src/backends/durable_store_import_provider_write_attempt.cpp`
- `tests/durable_store_import/*.durable-store-import-provider-write-attempt.json`

### 3. 改 provider recovery handoff 文本输出

- `include/ahfl/backends/durable_store_import_provider_recovery_handoff.hpp`
- `src/backends/durable_store_import_provider_recovery_handoff.cpp`
- `tests/durable_store_import/*.durable-store-import-provider-recovery-handoff`

### 4. 改 CLI / golden / label / CI

- `src/backends/CMakeLists.txt`
- `src/cli/command_catalog.cpp`
- `src/cli/pipeline_runner.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

### 5. 改 compatibility / consumer guidance

- `docs/design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md`
- `docs/reference/durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md`
- `docs/reference/native-consumer-matrix-v0.21.zh.md`
- `docs/reference/contributor-guide-v0.21.zh.md`
- `README.md`
- `MIGRATION.md`
- `docs/README.md`

## 最小验证清单

```bash
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-adapter-config-model
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-write-attempt-model
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-write-attempt-bootstrap
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-recovery-handoff-model
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-adapter-golden
```

若改 CLI 输出，再跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-adapter-emission
```

## Boundary Rules

1. `ProviderWriteAttemptPreview` 只能从 V0.20 `AdapterExecutionReceipt`、provider config 与 capability matrix 构建。
2. `ProviderRecoveryHandoffPreview` 只能从 `ProviderWriteAttemptPreview` 构建。
3. `RecoveryCommandPreview` 不是 provider adapter ABI。
4. 当前版本不得加入 credential、endpoint secret、真实 object path、database key、host telemetry、provider SDK result 或 recovery daemon ABI。
5. 改稳定字段时必须同步 compatibility 文档、golden、label 与 CI。

## 当前状态

截至 V0.21：

1. provider adapter config / capability matrix / write attempt / recovery handoff model 已落地
2. provider write attempt JSON emission 已落地
3. provider recovery handoff reviewer emission 已落地
4. single-file accepted、project rejected、workspace partial accepted 已有 golden
5. `ahfl-v0.21` 已覆盖 direct、emission 与 golden regression
