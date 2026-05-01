# AHFL V0.26 Native Durable Store Provider Local Host Execution Prototype Bootstrap

本文定义 V0.26 provider local host execution receipt 的最小实现边界。V0.26 不接入 SDK、credential、secret manager、真实配置加载器、host process manager、网络、对象存储、数据库 writer 或 recovery daemon；它在 V0.25 `ProviderHostExecutionPlan` 之后冻结 local host execution receipt / review contract。

## Artifact Chain

V0.26 的新增链路是：

```text
ProviderHostExecutionPlan
-> ProviderLocalHostExecutionReceipt
-> ProviderLocalHostExecutionReceiptReview
```

`ProviderLocalHostExecutionReceipt` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-local-host-execution-receipt.v1
```

`ProviderLocalHostExecutionReceiptReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-local-host-execution-receipt-review.v1
```

## Local Host Execution Boundary

V0.26 把职责拆成三层：

1. provider host execution plan：V0.25 生成 host execution descriptor identity 与 dry-run host receipt placeholder identity。
2. provider local host execution receipt：在 ready plan 上生成 simulated local host receipt identity。
3. provider SDK adapter / production host harness：future extension，未来才处理 host command、host environment、config snapshot、secret resolution、endpoint、object path、database table 与 SDK payload。

## Execution Semantics

1. ready `ProviderHostExecutionPlan` 生成 simulated-ready receipt、deterministic `provider-local-host-execution-receipt::<provider-host-execution-descriptor-id>` identity。
2. blocked host execution plan 生成 blocked noop receipt，并保留 host-execution-not-ready failure attribution。
3. `starts_host_process`、`reads_host_environment`、`opens_network_connection`、`materializes_sdk_request_payload`、`invokes_provider_sdk`、`writes_host_filesystem` 必须为 `false`。
4. `ProviderLocalHostExecutionReceiptReview` 只解释 receipt 状态，不是 provider SDK adapter ABI。

## Layering Rules

1. `ProviderLocalHostExecutionReceipt` 只能消费 V0.25 `ProviderHostExecutionPlan`。
2. `ProviderLocalHostExecutionReceiptReview` 只能消费 `ProviderLocalHostExecutionReceipt`。
3. host execution readiness text、CLI 文本、trace、host log、secret manager response、provider payload、host command、host environment、network endpoint 与私有脚本不能成为 local host execution receipt state source。
4. V0.26 不修改 V0.25 provider host execution plan 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_local_host_execution.hpp`
- `src/durable_store_import/provider_local_host_execution.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-local-host-execution-receipt`
- `emit-durable-store-import-provider-local-host-execution-receipt-review`

回归入口：

- `ahfl-v0.26`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-model`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-bootstrap`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-review-model`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-emission`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-golden`
