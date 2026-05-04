# AHFL V0.32 Native Durable Store Provider SDK Payload Materialization Bootstrap

V0.32 冻结 SDK payload materialization boundary。它只为 fake provider 生成可审计的 payload plan、schema ref、digest 与 redaction/audit summary，不持久化 raw payload，也不调用真实 SDK。

## Artifact Chain

```text
ProviderLocalHostHarnessReview
-> ProviderSdkPayloadMaterializationPlan
-> ProviderSdkPayloadAuditSummary
```

formats：

```text
ahfl.durable-store-import-provider-sdk-payload-materialization-plan.v1
ahfl.durable-store-import-provider-sdk-payload-audit-summary.v1
```

fake schema：

```text
ahfl.fake-provider-sdk-payload-schema.v1
```

## Boundary

V0.32 固定：

1. `fake_provider_only=true`
2. schema ref 和 deterministic payload digest
3. redaction summary，声明 secret-free、credential-free、token-free

validation 拒绝 raw payload persistence、secret value、credential material、token value、network 与真实 provider SDK invocation。

## Entry Points

- `include/ahfl/durable_store_import/provider_sdk_payload.hpp`
- `src/durable_store_import/provider_sdk_payload.cpp`
- `emit-durable-store-import-provider-sdk-payload-plan`
- `emit-durable-store-import-provider-sdk-payload-audit`
