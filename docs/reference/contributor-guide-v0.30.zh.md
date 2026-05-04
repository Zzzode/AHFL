# AHFL V0.30 Contributor Guide

## Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-resolver-request --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-resolver-response --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-policy-review --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.30
```

提交前确认 secret resolver artifact 不包含明文 secret、credential、token 或 secret manager response。
