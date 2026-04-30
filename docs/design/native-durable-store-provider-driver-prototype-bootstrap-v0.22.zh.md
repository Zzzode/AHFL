# AHFL V0.22 Native Durable Store Provider Driver Prototype Bootstrap

本文定义 V0.22 provider driver binding 的最小实现边界。V0.22 不接入云 SDK、credential、真实对象存储、数据库 writer 或 recovery daemon；它在 V0.21 `ProviderWriteAttemptPreview` 之后冻结 secret-free provider driver binding contract。

## Artifact Chain

V0.22 的新增链路是：

```text
ProviderWriteAttemptPreview
-> ProviderDriverBindingPlan
-> ProviderDriverReadinessReview
```

`ProviderDriverBindingPlan` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-driver-binding-plan.v1
```

`ProviderDriverReadinessReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-driver-readiness-review.v1
```

## Driver Boundary

V0.22 把职责拆成四层：

1. provider-neutral shim：V0.21 生成 provider write attempt。
2. provider driver profile：声明 secret-free driver identity 与 capability。
3. provider driver binding：把 provider write intent 规划成 future driver operation descriptor。
4. production SDK writer：future extension，未来才处理 credential、endpoint、object path、database table 与 SDK payload。

## Secret-Free Contract

`ProviderDriverProfile` 只允许 secret-free reference：

1. `driver_profile_identity`
2. `provider_profile_ref`
3. `provider_namespace`
4. `secret_free_config_ref`
5. capability support booleans

artifact 禁止包含 credential、endpoint URI、endpoint secret、object path、database table、SDK payload schema、provider payload、host telemetry、真实 retry token 或 resume token。

## Binding Semantics

1. planned `ProviderWriteAttemptPreview` 且 driver profile 匹配、write translation capability 可用时，生成 bound plan 与 deterministic `provider-driver-operation::<provider-persistence-id>` descriptor。
2. source not planned、profile mismatch 或 unsupported driver capability 时，生成 non-bound noop plan，并保留 failure attribution。
3. `invokes_provider_sdk` 必须为 `false`。
4. `ProviderDriverReadinessReview` 只解释 driver binding 状态，不是 provider SDK ABI。

## Layering Rules

1. `ProviderDriverBindingPlan` 只能消费 V0.21 `ProviderWriteAttemptPreview` 与 driver profile。
2. `ProviderDriverReadinessReview` 只能消费 `ProviderDriverBindingPlan`。
3. recovery handoff text、CLI 文本、review summary、trace、host log 与私有脚本不能成为 provider driver binding state source。
4. V0.22 不修改 V0.21 provider write attempt 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_driver.hpp`
- `src/durable_store_import/provider_driver.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-driver-binding`
- `emit-durable-store-import-provider-driver-readiness`

回归入口：

- `ahfl-v0.22`
- `v0.22-durable-store-import-provider-driver-profile-model`
- `v0.22-durable-store-import-provider-driver-binding-model`
- `v0.22-durable-store-import-provider-driver-binding-bootstrap`
- `v0.22-durable-store-import-provider-driver-readiness-model`
- `v0.22-durable-store-import-provider-driver-emission`
- `v0.22-durable-store-import-provider-driver-golden`
