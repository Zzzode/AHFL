# AHFL V0.39 Native Durable Store Provider Observability Audit Bootstrap

V0.39 建立 provider execution 的 structured audit event、telemetry summary 与 operator review event。它消费 V0.38 taxonomy report，不接入任何生产 metrics backend 或 SIEM。

## Artifact Chain

```text
ProviderFailureTaxonomyReport
-> ProviderExecutionAuditEvent
-> ProviderTelemetrySummary
-> ProviderOperatorReviewEvent
```

formats：

```text
ahfl.durable-store-import-provider-execution-audit-event.v1
ahfl.durable-store-import-provider-telemetry-summary.v1
ahfl.durable-store-import-provider-operator-review-event.v1
ahfl.provider-audit-redaction.v1
```

## Outcome Mapping

1. taxonomy `none` -> `success`
2. `network` / `throttling` -> `retry_pending`
3. `conflict` -> `recovery_pending`
4. `schema_mismatch` / `provider_internal` / `unknown` -> `failure`
5. `blocked` -> `blocked`

## Safety Boundary

1. audit event 与 telemetry summary 必须 secret-free。
2. 不输出 host-level raw telemetry。
3. 不绑定 metrics backend、SIEM vendor 或 alert sink。

## Entry Points

- `include/ahfl/durable_store_import/provider_audit.hpp`
- `src/durable_store_import/provider_audit.cpp`
- `emit-durable-store-import-provider-execution-audit-event`
- `emit-durable-store-import-provider-telemetry-summary`
- `emit-durable-store-import-provider-operator-review-event`
