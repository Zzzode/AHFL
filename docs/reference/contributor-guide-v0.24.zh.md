# AHFL V0.24 Contributor Guide

本指南说明 V0.24 provider SDK request envelope prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md](../design/native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md)
- [durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md](./durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md)
- [native-consumer-matrix-v0.24.zh.md](./native-consumer-matrix-v0.24.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-envelope \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-handoff-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.24
ctest --preset test-dev --output-on-failure -L v0.24-durable-store-import-provider-sdk-golden
```

ASan slice：

```sh
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure -L ahfl-v0.24
```

## Golden Files

V0.24 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-sdk-envelope.json`
- `tests/durable_store_import/*.durable-store-import-provider-sdk-handoff-readiness`

更新 golden 时必须同步：

1. `tests/cmake/ProjectTests.cmake`
2. `tests/cmake/SingleFileCliTests.cmake`
3. `tests/cmake/LabelTests.cmake`
4. `docs/reference/durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md`
5. `docs/reference/native-consumer-matrix-v0.24.zh.md`

## 禁止事项

V0.24 代码和 golden 中不得加入：

1. credential reference、secret value、provider endpoint URI
2. object path、database table、network endpoint URI
3. SDK request / response payload、provider request / response payload
4. host command、host telemetry、operator payload
5. `materializes_sdk_request_payload = true`、`starts_host_process = true`、`opens_network_connection = true`、`invokes_provider_sdk = true`

## Checklist

提交前确认：

1. `ProviderSdkEnvelopePolicy` validation 覆盖 forbidden fields
2. `ProviderSdkRequestEnvelopePlan` ready / blocked 状态都有 direct regression
3. handoff readiness review 只消费 request envelope plan
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.24` 已覆盖 direct、emission 与 golden regression
