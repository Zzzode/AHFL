# AHFL V0.26 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider Local Host Execution Receipt Consumer | V0.25 `ProviderHostExecutionPlan` | `emit-durable-store-import-provider-local-host-execution-receipt`、`durable_store_import::ProviderLocalHostExecutionReceipt`、`validate_provider_local_host_execution_receipt(...)` | host execution identity、source host execution status、host execution descriptor identity、host receipt placeholder identity、local host receipt identity、operation kind、execution status、side-effect booleans、failure attribution | credential、secret value、host command、host environment、endpoint URI、object path、database table、SDK request / response payload、reviewer text |
| Provider Local Host Execution Receipt Review Consumer | `ProviderLocalHostExecutionReceipt` | `emit-durable-store-import-provider-local-host-execution-receipt-review`、`durable_store_import::ProviderLocalHostExecutionReceiptReview`、`validate_provider_local_host_execution_receipt_review(...)` | local host receipt identity、execution status、operation kind、side-effect booleans、failure attribution、next action、next-step recommendation | real host execution ABI、provider request / response payload、config loader output、secret resolver output、host env、filesystem output、operator payload、host telemetry |

## Artifact Order

V0.26 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-provider-sdk-envelope`
2. `ahflc emit-durable-store-import-provider-host-execution`
3. `ahflc emit-durable-store-import-provider-host-execution-readiness`
4. `ahflc emit-durable-store-import-provider-local-host-execution-receipt`
5. `ahflc emit-durable-store-import-provider-local-host-execution-receipt-review`

## Side-Effect Boundary

Provider local host execution receipt consumer 只能生成 simulated receipt identity：

```text
provider-local-host-execution-receipt::<provider-host-execution-descriptor-id>
```

它不能：

1. 启动 host process
2. 读取 host environment
3. 打开网络连接
4. 生成真实 SDK request payload
5. 调用 provider SDK
6. 写 host filesystem、object store 或 database
7. 把 provider payload 存入 stable artifact

## Labels

- `ahfl-v0.26`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-model`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-bootstrap`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-review-model`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-emission`
- `v0.26-durable-store-import-provider-local-host-execution-receipt-golden`
