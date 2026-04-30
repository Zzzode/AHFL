# AHFL V0.24 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider SDK Request Envelope Consumer | V0.23 `ProviderRuntimePreflightPlan`、secret-free envelope policy | `emit-durable-store-import-provider-sdk-envelope`、`durable_store_import::ProviderSdkRequestEnvelopePlan`、`validate_provider_sdk_request_envelope_plan(...)` | runtime preflight identity、source preflight status、source SDK invocation envelope identity、envelope policy identity、runtime profile ref、provider namespace、envelope capability support、envelope status、request envelope identity、host handoff descriptor identity、failure attribution | credential、secret value、endpoint URI、object path、database table、SDK request / response payload、host command、network endpoint、reviewer text |
| Provider SDK Handoff Readiness Consumer | `ProviderSdkRequestEnvelopePlan` | `emit-durable-store-import-provider-sdk-handoff-readiness`、`durable_store_import::ProviderSdkHandoffReadinessReview`、`validate_provider_sdk_handoff_readiness_review(...)` | SDK request envelope identity、envelope status、operation kind、request envelope identity、host handoff descriptor identity、side-effect booleans、failure attribution、next action、next-step recommendation | real host execution ABI、provider request / response payload、config loader output、secret resolver output、operator payload、host telemetry |

## Artifact Order

V0.24 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-adapter-execution`
2. `ahflc emit-durable-store-import-provider-write-attempt`
3. `ahflc emit-durable-store-import-provider-driver-binding`
4. `ahflc emit-durable-store-import-provider-runtime-preflight`
5. `ahflc emit-durable-store-import-provider-sdk-envelope`
6. `ahflc emit-durable-store-import-provider-sdk-handoff-readiness`

## Side-Effect Boundary

Provider SDK request envelope consumer 只能规划 future request envelope 与 host handoff descriptor identity：

```text
provider-sdk-request-envelope::<provider-sdk-invocation-envelope-id>
provider-sdk-host-handoff::<provider-sdk-invocation-envelope-id>
```

它不能：

1. 生成真实 SDK request payload
2. 启动 host process
3. 打开网络连接
4. 调用 provider SDK
5. 写 object store 或 database
6. 把 provider payload 存入 stable artifact

## Labels

- `ahfl-v0.24`
- `v0.24-durable-store-import-provider-sdk-policy-model`
- `v0.24-durable-store-import-provider-sdk-envelope-model`
- `v0.24-durable-store-import-provider-sdk-envelope-bootstrap`
- `v0.24-durable-store-import-provider-sdk-handoff-readiness-model`
- `v0.24-durable-store-import-provider-sdk-emission`
- `v0.24-durable-store-import-provider-sdk-golden`
