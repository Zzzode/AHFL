# AHFL V0.37 Contributor Guide

## Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-recovery-checkpoint --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-recovery-plan --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-recovery-review --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.37
```

提交前确认 recovery plan 保持 secret-free，partial write 路径保留 idempotency key 与 resume token placeholder。
