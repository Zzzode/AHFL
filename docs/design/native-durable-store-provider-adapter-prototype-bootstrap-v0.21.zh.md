# AHFL V0.21 Native Durable Store Provider Adapter Prototype Bootstrap

本文定义 V0.21 provider-neutral durable store adapter boundary 的最小实现边界。V0.21 不接入云 SDK、credential、真实对象存储、数据库 writer 或 recovery daemon；它在 V0.20 `AdapterExecutionReceipt` 之后冻结 secret-free provider planning contract。

## Artifact Chain

V0.21 的新增链路是：

```text
AdapterExecutionReceipt
-> ProviderWriteAttemptPreview
-> ProviderRecoveryHandoffPreview
```

`ProviderWriteAttemptPreview` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-provider-write-attempt-preview.v1
```

`ProviderRecoveryHandoffPreview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-provider-recovery-handoff-preview.v1
```

## Provider-Neutral Boundary

V0.21 把职责拆成四层：

1. V0.20 local fake store：仍然是 deterministic regression substitute。
2. provider adapter shim：生成 secret-free provider config、capability matrix、write intent、retry / resume placeholder。
3. provider driver：future extension，未来才把 provider-neutral intent 转成 provider-specific call。
4. production writer：future extension，未来才处理真实 object path、database key、credential lifecycle、retry token 与 resume token。

## Secret-Free Contract

`ProviderAdapterConfig` 只允许 provider-neutral reference：

1. `config_identity`
2. `provider_profile_ref`
3. `provider_namespace`
4. `credential_free = true`

artifact 禁止包含 credential、endpoint secret、真实 object path、database key、tenant secret、provider payload、host telemetry、真实 retry token 或 resume token。

## Planning Semantics

1. accepted + persisted `AdapterExecutionReceipt` 且 provider write capability 可用时，生成 mutating `ProviderPersistReceipt` intent 与 deterministic `provider-persistence::<package>::<session>::accepted` id。
2. blocked、deferred、rejected 或 provider write capability 不可用时，生成 non-mutating noop intent，并保留 failure attribution。
3. retry / resume 目前只生成 placeholder identity，不代表真实 recovery token。
4. `ProviderRecoveryHandoffPreview` 只解释 handoff 状态，不是 recovery daemon ABI。

## Layering Rules

1. `ProviderWriteAttemptPreview` 只能消费 V0.20 `AdapterExecutionReceipt`、provider config 与 capability matrix。
2. `ProviderRecoveryHandoffPreview` 只能消费 `ProviderWriteAttemptPreview`。
3. `RecoveryCommandPreview`、CLI 文本、review summary、trace、host log 与私有脚本不能成为 provider adapter state source。
4. V0.21 不修改 V0.20 `AdapterExecutionReceipt` 稳定字段。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/provider_adapter.hpp`
- `src/durable_store_import/provider_adapter.cpp`

CLI / backend 入口：

- `emit-durable-store-import-provider-write-attempt`
- `emit-durable-store-import-provider-recovery-handoff`

回归入口：

- `ahfl-v0.21`
- `v0.21-durable-store-import-provider-adapter-config-model`
- `v0.21-durable-store-import-provider-write-attempt-model`
- `v0.21-durable-store-import-provider-write-attempt-bootstrap`
- `v0.21-durable-store-import-provider-recovery-handoff-model`
- `v0.21-durable-store-import-provider-adapter-emission`
- `v0.21-durable-store-import-provider-adapter-golden`
