# AHFL V0.28 Durable Store Provider SDK Adapter Interface Compatibility

本文定义 V0.28 provider SDK adapter interface prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

```text
ahfl.durable-store-import-provider-sdk-adapter-interface-plan.v1
ahfl.durable-store-import-provider-sdk-adapter-interface-review.v1
ahfl.provider-sdk-adapter-interface.v1
ahfl.provider-sdk-adapter-error-taxonomy.v1
```

## Stable Fields

`ProviderSdkAdapterInterfacePlan` 的稳定字段：

- source V0.27 request plan format
- workflow / session / run / input fixture identity
- source request status and request identity
- interface artifact identity
- operation kind and interface status
- provider registry identity
- adapter descriptor identity
- provider key、adapter name、adapter version、interface ABI version
- capability descriptor identity and capability descriptor
- error taxonomy version and normalized error kind
- placeholder result flag
- side-effect booleans
- forbidden endpoint / credential / payload optionals
- failure attribution

`ProviderSdkAdapterInterfaceReview` 的稳定字段：

- source interface plan format
- workflow / session / run / input fixture identity
- request and interface identities
- registry / adapter / capability identities
- capability support status
- error taxonomy version and normalized error kind
- placeholder result flag
- side-effect booleans
- failure attribution
- summary fields and next action

## Forbidden Fields

V0.28 artifacts must not contain credential values, secret values, provider endpoint URI, SDK request payload, SDK response payload, provider response payload, network endpoint, host environment secret, filesystem output, object path, database table, real retry token, or real resume token.

`materializes_sdk_request_payload`、`invokes_provider_sdk`、`opens_network_connection`、`reads_secret_material`、`reads_host_environment`、`writes_host_filesystem` must remain `false`.

## Compatibility Rules

Breaking changes:

1. 修改任一 stable format / ABI / taxonomy version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic registry、adapter descriptor、capability descriptor 或 interface identity
4. 让 artifact 调用 SDK、打开网络、读取 secret、读取 host environment 或写 filesystem
5. 把 endpoint、credential、SDK payload 或 provider response 写入 stable artifact

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.28
ctest --preset test-dev --output-on-failure -L v0.28-durable-store-import-provider-sdk-adapter-interface-golden
```
