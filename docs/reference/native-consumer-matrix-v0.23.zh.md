# AHFL V0.23 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider Runtime Preflight Consumer | V0.22 `ProviderDriverBindingPlan`、secret-free runtime profile | `emit-durable-store-import-provider-runtime-preflight`、`durable_store_import::ProviderRuntimePreflightPlan`、`validate_provider_runtime_preflight_plan(...)` | driver binding identity、source binding status、operation descriptor identity、provider persistence id、runtime profile identity、driver profile ref、provider namespace、runtime capability support、preflight status、SDK invocation envelope identity、failure attribution | credential、secret value、endpoint URI、object path、database table、SDK payload、config snapshot、secret manager response、reviewer text |
| Provider Runtime Readiness Consumer | `ProviderRuntimePreflightPlan` | `emit-durable-store-import-provider-runtime-readiness`、`durable_store_import::ProviderRuntimeReadinessReview`、`validate_provider_runtime_readiness_review(...)` | preflight identity、preflight status、operation kind、SDK invocation envelope identity、side-effect booleans、failure attribution、next action、next-step recommendation | real SDK ABI、provider request / response payload、config loader output、secret resolver output、operator payload、host telemetry |

## Artifact Order

V0.23 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-adapter-execution`
2. `ahflc emit-durable-store-import-provider-write-attempt`
3. `ahflc emit-durable-store-import-provider-driver-binding`
4. `ahflc emit-durable-store-import-provider-runtime-preflight`
5. `ahflc emit-durable-store-import-provider-runtime-readiness`

## Side-Effect Boundary

Provider runtime preflight consumer 只能规划 future SDK invocation envelope identity：

```text
provider-sdk-invocation-envelope::<provider-driver-operation-id>
```

它不能：

1. 读取真实 runtime config
2. 解析 secret handle
3. 调用 provider SDK
4. 写 object store 或 database
5. 把 provider payload 存入 stable artifact

## Labels

- `ahfl-v0.23`
- `v0.23-durable-store-import-provider-runtime-profile-model`
- `v0.23-durable-store-import-provider-runtime-preflight-model`
- `v0.23-durable-store-import-provider-runtime-preflight-bootstrap`
- `v0.23-durable-store-import-provider-runtime-readiness-model`
- `v0.23-durable-store-import-provider-runtime-emission`
- `v0.23-durable-store-import-provider-runtime-golden`
