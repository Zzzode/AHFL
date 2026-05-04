# AHFL V0.31 Durable Store Provider Local Host Harness Compatibility

V0.31 的兼容边界是 test-only local host harness。

## Stable Formats

- `ahfl.durable-store-import-provider-local-host-harness-request.v1`
- `ahfl.durable-store-import-provider-local-host-harness-execution-record.v1`
- `ahfl.durable-store-import-provider-local-host-harness-review.v1`

## Compatibility Rules

1. sandbox policy 必须 test-only。
2. network、secret、filesystem write、host env、真实 provider SDK 默认禁止。
3. execution record 必须稳定表达 success、nonzero exit、timeout、sandbox denied。
4. 只有 success review 可以进入 SDK payload materialization。
