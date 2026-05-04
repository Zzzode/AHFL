# AHFL V0.34 Durable Store Provider Local Filesystem Alpha Compatibility

V0.34 的兼容边界是 opt-in local filesystem alpha provider。

## Stable Formats

- `ahfl.durable-store-import-provider-local-filesystem-alpha-plan.v1`
- `ahfl.durable-store-import-provider-local-filesystem-alpha-result.v1`
- `ahfl.durable-store-import-provider-local-filesystem-alpha-readiness.v1`

## Compatibility Rules

1. `provider_key` 固定为 `local-filesystem-alpha`。
2. CLI 默认必须保持 `opt_in_enabled=false`，不得写入本地文件。
3. alpha provider 不访问网络、不读取 secret、不调用云 SDK。
4. `dry-run-only` 是默认兼容输出，不代表真实 provider commit。
5. 真实写入只能通过显式 opt-in 测试路径验证。
6. fake / mock adapter 默认路径仍是生产前回归基线。
