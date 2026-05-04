# AHFL V0.29 Native Durable Store Provider Config Loader Bootstrap

本文定义 V0.29 provider config loader boundary 的最小实现。V0.29 不读取 secret value、credential material、remote config service、provider endpoint、host environment、network、filesystem、SDK payload 或 provider response；它在 V0.28 `ProviderSdkAdapterInterfacePlan` 后冻结 config load plan、profile descriptor、snapshot placeholder 与 readiness review。

## Artifact Chain

V0.29 的新增链路是：

```text
ProviderSdkAdapterInterfacePlan
-> ProviderConfigLoadPlan
-> ProviderConfigSnapshotPlaceholder
-> ProviderConfigReadinessReview
```

machine-facing formats：

```text
ahfl.durable-store-import-provider-config-load-plan.v1
ahfl.durable-store-import-provider-config-snapshot-placeholder.v1
```

reviewer-facing format：

```text
ahfl.durable-store-import-provider-config-readiness-review.v1
```

config schema version：

```text
ahfl.provider-config-schema.v1
```

## Boundary

V0.29 固定三件事：

1. provider config profile identity 和 profile descriptor。
2. provider config snapshot placeholder identity。
3. missing / incompatible / unsupported config 的 failure attribution skeleton。

ready path 只生成 placeholder config snapshot，不连接 remote config、不读取 secret、不打开网络、不生成 endpoint credential。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_config.hpp`
- `src/durable_store_import/provider_config.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-config-load`
- `emit-durable-store-import-provider-config-snapshot`
- `emit-durable-store-import-provider-config-readiness`

回归入口：

- `ahfl-v0.29`
- `v0.29-durable-store-import-provider-config-load-model`
- `v0.29-durable-store-import-provider-config-snapshot-model`
- `v0.29-durable-store-import-provider-config-readiness-model`
- `v0.29-durable-store-import-provider-config-emission`
- `v0.29-durable-store-import-provider-config-golden`
