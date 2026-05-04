# AHFL V0.41 Native Durable Store Provider Registry Selection Bootstrap

V0.41 建立 multi-provider registry 与 provider selection artifact 边界。它消费 V0.40 compatibility report，输出 registry、selection plan 与 capability negotiation review。

## Artifact Chain

```text
ProviderCompatibilityReport
-> ProviderRegistry
-> ProviderSelectionPlan
-> ProviderCapabilityNegotiationReview
```

formats：

```text
ahfl.durable-store-import-provider-registry.v1
ahfl.durable-store-import-provider-selection-plan.v1
ahfl.durable-store-import-provider-capability-negotiation-review.v1
```

## Selection Boundary

1. registry 默认注册 `local-filesystem-alpha` 与 `sdk-mock-adapter`。
2. primary provider 是 local filesystem alpha。
3. fallback provider 是 SDK mock adapter。
4. fallback policy 必须是 audited-fallback-only。
5. blocked selection 必须要求 operator review。
6. 未审计 fallback 不允许进入 artifact。

## Entry Points

- `include/ahfl/durable_store_import/provider_registry.hpp`
- `src/durable_store_import/provider_registry.cpp`
- `emit-durable-store-import-provider-registry`
- `emit-durable-store-import-provider-selection-plan`
- `emit-durable-store-import-provider-capability-negotiation-review`
