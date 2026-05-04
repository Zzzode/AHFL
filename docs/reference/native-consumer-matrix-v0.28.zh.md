# AHFL V0.28 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider SDK Adapter Interface Consumer | V0.27 `ProviderSdkAdapterRequestPlan` | `emit-durable-store-import-provider-sdk-adapter-interface`、`durable_store_import::ProviderSdkAdapterInterfacePlan`、`validate_provider_sdk_adapter_interface_plan(...)` | request identity、interface identity、registry identity、adapter descriptor identity、provider key、adapter version、ABI version、capability descriptor、error taxonomy version、normalized error kind、side-effect booleans、failure attribution | credential、secret value、endpoint URI、SDK payload、provider response payload、network response、filesystem output、reviewer text |
| Provider SDK Adapter Interface Review Consumer | `ProviderSdkAdapterInterfacePlan` | `emit-durable-store-import-provider-sdk-adapter-interface-review`、`durable_store_import::ProviderSdkAdapterInterfaceReview`、`validate_provider_sdk_adapter_interface_review(...)` | interface status、operation kind、registry / adapter / capability identities、capability support、normalized error、next action、next-step recommendation | machine state source、SDK payload、SDK call result、network response、secret resolver output、host log、CLI text |

## Artifact Order

V0.28 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-provider-sdk-adapter-request`
2. `ahflc emit-durable-store-import-provider-sdk-adapter-response-placeholder`
3. `ahflc emit-durable-store-import-provider-sdk-adapter-readiness`
4. `ahflc emit-durable-store-import-provider-sdk-adapter-interface`
5. `ahflc emit-durable-store-import-provider-sdk-adapter-interface-review`

## Labels

- `ahfl-v0.28`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-model`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-review-model`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-emission`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-golden`
