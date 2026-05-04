# AHFL V0.28 Contributor Guide

本指南说明 V0.28 provider SDK adapter interface prototype 的代码、文档、golden 与测试同步流程。

## 必读文档

- [native-durable-store-provider-sdk-adapter-interface-bootstrap-v0.28.zh.md](../design/native-durable-store-provider-sdk-adapter-interface-bootstrap-v0.28.zh.md)
- [durable-store-provider-sdk-adapter-interface-compatibility-v0.28.zh.md](./durable-store-provider-sdk-adapter-interface-compatibility-v0.28.zh.md)
- [native-consumer-matrix-v0.28.zh.md](./native-consumer-matrix-v0.28.zh.md)

## 常用命令

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-interface \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-interface-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.28
ctest --preset test-dev --output-on-failure -L v0.28-durable-store-import-provider-sdk-adapter-interface-golden
```

ASan slice：

```sh
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure -L ahfl-v0.28
```

## Golden Files

V0.28 golden 文件位于：

- `tests/durable_store_import/*.durable-store-import-provider-sdk-adapter-interface.json`
- `tests/durable_store_import/*.durable-store-import-provider-sdk-adapter-interface-review`

## Checklist

提交前确认：

1. interface plan validation 覆盖 descriptor、capability、taxonomy 与 forbidden material
2. ready / blocked 状态都有 direct regression
3. review 只消费 interface plan
4. golden 覆盖 single-file、project manifest、workspace 三种入口
5. `ahfl-v0.28` 已覆盖 direct、emission 与 golden regression
