# AHFL V0.25 Contributor Guide

本指南说明 V0.25 provider host execution prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md](../design/native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md)
- [durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md](./durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md)
- [native-consumer-matrix-v0.25.zh.md](./native-consumer-matrix-v0.25.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-host-execution \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-host-execution-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.25
ctest --preset test-dev --output-on-failure -L v0.25-durable-store-import-provider-host-execution-golden
```

ASan slice：

```sh
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure -L ahfl-v0.25
```

## Golden Files

V0.25 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-host-execution.json`
- `tests/durable_store_import/*.durable-store-import-provider-host-execution-readiness`

更新 golden 时必须同步：

1. `tests/cmake/ProjectTests.cmake`
2. `tests/cmake/SingleFileCliTests.cmake`
3. `tests/cmake/LabelTests.cmake`
4. `docs/reference/durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md`
5. `docs/reference/native-consumer-matrix-v0.25.zh.md`

## 禁止事项

V0.25 代码和 golden 中不得加入：

1. credential reference、secret value、host environment secret
2. host command、provider endpoint URI、network endpoint URI
3. object path、database table
4. SDK request / response payload、provider request / response payload
5. `starts_host_process = true`、`reads_host_environment = true`、`opens_network_connection = true`、`materializes_sdk_request_payload = true`、`invokes_provider_sdk = true`、`writes_host_filesystem = true`

## Checklist

提交前确认：

1. `ProviderHostExecutionPolicy` validation 覆盖 forbidden fields
2. `ProviderHostExecutionPlan` ready / blocked 状态都有 direct regression
3. host execution readiness review 只消费 host execution plan
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.25` 已覆盖 direct、emission 与 golden regression
