# AHFL V0.39 Durable Store Provider Observability Audit Compatibility

V0.39 的兼容边界是 provider execution audit event、telemetry summary 与 operator review event。它消费 V0.38 failure taxonomy report，输出 secret-free 的审计与观测 artifact。

## Stable Formats

- `ahfl.durable-store-import-provider-execution-audit-event.v1`
- `ahfl.durable-store-import-provider-telemetry-summary.v1`
- `ahfl.durable-store-import-provider-operator-review-event.v1`
- `ahfl.provider-audit-redaction.v1`

## Compatibility Rules

1. audit event 必须保留 failure taxonomy report identity。
2. audit event 必须携带 redaction policy version。
3. audit event 与 telemetry summary 必须 `secret_free=true`。
4. audit event 与 telemetry summary 必须 `raw_telemetry_persisted=false`。
5. taxonomy `none` 必须映射为 audit `success`。
6. network 与 throttling failure 必须映射为 `retry_pending`。
7. conflict 必须映射为 `recovery_pending`。
8. schema mismatch、provider internal 与 unknown failure 必须映射为 `failure`。
9. blocked taxonomy 必须映射为 `blocked`。
10. operator review event 是 reviewer-facing projection，不替代 machine-facing audit event 或 telemetry summary。
