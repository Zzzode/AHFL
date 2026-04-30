# AHFL V0.23 Native Durable Store Provider Runtime Preflight Prototype Bootstrap

本文定义 V0.23 provider runtime preflight 的最小实现边界。V0.23 不接入 SDK、credential、secret manager、真实配置加载器、对象存储、数据库 writer 或 recovery daemon；它在 V0.22 `ProviderDriverBindingPlan` 之后冻结 runtime / SDK adapter preflight contract。

## Artifact Chain

V0.23 的新增链路是：

```text
ProviderDriverBindingPlan
-> ProviderRuntimePreflightPlan
-> ProviderRuntimeReadinessReview
```

`ProviderRuntimePreflightPlan` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-runtime-preflight-plan.v1
```

`ProviderRuntimeReadinessReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-runtime-readiness-review.v1
```

## Runtime Boundary

V0.23 把职责拆成四层：

1. provider driver binding：V0.22 生成 future driver operation descriptor。
2. provider runtime profile：声明 secret-free runtime identity、config policy ref 与 secret resolver policy ref。
3. provider runtime preflight：把 driver operation descriptor 规划成 future SDK invocation envelope identity。
4. production SDK adapter：future extension，未来才处理 config snapshot、secret resolution、endpoint、object path、database table 与 SDK payload。

## Secret-Free Contract

`ProviderRuntimeProfile` 只允许 secret-free reference：

1. `runtime_profile_identity`
2. `driver_profile_ref`
3. `provider_namespace`
4. `secret_free_runtime_config_ref`
5. `secret_resolver_policy_ref`
6. `config_snapshot_policy_ref`
7. capability support booleans

artifact 禁止包含 credential、secret value、secret manager endpoint URI、provider endpoint URI、object path、database table、SDK payload schema、SDK request payload、provider response、host telemetry、真实 retry token 或 resume token。

## Preflight Semantics

1. bound `ProviderDriverBindingPlan` 且 runtime profile 匹配、runtime preflight capability 可用时，生成 ready plan 与 deterministic `provider-sdk-invocation-envelope::<provider-driver-operation-id>` identity。
2. driver binding not ready、runtime profile mismatch 或 unsupported runtime capability 时，生成 blocked noop plan，并保留 failure attribution。
3. `loads_runtime_config`、`resolves_secret_handles`、`invokes_provider_sdk` 必须为 `false`。
4. `ProviderRuntimeReadinessReview` 只解释 runtime preflight 状态，不是 provider SDK adapter ABI。

## Layering Rules

1. `ProviderRuntimePreflightPlan` 只能消费 V0.22 `ProviderDriverBindingPlan` 与 runtime profile。
2. `ProviderRuntimeReadinessReview` 只能消费 `ProviderRuntimePreflightPlan`。
3. driver readiness text、CLI 文本、review summary、trace、host log、secret manager response、provider payload 与私有脚本不能成为 provider runtime preflight state source。
4. V0.23 不修改 V0.22 provider driver binding 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_runtime.hpp`
- `src/durable_store_import/provider_runtime.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-runtime-preflight`
- `emit-durable-store-import-provider-runtime-readiness`

回归入口：

- `ahfl-v0.23`
- `v0.23-durable-store-import-provider-runtime-profile-model`
- `v0.23-durable-store-import-provider-runtime-preflight-model`
- `v0.23-durable-store-import-provider-runtime-preflight-bootstrap`
- `v0.23-durable-store-import-provider-runtime-readiness-model`
- `v0.23-durable-store-import-provider-runtime-emission`
- `v0.23-durable-store-import-provider-runtime-golden`
