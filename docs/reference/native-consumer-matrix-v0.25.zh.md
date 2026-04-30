# AHFL V0.25 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider Host Execution Consumer | V0.24 `ProviderSdkRequestEnvelopePlan`、secret-free / network-free host execution policy | `emit-durable-store-import-provider-host-execution`、`durable_store_import::ProviderHostExecutionPlan`、`validate_provider_host_execution_plan(...)` | SDK request envelope identity、source envelope status、source host handoff descriptor identity、host execution policy identity、host handoff policy ref、provider namespace、host capability support、execution status、host execution descriptor identity、host receipt placeholder identity、failure attribution | credential、secret value、host command、host environment、endpoint URI、object path、database table、SDK request / response payload、reviewer text |
| Provider Host Execution Readiness Consumer | `ProviderHostExecutionPlan` | `emit-durable-store-import-provider-host-execution-readiness`、`durable_store_import::ProviderHostExecutionReadinessReview`、`validate_provider_host_execution_readiness_review(...)` | host execution identity、execution status、operation kind、host execution descriptor identity、host receipt placeholder identity、side-effect booleans、failure attribution、next action、next-step recommendation | real host execution ABI、provider request / response payload、config loader output、secret resolver output、host env、filesystem output、operator payload、host telemetry |

## Artifact Order

V0.25 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-adapter-execution`
2. `ahflc emit-durable-store-import-provider-write-attempt`
3. `ahflc emit-durable-store-import-provider-driver-binding`
4. `ahflc emit-durable-store-import-provider-runtime-preflight`
5. `ahflc emit-durable-store-import-provider-sdk-envelope`
6. `ahflc emit-durable-store-import-provider-host-execution`
7. `ahflc emit-durable-store-import-provider-host-execution-readiness`

## Side-Effect Boundary

Provider host execution consumer 只能规划 future host execution descriptor 与 dry-run host receipt placeholder identity：

```text
provider-host-execution-descriptor::<provider-sdk-host-handoff-id>
provider-host-receipt-placeholder::<provider-sdk-host-handoff-id>
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

- `ahfl-v0.25`
- `v0.25-durable-store-import-provider-host-policy-model`
- `v0.25-durable-store-import-provider-host-execution-model`
- `v0.25-durable-store-import-provider-host-execution-bootstrap`
- `v0.25-durable-store-import-provider-host-execution-readiness-model`
- `v0.25-durable-store-import-provider-host-execution-emission`
- `v0.25-durable-store-import-provider-host-execution-golden`
