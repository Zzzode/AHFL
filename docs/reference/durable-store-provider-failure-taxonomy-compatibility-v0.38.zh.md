# AHFL V0.38 Durable Store Provider Failure Taxonomy Compatibility

V0.38 的兼容边界是 provider failure taxonomy report 与 taxonomy review。它把 provider SDK mock adapter execution result 与 V0.37 recovery plan 归一化到稳定 failure taxonomy v1。

## Stable Formats

- `ahfl.durable-store-import-provider-failure-taxonomy-report.v1`
- `ahfl.durable-store-import-provider-failure-taxonomy-review.v1`

## Compatibility Rules

1. report 必须保留 mock adapter execution result identity 与 recovery plan identity。
2. failure taxonomy v1 必须稳定表达 failure kind、category、retryability、operator action 与 security sensitivity。
3. `accepted` 必须映射为 `none` failure kind 和 `not_applicable` retryability。
4. `timeout` 与 `throttled` 必须保持 retryable 语义。
5. `conflict` 必须映射为 duplicate review required，不能被消费者当作普通失败。
6. `schema_mismatch` 与 provider internal failure 必须保持 non-retryable 或 escalation 语义。
7. report 必须 `secret_bearing_error_persisted=false` 且 `raw_provider_error_persisted=false`。
8. taxonomy review 是 reviewer-facing projection，不替代 machine-facing taxonomy report。

## Reserved Taxonomy Values

`authentication` 与 `authorization` 已在 taxonomy v1 中预留，用于未来真实 provider adapter 映射。当前 mock adapter matrix 不强制自然产出这两类错误。
