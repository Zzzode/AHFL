# AHFL V0.30 Native Durable Store Provider Secret Resolver Bootstrap

V0.30 在 provider config snapshot 之后冻结 secret handle resolver boundary。它只表达未来要解析的 secret handle，不解析 secret manager，不读取 credential，不生成 token，也不打开 provider network。

## Artifact Chain

```text
ProviderConfigSnapshotPlaceholder
-> ProviderSecretResolverRequestPlan
-> ProviderSecretResolverResponsePlaceholder
-> ProviderSecretPolicyReview
```

machine-facing formats：

```text
ahfl.durable-store-import-provider-secret-resolver-request-plan.v1
ahfl.durable-store-import-provider-secret-resolver-response-placeholder.v1
```

reviewer-facing format：

```text
ahfl.durable-store-import-provider-secret-policy-review.v1
```

secret handle schema：

```text
ahfl.provider-secret-handle.v1
```

## Boundary

V0.30 固定三件事：

1. secret handle identity、provider key、profile key、purpose。
2. resolver request / response placeholder 的 ready 与 blocked 状态。
3. credential lifecycle placeholder：ready path 为 `placeholder_pending_resolution`。

所有 validation 都拒绝 secret value、credential material、token value、secret manager response、network、host env、filesystem write 与 secret manager invocation。

## Entry Points

- `include/ahfl/durable_store_import/provider_secret.hpp`
- `src/durable_store_import/provider_secret.cpp`
- `emit-durable-store-import-provider-secret-resolver-request`
- `emit-durable-store-import-provider-secret-resolver-response`
- `emit-durable-store-import-provider-secret-policy-review`
