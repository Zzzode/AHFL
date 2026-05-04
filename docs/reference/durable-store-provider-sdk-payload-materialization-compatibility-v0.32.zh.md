# AHFL V0.32 Durable Store Provider SDK Payload Materialization Compatibility

V0.32 的兼容边界是 fake SDK payload materialization。

## Stable Formats

- `ahfl.durable-store-import-provider-sdk-payload-materialization-plan.v1`
- `ahfl.durable-store-import-provider-sdk-payload-audit-summary.v1`
- `ahfl.fake-provider-sdk-payload-schema.v1`

## Compatibility Rules

1. payload plan 必须 fake-provider-only。
2. raw payload 不能持久化。
3. audit summary 必须声明 secret-free、credential-free、token-free。
4. 只有 ready audit 可以进入 mock adapter。
