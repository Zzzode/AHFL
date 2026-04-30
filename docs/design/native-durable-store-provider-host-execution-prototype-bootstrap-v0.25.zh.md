# AHFL V0.25 Native Durable Store Provider Host Execution Prototype Bootstrap

本文定义 V0.25 provider host execution planning 的最小实现边界。V0.25 不接入 SDK、credential、secret manager、真实配置加载器、host process manager、网络、对象存储、数据库 writer 或 recovery daemon；它在 V0.24 `ProviderSdkRequestEnvelopePlan` 之后冻结 host execution planning / readiness contract。

## Artifact Chain

V0.25 的新增链路是：

```text
ProviderSdkRequestEnvelopePlan
-> ProviderHostExecutionPlan
-> ProviderHostExecutionReadinessReview
```

`ProviderHostExecutionPlan` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-host-execution-plan.v1
```

`ProviderHostExecutionReadinessReview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-host-execution-readiness-review.v1
```

## Host Execution Boundary

V0.25 把职责拆成四层：

1. provider SDK request envelope：V0.24 生成 future host handoff descriptor identity。
2. provider host execution policy：声明 execution profile ref、sandbox policy ref、timeout policy ref 与 host planning capability。
3. provider host execution plan：把 host handoff descriptor identity 规划成 host execution descriptor identity 与 dry-run host receipt placeholder identity。
4. production host execution harness / SDK adapter：future extension，未来才处理 host command、host environment、config snapshot、secret resolution、endpoint、object path、database table 与 SDK payload。

## Secret-Free / Network-Free Contract

`ProviderHostExecutionPolicy` 只允许 secret-free、network-free reference：

1. `host_execution_policy_identity`
2. `host_handoff_policy_ref`
3. `provider_namespace`
4. `execution_profile_ref`
5. `sandbox_policy_ref`
6. `timeout_policy_ref`
7. `credential_free`
8. `network_free`
9. capability support booleans

artifact 禁止包含 credential、secret value、host command、host environment secret、provider endpoint URI、network endpoint URI、object path、database table、SDK request payload、SDK response payload、provider response、host telemetry、真实 retry token 或 resume token。

## Execution Planning Semantics

1. ready `ProviderSdkRequestEnvelopePlan` 且 host execution policy 匹配、local host execution descriptor capability 与 dry-run host receipt placeholder capability 可用时，生成 ready plan、deterministic `provider-host-execution-descriptor::<host-handoff-descriptor-id>` identity 与 `provider-host-receipt-placeholder::<host-handoff-descriptor-id>` identity。
2. SDK handoff not ready、host policy mismatch 或 unsupported host capability 时，生成 blocked noop plan，并保留 failure attribution。
3. `starts_host_process`、`reads_host_environment`、`opens_network_connection`、`materializes_sdk_request_payload`、`invokes_provider_sdk`、`writes_host_filesystem` 必须为 `false`。
4. `ProviderHostExecutionReadinessReview` 只解释 host execution planning 状态，不是 host execution harness ABI。

## Layering Rules

1. `ProviderHostExecutionPlan` 只能消费 V0.24 `ProviderSdkRequestEnvelopePlan` 与 host execution policy。
2. `ProviderHostExecutionReadinessReview` 只能消费 `ProviderHostExecutionPlan`。
3. SDK handoff readiness text、CLI 文本、review summary、trace、host log、secret manager response、provider payload、host command、host environment、network endpoint 与私有脚本不能成为 provider host execution state source。
4. V0.25 不修改 V0.24 provider SDK request envelope 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_host_execution.hpp`
- `src/durable_store_import/provider_host_execution.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-host-execution`
- `emit-durable-store-import-provider-host-execution-readiness`

回归入口：

- `ahfl-v0.25`
- `v0.25-durable-store-import-provider-host-policy-model`
- `v0.25-durable-store-import-provider-host-execution-model`
- `v0.25-durable-store-import-provider-host-execution-bootstrap`
- `v0.25-durable-store-import-provider-host-execution-readiness-model`
- `v0.25-durable-store-import-provider-host-execution-emission`
- `v0.25-durable-store-import-provider-host-execution-golden`
