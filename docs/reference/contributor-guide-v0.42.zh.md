# AHFL V0.42 Contributor Guide

## Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-production-readiness-evidence --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-production-readiness-review --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
./build/dev/src/cli/ahflc emit-durable-store-import-provider-production-readiness-report --package tests/ir/ok_workflow_value_flow.package.json --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 tests/ir/ok_workflow_value_flow.ahfl
```

## Regression

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.42
```

提交前确认 readiness report 只作为生产评审输入，不自动批准发布或启用真实 provider traffic。
