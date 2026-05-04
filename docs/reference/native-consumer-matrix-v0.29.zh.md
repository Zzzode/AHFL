# AHFL V0.29 Native Consumer Matrix

| Consumer | Input | Entry Points | Stable Fields Consumed | Must Not Consume |
| --- | --- | --- | --- | --- |
| Provider Config Load Consumer | V0.28 `ProviderSdkAdapterInterfacePlan` | `emit-durable-store-import-provider-config-load`、`durable_store_import::ProviderConfigLoadPlan`、`validate_provider_config_load_plan(...)` | interface identity、registry identity、adapter descriptor identity、provider key、profile descriptor、config schema version、profile identity、snapshot placeholder identity、side-effect booleans、failure attribution | secret value、credential material、endpoint URI、remote config response、SDK payload、network response、filesystem output、reviewer text |
| Provider Config Snapshot Consumer | `ProviderConfigLoadPlan` | `emit-durable-store-import-provider-config-snapshot`、`durable_store_import::ProviderConfigSnapshotPlaceholder`、`validate_provider_config_snapshot_placeholder(...)` | config load identity、profile identity、snapshot placeholder identity、schema version、snapshot status、failure attribution | secret value、credential material、endpoint、remote config response、host env、SDK payload |
| Provider Config Readiness Consumer | `ProviderConfigSnapshotPlaceholder` | `emit-durable-store-import-provider-config-readiness`、`durable_store_import::ProviderConfigReadinessReview`、`validate_provider_config_readiness_review(...)` | snapshot status、operation kind、profile identity、schema version、next action、next-step recommendation | machine state source、secret value、credential material、network response、host log、CLI text |

## Artifact Order

V0.29 的 native durable store import chain 末端顺序是：

1. `ahflc emit-durable-store-import-provider-sdk-adapter-interface`
2. `ahflc emit-durable-store-import-provider-sdk-adapter-interface-review`
3. `ahflc emit-durable-store-import-provider-config-load`
4. `ahflc emit-durable-store-import-provider-config-snapshot`
5. `ahflc emit-durable-store-import-provider-config-readiness`

## Labels

- `ahfl-v0.29`
- `v0.29-durable-store-import-provider-config-load-model`
- `v0.29-durable-store-import-provider-config-snapshot-model`
- `v0.29-durable-store-import-provider-config-readiness-model`
- `v0.29-durable-store-import-provider-config-emission`
- `v0.29-durable-store-import-provider-config-golden`
