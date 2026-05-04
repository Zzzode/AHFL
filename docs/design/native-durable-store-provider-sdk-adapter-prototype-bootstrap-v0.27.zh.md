# AHFL V0.27 Native Durable Store Provider SDK Adapter Prototype Bootstrap

本文定义 V0.27 provider SDK adapter artifact prototype 的最小实现边界。V0.27 不接入真实 SDK、credential、secret manager、config loader、provider endpoint、host process、网络、对象存储、数据库 writer 或 filesystem output；它在 V0.26 `ProviderLocalHostExecutionReceipt` 之后冻结 SDK adapter request / response placeholder / readiness review contract。

## Artifact Chain

V0.27 的新增链路是：

```text
ProviderLocalHostExecutionReceipt
-> ProviderSdkAdapterRequestPlan
-> ProviderSdkAdapterResponsePlaceholder
-> ProviderSdkAdapterReadinessReview
```

`ProviderSdkAdapterRequestPlan` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-sdk-adapter-request-plan.v1
```

`ProviderSdkAdapterResponsePlaceholder` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-sdk-adapter-response-placeholder.v1
```

`ProviderSdkAdapterReadinessReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-sdk-adapter-readiness-review.v1
```

## SDK Adapter Boundary

V0.27 把职责拆成三层：

1. local host execution receipt：V0.26 生成 simulated local host receipt identity。
2. SDK adapter request plan：在 ready receipt 上生成 deterministic adapter request identity 与 response placeholder identity。
3. SDK adapter interface / config / secret / real provider：future extension，未来才处理 adapter ABI、config snapshot、secret handle、endpoint、object path、database table、SDK payload 与 provider response。

## Execution Semantics

1. ready `ProviderLocalHostExecutionReceipt` 生成 ready request plan、deterministic `provider-sdk-adapter-request::<provider-local-host-execution-receipt-id>` identity。
2. ready request plan 生成 ready response placeholder、deterministic `provider-sdk-adapter-response-placeholder::<provider-sdk-adapter-request-id>` identity。
3. blocked source 生成 blocked noop plan / placeholder，并保留 failure attribution。
4. `materializes_sdk_request_payload`、`invokes_provider_sdk`、`opens_network_connection`、`reads_secret_material`、`reads_host_environment`、`writes_host_filesystem` 必须为 `false`。
5. `provider_endpoint_uri`、`object_path`、`database_table`、`sdk_request_payload`、`sdk_response_payload` 必须为空。

## Layering Rules

1. `ProviderSdkAdapterRequestPlan` 只能消费 V0.26 `ProviderLocalHostExecutionReceipt`。
2. `ProviderSdkAdapterResponsePlaceholder` 只能消费 `ProviderSdkAdapterRequestPlan`。
3. `ProviderSdkAdapterReadinessReview` 只能消费 `ProviderSdkAdapterResponsePlaceholder`。
4. readiness text、CLI 文本、trace、host log、secret manager response、provider payload、SDK payload、endpoint、host command、host environment、network endpoint 与私有脚本不能成为 SDK adapter artifact state source。
5. V0.27 不修改 V0.26 local host execution receipt 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_sdk_adapter.hpp`
- `src/durable_store_import/provider_sdk_adapter.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-sdk-adapter-request`
- `emit-durable-store-import-provider-sdk-adapter-response-placeholder`
- `emit-durable-store-import-provider-sdk-adapter-readiness`

回归入口：

- `ahfl-v0.27`
- `v0.27-durable-store-import-provider-sdk-adapter-request-model`
- `v0.27-durable-store-import-provider-sdk-adapter-request-bootstrap`
- `v0.27-durable-store-import-provider-sdk-adapter-response-placeholder-model`
- `v0.27-durable-store-import-provider-sdk-adapter-readiness-model`
- `v0.27-durable-store-import-provider-sdk-adapter-emission`
- `v0.27-durable-store-import-provider-sdk-adapter-golden`
