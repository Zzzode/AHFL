# AHFL V0.36 Durable Store Provider Write Commit Receipt Compatibility

V0.36 的兼容边界是 provider write commit receipt 与 commit review。

## Stable Formats

- `ahfl.durable-store-import-provider-write-commit-receipt.v1`
- `ahfl.durable-store-import-provider-write-commit-review.v1`

## Compatibility Rules

1. commit receipt 必须保留 provider commit reference、provider commit digest 与 idempotency key。
2. receipt 必须 `secret_free=true`。
3. receipt 必须 `raw_provider_payload_persisted=false`。
4. non-committed 状态必须携带 failure attribution。
5. duplicate、partial、failed、blocked 不得被消费者当作 committed。
6. commit review 是 reviewer-facing projection，不替代 machine-facing receipt。
