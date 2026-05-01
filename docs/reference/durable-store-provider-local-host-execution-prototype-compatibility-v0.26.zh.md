# AHFL V0.26 Durable Store Provider Local Host Execution Prototype Compatibility

本文定义 V0.26 provider local host execution receipt prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

Provider local host execution receipt format：

```text
ahfl.durable-store-import-provider-local-host-execution-receipt.v1
```

Provider local host execution receipt review format：

```text
ahfl.durable-store-import-provider-local-host-execution-receipt-review.v1
```

## Stable Fields

`ProviderLocalHostExecutionReceipt` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK request envelope / host execution identities
- source host execution status
- source host execution descriptor identity
- source host receipt placeholder identity
- local host execution receipt identity
- operation kind
- execution status
- provider local host execution receipt identity
- side-effect booleans
- failure attribution

`ProviderLocalHostExecutionReceiptReview` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK request envelope / host execution / local host receipt identities
- execution status
- operation kind
- provider local host execution receipt identity
- side-effect booleans
- failure attribution
- reviewer summary fields
- next action

## Forbidden Fields

V0.26 artifacts must not contain:

1. credential reference、credential value、secret value
2. host command、host environment secret
3. provider endpoint URI、network endpoint URI
4. object path、database table、provider request payload、provider response payload
5. SDK request payload、SDK response payload
6. real retry token、real resume token、host telemetry、operator payload

`starts_host_process`、`reads_host_environment`、`opens_network_connection`、`materializes_sdk_request_payload`、`invokes_provider_sdk`、`writes_host_filesystem` must remain `false`.

## Compatibility Rules

Compatible changes:

1. 添加只读 reviewer summary 文本
2. 添加新的 blocked failure attribution kind，但不改变现有 kind 的语义
3. 添加新的 receipt next action，只要现有 validation 和 golden 不变

Breaking changes:

1. 修改任一 `*.v1` format version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic local host receipt identity
4. 让 blocked path 生成 local host receipt identity
5. 让 artifact 启动 host process、读取 host environment、打开网络连接、生成 SDK request payload、调用 provider SDK 或写 host filesystem
6. 从 readiness review、CLI text、host log、secret manager response、SDK payload、host command、host environment、network endpoint 或 private script 推导 local host execution receipt state

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.26
ctest --preset test-dev --output-on-failure -L v0.26-durable-store-import-provider-local-host-execution-receipt-golden
```
