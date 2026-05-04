# AHFL V0.33 Durable Store Provider SDK Mock Adapter Compatibility

V0.33 的兼容边界是 provider SDK mock adapter。

## Stable Formats

- `ahfl.durable-store-import-provider-sdk-mock-adapter-contract.v1`
- `ahfl.durable-store-import-provider-sdk-mock-adapter-execution-result.v1`
- `ahfl.durable-store-import-provider-sdk-mock-adapter-readiness.v1`

## Compatibility Rules

1. contract 只接受 fake SDK payload audit。
2. execution 不打开 network、不读取 secret、不调用真实 provider SDK。
3. normalized result 覆盖 accepted、provider_failure、timeout、throttled、conflict、schema_mismatch。
4. readiness success path 可作为真实 adapter parity test 起点。
