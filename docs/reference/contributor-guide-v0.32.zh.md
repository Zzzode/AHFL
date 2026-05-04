# AHFL V0.32 Contributor Guide

## Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-payload-plan --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-payload-audit --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.32
```

提交前确认 raw payload、secret、credential、token 不进入 artifact。
