# AHFL V0.29 Durable Store Provider Config Loader Compatibility

本文定义 V0.29 provider config loader prototype 的兼容性边界。本文是 reference，不替代语言 spec。

## Stable Formats

```text
ahfl.durable-store-import-provider-config-load-plan.v1
ahfl.durable-store-import-provider-config-snapshot-placeholder.v1
ahfl.durable-store-import-provider-config-readiness-review.v1
ahfl.provider-config-schema.v1
```

## Stable Fields

`ProviderConfigLoadPlan` 的稳定字段：

- source V0.28 interface plan format
- workflow / session / run / input fixture identity
- source adapter interface status
- registry / adapter descriptor / provider key
- config load artifact identity
- operation kind and config load status
- config profile identity
- config snapshot placeholder identity
- profile descriptor and config schema version
- side-effect booleans
- forbidden secret / credential / endpoint / remote config / SDK payload optionals
- failure attribution

`ProviderConfigSnapshotPlaceholder` 的稳定字段：

- source config load plan format
- workflow / session / run / input fixture identity
- config load identity and source status
- config snapshot artifact identity
- operation kind and snapshot status
- profile identity and snapshot placeholder identity
- config schema version
- side-effect booleans
- failure attribution

## Forbidden Fields

V0.29 artifacts must not contain secret value, credential material, provider endpoint URI, remote config URI response, SDK request payload, SDK response payload, provider response payload, network endpoint, host environment secret, filesystem output, object path, database table, real retry token, or real resume token.

All side-effect booleans for secret read, secret value materialization, credential materialization, network, host env, filesystem, SDK payload, and SDK call must remain `false`.

## Compatibility Rules

Breaking changes:

1. 修改任一 stable format 或 schema version 字符串
2. 删除或重命名 stable field
3. 让 ready path 生成非 deterministic profile、snapshot placeholder 或 config load identity
4. 让 artifact 读取 secret、生成 credential、打开网络、读取 host environment 或写 filesystem
5. 把 endpoint、secret、credential、remote config response 或 SDK payload 写入 stable artifact

## Regression Labels

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.29
ctest --preset test-dev --output-on-failure -L v0.29-durable-store-import-provider-config-golden
```
