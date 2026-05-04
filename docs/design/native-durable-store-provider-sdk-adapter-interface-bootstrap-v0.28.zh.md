# AHFL V0.28 Native Durable Store Provider SDK Adapter Interface Bootstrap

本文定义 V0.28 provider SDK adapter interface 的最小实现边界。V0.28 不接入真实 SDK、credential、secret manager、provider endpoint、网络、filesystem、object store、database writer 或 dynamic plugin loader；它在 V0.27 `ProviderSdkAdapterRequestPlan` 之后冻结 adapter descriptor、registry identity、capability descriptor 与 error taxonomy skeleton。

## Artifact Chain

V0.28 的新增链路是：

```text
ProviderSdkAdapterRequestPlan
-> ProviderSdkAdapterInterfacePlan
-> ProviderSdkAdapterInterfaceReview
```

machine-facing interface plan format：

```text
ahfl.durable-store-import-provider-sdk-adapter-interface-plan.v1
```

reviewer-facing interface review format：

```text
ahfl.durable-store-import-provider-sdk-adapter-interface-review.v1
```

interface ABI version：

```text
ahfl.provider-sdk-adapter-interface.v1
```

error taxonomy version：

```text
ahfl.provider-sdk-adapter-error-taxonomy.v1
```

## Boundary

V0.28 固定三件事：

1. provider registry identity 与 adapter descriptor identity。
2. `durable_store_import.write` capability descriptor。
3. normalized error kind skeleton。

ready path 只返回 placeholder-ready interface plan，不调用 SDK、不生成 SDK payload、不读取 secret、不打开网络、不写 filesystem。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_sdk_interface.hpp`
- `src/durable_store_import/provider_sdk_interface.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-sdk-adapter-interface`
- `emit-durable-store-import-provider-sdk-adapter-interface-review`

回归入口：

- `ahfl-v0.28`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-model`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-review-model`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-emission`
- `v0.28-durable-store-import-provider-sdk-adapter-interface-golden`
