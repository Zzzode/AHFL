# AHFL V0.24 Durable Store Provider SDK Envelope Prototype Compatibility

本文定义 V0.24 provider SDK request envelope prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

Provider SDK envelope policy format：

```text
ahfl.durable-store-import-provider-sdk-envelope-policy.v1
```

Provider SDK request envelope plan format：

```text
ahfl.durable-store-import-provider-sdk-request-envelope-plan.v1
```

Provider SDK handoff readiness review format：

```text
ahfl.durable-store-import-provider-sdk-handoff-readiness-review.v1
```

## Stable Fields

`ProviderSdkEnvelopePolicy` 的稳定字段：

- `sdk_envelope_policy_identity`
- `runtime_profile_ref`
- `provider_namespace`
- `secret_free_request_schema_ref`
- `host_handoff_policy_ref`
- `credential_free`
- capability support booleans
- forbidden nullable fields 必须保持 absent / null

`ProviderSdkRequestEnvelopePlan` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- `durable_store_import_provider_runtime_preflight_identity`
- `source_sdk_invocation_envelope_identity`
- `source_preflight_status`
- `envelope_policy`
- `durable_store_import_provider_sdk_request_envelope_identity`
- `operation_kind`
- `envelope_status`
- `provider_sdk_request_envelope_identity`
- `host_handoff_descriptor_identity`
- `materializes_sdk_request_payload`
- `starts_host_process`
- `opens_network_connection`
- `invokes_provider_sdk`
- `failure_attribution`

`ProviderSdkHandoffReadinessReview` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- runtime preflight / SDK request envelope identities
- envelope status
- operation kind
- request envelope identity
- host handoff descriptor identity
- side-effect booleans
- failure attribution
- reviewer summary fields
- next action

## Forbidden Fields

V0.24 artifacts must not contain:

1. credential reference、credential value、secret value
2. provider endpoint URI、network endpoint URI
3. object path、database table、provider request payload、provider response payload
4. SDK request payload、SDK response payload
5. host command、real retry token、real resume token、host telemetry、operator payload

`materializes_sdk_request_payload`、`starts_host_process`、`opens_network_connection`、`invokes_provider_sdk` must remain `false`.

## Compatibility Rules

Compatible changes:

1. 添加只读 reviewer summary 文本
2. 添加新的 blocked failure attribution kind，但不改变现有 kind 的语义
3. 添加新的 envelope capability kind，只要现有 validation 和 golden 不变

Breaking changes:

1. 修改任一 `*.v1` format version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic request envelope identity 或 host handoff descriptor identity
4. 让 blocked path 生成 request envelope identity 或 host handoff descriptor identity
5. 让 artifact 生成 SDK request payload、启动 host process、打开网络连接或调用 provider SDK
6. 从 readiness review、CLI text、host log、secret manager response、SDK payload、host command、network endpoint 或 private script 推导 SDK envelope state

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.24
ctest --preset test-dev --output-on-failure -L v0.24-durable-store-import-provider-sdk-golden
```
