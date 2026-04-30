# AHFL V0.25 Durable Store Provider Host Execution Prototype Compatibility

本文定义 V0.25 provider host execution prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

Provider host execution policy format：

```text
ahfl.durable-store-import-provider-host-execution-policy.v1
```

Provider host execution plan format：

```text
ahfl.durable-store-import-provider-host-execution-plan.v1
```

Provider host execution readiness review format：

```text
ahfl.durable-store-import-provider-host-execution-readiness-review.v1
```

## Stable Fields

`ProviderHostExecutionPolicy` 的稳定字段：

- `host_execution_policy_identity`
- `host_handoff_policy_ref`
- `provider_namespace`
- `execution_profile_ref`
- `sandbox_policy_ref`
- `timeout_policy_ref`
- `credential_free`
- `network_free`
- capability support booleans
- forbidden nullable fields 必须保持 absent / null

`ProviderHostExecutionPlan` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- `durable_store_import_provider_sdk_request_envelope_identity`
- `source_host_handoff_descriptor_identity`
- `source_envelope_status`
- `host_execution_policy`
- `durable_store_import_provider_host_execution_identity`
- `operation_kind`
- `execution_status`
- `provider_host_execution_descriptor_identity`
- `provider_host_receipt_placeholder_identity`
- `starts_host_process`
- `reads_host_environment`
- `opens_network_connection`
- `materializes_sdk_request_payload`
- `invokes_provider_sdk`
- `writes_host_filesystem`
- `failure_attribution`

`ProviderHostExecutionReadinessReview` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- SDK request envelope / host execution identities
- execution status
- operation kind
- host execution descriptor identity
- host receipt placeholder identity
- side-effect booleans
- failure attribution
- reviewer summary fields
- next action

## Forbidden Fields

V0.25 artifacts must not contain:

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
3. 添加新的 host capability kind，只要现有 validation 和 golden 不变

Breaking changes:

1. 修改任一 `*.v1` format version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic host execution descriptor identity 或 host receipt placeholder identity
4. 让 blocked path 生成 host execution descriptor identity 或 host receipt placeholder identity
5. 让 artifact 启动 host process、读取 host environment、打开网络连接、生成 SDK request payload、调用 provider SDK 或写 host filesystem
6. 从 readiness review、CLI text、host log、secret manager response、SDK payload、host command、host environment、network endpoint 或 private script 推导 host execution state

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.25
ctest --preset test-dev --output-on-failure -L v0.25-durable-store-import-provider-host-execution-golden
```
