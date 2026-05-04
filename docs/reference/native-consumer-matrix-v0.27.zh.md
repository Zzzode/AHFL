# AHFL V0.27 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider SDK Adapter Request Consumer | V0.26 `ProviderLocalHostExecutionReceipt` | `emit-durable-store-import-provider-sdk-adapter-request`、`durable_store_import::ProviderSdkAdapterRequestPlan`、`validate_provider_sdk_adapter_request_plan(...)` | local host receipt identity、source local host execution status、SDK adapter request identity、response placeholder identity、operation kind、request status、side-effect booleans、empty forbidden fields、failure attribution | credential、secret value、provider endpoint、network endpoint、object path、database table、SDK request / response payload、provider response payload、reviewer text |
| Provider SDK Adapter Response Placeholder Consumer | `ProviderSdkAdapterRequestPlan` | `emit-durable-store-import-provider-sdk-adapter-response-placeholder`、`durable_store_import::ProviderSdkAdapterResponsePlaceholder`、`validate_provider_sdk_adapter_response_placeholder(...)` | request artifact identity、response placeholder artifact identity、provider adapter request identity、provider adapter response placeholder identity、operation kind、response status、side-effect booleans、empty forbidden fields、failure attribution | real SDK ABI、provider request / response payload、config loader output、secret resolver output、host env、filesystem output、operator payload、host telemetry |
| Provider SDK Adapter Readiness Consumer | `ProviderSdkAdapterResponsePlaceholder` | `emit-durable-store-import-provider-sdk-adapter-readiness`、`durable_store_import::ProviderSdkAdapterReadinessReview`、`validate_provider_sdk_adapter_readiness_review(...)` | response placeholder identity、response status、operation kind、side-effect booleans、failure attribution、next action、next-step recommendation | machine state source、SDK payload、SDK call result、network response、secret resolver output、host log、CLI text |

## Artifact Order

V0.27 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-provider-host-execution`
2. `ahflc emit-durable-store-import-provider-host-execution-readiness`
3. `ahflc emit-durable-store-import-provider-local-host-execution-receipt`
4. `ahflc emit-durable-store-import-provider-local-host-execution-receipt-review`
5. `ahflc emit-durable-store-import-provider-sdk-adapter-request`
6. `ahflc emit-durable-store-import-provider-sdk-adapter-response-placeholder`
7. `ahflc emit-durable-store-import-provider-sdk-adapter-readiness`

## Side-Effect Boundary

Provider SDK adapter request consumer 只能生成 deterministic adapter identity：

```text
provider-sdk-adapter-request::<provider-local-host-execution-receipt-id>
```

Response placeholder consumer 只能生成 deterministic placeholder identity：

```text
provider-sdk-adapter-response-placeholder::<provider-sdk-adapter-request-id>
```

它不能：

1. 生成真实 SDK request payload
2. 调用 provider SDK
3. 打开网络连接
4. 读取 secret material 或 host environment
5. 写 host filesystem、object store 或 database
6. 把 endpoint、object path、database table、SDK payload 或 provider response 写入 stable artifact

## Labels

- `ahfl-v0.27`
- `v0.27-durable-store-import-provider-sdk-adapter-request-model`
- `v0.27-durable-store-import-provider-sdk-adapter-request-bootstrap`
- `v0.27-durable-store-import-provider-sdk-adapter-response-placeholder-model`
- `v0.27-durable-store-import-provider-sdk-adapter-readiness-model`
- `v0.27-durable-store-import-provider-sdk-adapter-emission`
- `v0.27-durable-store-import-provider-sdk-adapter-golden`
