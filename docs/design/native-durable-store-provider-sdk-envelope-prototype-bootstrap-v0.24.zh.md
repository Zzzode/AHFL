# AHFL V0.24 Native Durable Store Provider SDK Envelope Prototype Bootstrap

本文定义 V0.24 provider SDK request envelope 的最小实现边界。V0.24 不接入 SDK、credential、secret manager、真实配置加载器、host process manager、网络、对象存储、数据库 writer 或 recovery daemon；它在 V0.23 `ProviderRuntimePreflightPlan` 之后冻结 SDK request envelope / host handoff readiness contract。

## Artifact Chain

V0.24 的新增链路是：

```text
ProviderRuntimePreflightPlan
-> ProviderSdkRequestEnvelopePlan
-> ProviderSdkHandoffReadinessReview
```

`ProviderSdkRequestEnvelopePlan` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-sdk-request-envelope-plan.v1
```

`ProviderSdkHandoffReadinessReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-sdk-handoff-readiness-review.v1
```

## SDK Envelope Boundary

V0.24 把职责拆成四层：

1. provider runtime preflight：V0.23 生成 future SDK invocation envelope identity。
2. provider SDK envelope policy：声明 secret-free request schema ref、host handoff policy ref 与 envelope planning capability。
3. provider SDK request envelope plan：把 SDK invocation envelope identity 规划成 request envelope identity 与 host handoff descriptor identity。
4. production host execution / SDK adapter：future extension，未来才处理 host command、config snapshot、secret resolution、endpoint、object path、database table 与 SDK payload。

## Secret-Free Contract

`ProviderSdkEnvelopePolicy` 只允许 secret-free reference：

1. `sdk_envelope_policy_identity`
2. `runtime_profile_ref`
3. `provider_namespace`
4. `secret_free_request_schema_ref`
5. `host_handoff_policy_ref`
6. `credential_free`
7. capability support booleans

artifact 禁止包含 credential、secret value、provider endpoint URI、object path、database table、SDK request payload、SDK response payload、host command、network endpoint URI、provider response、host telemetry、真实 retry token 或 resume token。

## Envelope Semantics

1. ready `ProviderRuntimePreflightPlan` 且 envelope policy 匹配、secret-free request envelope capability 与 host handoff descriptor capability 可用时，生成 ready plan、deterministic `provider-sdk-request-envelope::<sdk-invocation-envelope-id>` identity 与 `provider-sdk-host-handoff::<sdk-invocation-envelope-id>` identity。
2. runtime preflight not ready、envelope policy mismatch 或 unsupported envelope capability 时，生成 blocked noop plan，并保留 failure attribution。
3. `materializes_sdk_request_payload`、`starts_host_process`、`opens_network_connection`、`invokes_provider_sdk` 必须为 `false`。
4. `ProviderSdkHandoffReadinessReview` 只解释 SDK request envelope 状态，不是 host execution ABI。

## Layering Rules

1. `ProviderSdkRequestEnvelopePlan` 只能消费 V0.23 `ProviderRuntimePreflightPlan` 与 envelope policy。
2. `ProviderSdkHandoffReadinessReview` 只能消费 `ProviderSdkRequestEnvelopePlan`。
3. runtime readiness text、CLI 文本、review summary、trace、host log、secret manager response、provider payload、host command、network endpoint 与私有脚本不能成为 provider SDK envelope state source。
4. V0.24 不修改 V0.23 provider runtime preflight 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_sdk.hpp`
- `src/durable_store_import/provider_sdk.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-sdk-envelope`
- `emit-durable-store-import-provider-sdk-handoff-readiness`

回归入口：

- `ahfl-v0.24`
- `v0.24-durable-store-import-provider-sdk-policy-model`
- `v0.24-durable-store-import-provider-sdk-envelope-model`
- `v0.24-durable-store-import-provider-sdk-envelope-bootstrap`
- `v0.24-durable-store-import-provider-sdk-handoff-readiness-model`
- `v0.24-durable-store-import-provider-sdk-emission`
- `v0.24-durable-store-import-provider-sdk-golden`
