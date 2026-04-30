# AHFL Contributor Guide V0.22

本文给出面向贡献者的 V0.22 provider driver binding 上手路径，重点覆盖 secret-free driver profile、driver capability gating、binding plan、readiness review、golden regression 与 CI 标签。

关联文档：

- [native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md](../design/native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md)
- [durable-store-provider-driver-prototype-compatibility-v0.22.zh.md](./durable-store-provider-driver-prototype-compatibility-v0.22.zh.md)
- [native-consumer-matrix-v0.22.zh.md](./native-consumer-matrix-v0.22.zh.md)

## 先跑通的路径

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc ahfl_durable_store_import_decision_tests
./build/dev/src/cli/ahflc emit-durable-store-import-provider-driver-binding \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-driver-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
ctest --preset test-dev --output-on-failure -L ahfl-v0.22
```

## 按改动类型找入口

### 1. 改 provider driver model / validation / bootstrap

- `include/ahfl/durable_store_import/provider_driver.hpp`
- `src/durable_store_import/provider_driver.cpp`
- `tests/durable_store_import/decision.cpp`

### 2. 改 provider driver binding JSON 输出

- `include/ahfl/backends/durable_store_import_provider_driver_binding.hpp`
- `src/backends/durable_store_import_provider_driver_binding.cpp`
- `tests/durable_store_import/*.durable-store-import-provider-driver-binding.json`

### 3. 改 provider driver readiness 文本输出

- `include/ahfl/backends/durable_store_import_provider_driver_readiness.hpp`
- `src/backends/durable_store_import_provider_driver_readiness.cpp`
- `tests/durable_store_import/*.durable-store-import-provider-driver-readiness`

### 4. 改 CLI / golden / label / CI

- `src/backends/CMakeLists.txt`
- `src/cli/command_catalog.cpp`
- `src/cli/pipeline_runner.cpp`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `.github/workflows/ci.yml`

## 最小验证清单

```bash
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-profile-model
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-binding-model
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-binding-bootstrap
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-readiness-model
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-golden
```

若改 CLI 输出，再跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-emission
```

## Boundary Rules

1. `ProviderDriverBindingPlan` 只能从 V0.21 `ProviderWriteAttemptPreview` 与 driver profile 构建。
2. `ProviderDriverReadinessReview` 只能从 `ProviderDriverBindingPlan` 构建。
3. readiness review 不是 provider SDK ABI。
4. 当前版本不得加入 credential、endpoint URI、真实 object path、database table、host telemetry、provider SDK payload 或 recovery daemon ABI。
5. 改稳定字段时必须同步 compatibility 文档、golden、label 与 CI。

## 当前状态

截至 V0.22：

1. provider driver profile / binding plan / readiness review model 已落地
2. provider driver binding JSON emission 已落地
3. provider driver readiness reviewer emission 已落地
4. single-file accepted、project rejected、workspace partial accepted 已有 golden
5. `ahfl-v0.22` 已覆盖 direct、emission 与 golden regression
