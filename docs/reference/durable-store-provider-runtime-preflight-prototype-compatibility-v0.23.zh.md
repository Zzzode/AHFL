# AHFL V0.23 Durable Store Provider Runtime Preflight Prototype Compatibility

本文定义 V0.23 provider runtime preflight prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

Runtime profile format：

```text
ahfl.durable-store-import-provider-runtime-profile.v1
```

Runtime preflight plan format：

```text
ahfl.durable-store-import-provider-runtime-preflight-plan.v1
```

Runtime readiness review format：

```text
ahfl.durable-store-import-provider-runtime-readiness-review.v1
```

## Stable Fields

`ProviderRuntimeProfile` 的稳定字段：

- `runtime_profile_identity`
- `driver_profile_ref`
- `provider_namespace`
- `secret_free_runtime_config_ref`
- `secret_resolver_policy_ref`
- `config_snapshot_policy_ref`
- capability support booleans
- forbidden nullable fields 必须保持 absent / null

`ProviderRuntimePreflightPlan` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- `durable_store_import_provider_driver_binding_identity`
- `provider_persistence_id`
- `source_binding_status`
- `source_operation_descriptor_identity`
- `runtime_profile`
- `durable_store_import_provider_runtime_preflight_identity`
- `operation_kind`
- `preflight_status`
- `sdk_invocation_envelope_identity`
- `loads_runtime_config`
- `resolves_secret_handles`
- `invokes_provider_sdk`
- `failure_attribution`

`ProviderRuntimeReadinessReview` 的稳定字段：

- source format versions
- workflow / session / run / input fixture identity
- binding / preflight identities
- preflight status
- operation kind
- SDK invocation envelope identity
- side-effect booleans
- failure attribution
- reviewer summary fields
- next action

## Forbidden Fields

V0.23 artifacts must not contain:

1. credential reference、credential value、secret value
2. secret manager endpoint URI、provider endpoint URI
3. object path、database table、provider request payload、provider response payload
4. SDK payload schema、SDK request payload、SDK response payload
5. real retry token、real resume token、host telemetry、operator payload

`loads_runtime_config`、`resolves_secret_handles`、`invokes_provider_sdk` must remain `false`.

## Compatibility Rules

Compatible changes:

1. 添加只读 reviewer summary 文本
2. 添加新的 blocked failure attribution kind，但不改变现有 kind 的语义
3. 添加新的 runtime capability kind，只要现有 validation 和 golden 不变

Breaking changes:

1. 修改任一 `*.v1` format version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic envelope identity
4. 让 blocked path 生成 SDK invocation envelope identity
5. 让 artifact 加载 runtime config、解析 secret handle 或调用 provider SDK
6. 从 readiness review、CLI text、host log、secret manager response 或 private script 推导 runtime preflight state

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.23
ctest --preset test-dev --output-on-failure -L v0.23-durable-store-import-provider-runtime-golden
```
