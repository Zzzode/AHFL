# AHFL V0.27 Durable Store Provider SDK Adapter Prototype Compatibility

本文定义 V0.27 provider SDK adapter artifact prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

Provider SDK adapter request plan format：

```text
ahfl.durable-store-import-provider-sdk-adapter-request-plan.v1
```

Provider SDK adapter response placeholder format：

```text
ahfl.durable-store-import-provider-sdk-adapter-response-placeholder.v1
```

Provider SDK adapter readiness review format：

```text
ahfl.durable-store-import-provider-sdk-adapter-readiness-review.v1
```

## Stable Fields

`ProviderSdkAdapterRequestPlan` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK request envelope / host execution / local host receipt identities
- source local host execution status
- source provider local host execution receipt identity
- SDK adapter request artifact identity
- operation kind
- request status
- provider SDK adapter request identity
- provider SDK adapter response placeholder identity
- side-effect booleans
- forbidden payload / endpoint / storage fields as empty optionals
- failure attribution

`ProviderSdkAdapterResponsePlaceholder` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK adapter request artifact identity
- response placeholder artifact identity
- operation kind
- response status
- provider SDK adapter request identity
- provider SDK adapter response placeholder identity
- side-effect booleans
- forbidden payload / endpoint / storage fields as empty optionals
- failure attribution

`ProviderSdkAdapterReadinessReview` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK adapter request / response placeholder identities
- response status
- operation kind
- provider SDK adapter request / response placeholder identities
- side-effect booleans
- failure attribution
- reviewer summary fields
- next action

## Forbidden Fields

V0.27 artifacts must not contain:

1. credential reference、credential value、secret value
2. provider endpoint URI、network endpoint URI
3. object path、database table、provider request payload、provider response payload
4. SDK request payload、SDK response payload
5. host command、host environment secret、host telemetry、operator payload
6. real retry token、real resume token、filesystem output

`materializes_sdk_request_payload`、`invokes_provider_sdk`、`opens_network_connection`、`reads_secret_material`、`reads_host_environment`、`writes_host_filesystem` must remain `false`.

## Compatibility Rules

Compatible changes:

1. 添加只读 reviewer summary 文本
2. 添加新的 blocked failure attribution kind，但不改变现有 kind 的语义
3. 添加新的 readiness next action，只要现有 validation 和 golden 不变

Breaking changes:

1. 修改任一 `*.v1` format version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic SDK adapter request identity 或 response placeholder identity
4. 让 blocked path 生成 provider SDK adapter request / response placeholder identity
5. 让 artifact 生成 SDK request payload、调用 provider SDK、打开网络连接、读取 secret material、读取 host environment 或写 host filesystem
6. 把 endpoint、object path、database table、SDK payload 或 provider response payload 写入 stable artifact
7. 从 readiness review、CLI text、host log、secret manager response、SDK payload、host command、host environment、network endpoint 或 private script 推导 SDK adapter state

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.27
ctest --preset test-dev --output-on-failure -L v0.27-durable-store-import-provider-sdk-adapter-golden
```
