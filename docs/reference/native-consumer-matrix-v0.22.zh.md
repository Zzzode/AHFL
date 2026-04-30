# AHFL V0.22 Native Consumer Matrix

本文列出 V0.22 provider driver binding 相关 native consumer 边界。

| Consumer | 输入 | 入口 | 可依赖字段 | 不可依赖字段 |
| --- | --- | --- | --- | --- |
| Provider Driver Binding Consumer | V0.21 `ProviderWriteAttemptPreview`、secret-free driver profile | `emit-durable-store-import-provider-driver-binding`、`durable_store_import::ProviderDriverBindingPlan`、`validate_provider_driver_binding_plan(...)` | provider write attempt identity、driver profile identity、provider profile ref、provider namespace、driver capability support、binding status、operation descriptor identity、failure attribution | credential、endpoint URI、object path、database table、SDK payload、reviewer text |
| Provider Driver Readiness Consumer | `ProviderDriverBindingPlan` | `emit-durable-store-import-provider-driver-readiness`、`durable_store_import::ProviderDriverReadinessReview`、`validate_provider_driver_readiness_review(...)` | binding identity、binding status、operation kind、operation descriptor identity、failure attribution、next action、next-step recommendation | provider SDK ABI、provider request / response payload、真实 retry token、真实 resume token、operator payload、host telemetry |

## Dependency Order

V0.22 的 provider driver consumer 顺序是：

1. `durable_store_import::build_provider_write_attempt_preview(...)`
2. `durable_store_import::validate_provider_write_attempt_preview(...)`
3. `durable_store_import::build_default_provider_driver_profile(...)`
4. `durable_store_import::build_provider_driver_binding_plan(...)`
5. `durable_store_import::validate_provider_driver_binding_plan(...)`
6. `durable_store_import::build_provider_driver_readiness_review(...)`
7. `durable_store_import::validate_provider_driver_readiness_review(...)`
8. `ahflc emit-durable-store-import-provider-driver-binding`
9. `ahflc emit-durable-store-import-provider-driver-readiness`

## Bound Path

bound path 表示 V0.21 provider write attempt 已经 planned。V0.22 provider driver consumer 可以依赖：

1. `binding_status = bound`
2. `operation_kind = translate_provider_persist_receipt`
3. deterministic operation descriptor identity
4. `invokes_provider_sdk = false`

该 path 仍不代表真实 provider SDK call 已发生。

## Non-Bound Paths

source-not-planned、profile-mismatch 与 capability-gap path 必须保持 non-bound：

1. 不生成 operation descriptor identity
2. 不声明真实 SDK invocation
3. 保留 driver binding failure attribution
4. readiness review 只给 reviewer next-step，不定义 provider SDK ABI

## CI Labels

V0.22 consumer regression 使用：

- `ahfl-v0.22`
- `v0.22-durable-store-import-provider-driver-profile-model`
- `v0.22-durable-store-import-provider-driver-binding-model`
- `v0.22-durable-store-import-provider-driver-binding-bootstrap`
- `v0.22-durable-store-import-provider-driver-readiness-model`
- `v0.22-durable-store-import-provider-driver-emission`
- `v0.22-durable-store-import-provider-driver-golden`
