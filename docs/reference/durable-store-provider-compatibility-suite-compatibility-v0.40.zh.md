# AHFL V0.40 Durable Store Provider Compatibility Suite Compatibility

V0.40 的兼容边界是 provider compatibility test manifest、fixture matrix 与 compatibility report。

## Stable Formats

- `ahfl.durable-store-import-provider-compatibility-test-manifest.v1`
- `ahfl.durable-store-import-provider-fixture-matrix.v1`
- `ahfl.durable-store-import-provider-compatibility-report.v1`

## Compatibility Rules

1. manifest 必须保留 operator review event source identity。
2. matrix 必须保留 manifest identity。
3. report 必须保留 fixture matrix identity 与 telemetry summary identity。
4. manifest 与 matrix 必须覆盖 mock adapter 与 local filesystem alpha provider。
5. 默认 suite 不得要求外部服务。
6. report status 必须显式区分 `passed`、`failed` 与 `blocked`。
7. report 是 machine-facing compatibility source，不应从 CLI 文本或 private script 推断。
