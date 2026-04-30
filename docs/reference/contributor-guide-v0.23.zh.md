# AHFL V0.23 Contributor Guide

本指南说明 V0.23 provider runtime preflight prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md](../design/native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md)
- [durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md](./durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md)
- [native-consumer-matrix-v0.23.zh.md](./native-consumer-matrix-v0.23.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-runtime-preflight \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-runtime-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.23
ctest --preset test-dev --output-on-failure -L v0.23-durable-store-import-provider-runtime-golden
```

ASan slice：

```sh
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure -L ahfl-v0.23
```

## Golden Files

V0.23 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-runtime-preflight.json`
- `tests/durable_store_import/*.durable-store-import-provider-runtime-readiness`

更新 golden 时必须同步：

1. `tests/cmake/ProjectTests.cmake`
2. `tests/cmake/SingleFileCliTests.cmake`
3. `tests/cmake/LabelTests.cmake`
4. `docs/reference/durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md`
5. `docs/reference/native-consumer-matrix-v0.23.zh.md`

## 禁止事项

V0.23 代码和 golden 中不得加入：

1. credential reference、secret value、secret manager endpoint URI
2. provider endpoint URI、object path、database table
3. SDK payload schema、SDK request / response payload
4. runtime config loader output、secret resolver output
5. `loads_runtime_config = true`、`resolves_secret_handles = true`、`invokes_provider_sdk = true`

## Checklist

提交前确认：

1. `ProviderRuntimeProfile` validation 覆盖 forbidden fields
2. `ProviderRuntimePreflightPlan` ready / blocked 状态都有 direct regression
3. readiness review 只消费 preflight plan
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.23` 已覆盖 direct、emission 与 golden regression
