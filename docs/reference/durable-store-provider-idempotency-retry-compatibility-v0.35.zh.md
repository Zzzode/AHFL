# AHFL V0.35 Durable Store Provider Idempotency Retry Compatibility

V0.35 的兼容边界是 provider write retry decision。

## Stable Formats

- `ahfl.durable-store-import-provider-write-retry-decision.v1`
- `ahfl.provider-write-retry-token.v1`

## Compatibility Rules

1. idempotency namespace 固定为 `ahfl.durable-store-import.provider-write.v1`。
2. accepted result 不允许 retry。
3. timeout 与 throttled result 可 retry。
4. conflict result 必须进入 duplicate review。
5. provider failure 与 schema mismatch 不可 retry。
6. decision 只表达 retry contract，不执行隐式重试。
