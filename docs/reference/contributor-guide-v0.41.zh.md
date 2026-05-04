# AHFL V0.41 Contributor Guide

## Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-registry --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-selection-plan --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-capability-negotiation-review --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.41
```

提交前确认 fallback policy 保持 audited-fallback-only，且 blocked selection 需要 operator review。
