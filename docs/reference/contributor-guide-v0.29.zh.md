# AHFL V0.29 Contributor Guide

本指南说明 V0.29 provider config loader prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-config-loader-bootstrap-v0.29.zh.md](../design/native-durable-store-provider-config-loader-bootstrap-v0.29.zh.md)
- [durable-store-provider-config-loader-compatibility-v0.29.zh.md](./durable-store-provider-config-loader-compatibility-v0.29.zh.md)
- [native-consumer-matrix-v0.29.zh.md](./native-consumer-matrix-v0.29.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-load \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-snapshot \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.29
ctest --preset test-dev --output-on-failure -L v0.29-durable-store-import-provider-config-golden
```

## Golden Files

V0.29 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-config-load.json`
- `tests/durable_store_import/*.durable-store-import-provider-config-snapshot.json`
- `tests/durable_store_import/*.durable-store-import-provider-config-readiness`

## Checklist

提交前确认：

1. config load validation 覆盖 forbidden material 与 side-effect flags
2. ready / blocked 状态都有 direct regression
3. snapshot placeholder 不包含 secret、credential、endpoint 或 remote response
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.29` 已覆盖 direct、emission 与 golden regression
