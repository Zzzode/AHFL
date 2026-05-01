# AHFL V0.26 Contributor Guide

本指南说明 V0.26 provider local host execution receipt prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md](../design/native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md)
- [durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md](./durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md)
- [native-consumer-matrix-v0.26.zh.md](./native-consumer-matrix-v0.26.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-execution-receipt \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-execution-receipt-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.26
ctest --preset test-dev --output-on-failure -L v0.26-durable-store-import-provider-local-host-execution-receipt-golden
```

ASan slice：

```sh
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure -L ahfl-v0.26
```

## Golden Files

V0.26 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-local-host-execution-receipt.json`
- `tests/durable_store_import/*.durable-store-import-provider-local-host-execution-receipt-review`

更新 golden 时必须同步：

1. `tests/cmake/ProjectTests.cmake`
2. `tests/cmake/SingleFileCliTests.cmake`
3. `tests/cmake/LabelTests.cmake`
4. `docs/reference/durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md`
5. `docs/reference/native-consumer-matrix-v0.26.zh.md`

## 禁止事项

V0.26 代码和 golden 中不得加入：

1. credential reference、secret value、host environment secret
2. host command、provider endpoint URI、network endpoint URI
3. object path、database table
4. SDK request / response payload、provider request / response payload
5. `starts_host_process = true`、`reads_host_environment = true`、`opens_network_connection = true`、`materializes_sdk_request_payload = true`、`invokes_provider_sdk = true`、`writes_host_filesystem = true`

## Checklist

提交前确认：

1. `ProviderLocalHostExecutionReceipt` validation 覆盖 side-effect flags
2. ready / blocked receipt 状态都有 direct regression
3. receipt review 只消费 local host execution receipt
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.26` 已覆盖 direct、emission 与 golden regression
