# AHFL V0.21 Native Consumer Matrix

本文列出 V0.21 provider-neutral durable store adapter 相关 native consumer 边界。

| Consumer | 输入 | 入口 | 可依赖字段 | 不可依赖字段 |
| --- | --- | --- | --- | --- |
| Provider Write Attempt Consumer | V0.20 `AdapterExecutionReceipt`、secret-free provider config、capability matrix | `emit-durable-store-import-provider-write-attempt`、`durable_store_import::ProviderWriteAttemptPreview`、`validate_provider_write_attempt_preview(...)` | source adapter execution identity、provider config identity、provider namespace、capability support、provider write intent、planning status、provider persistence id、failure attribution | credential、endpoint secret、真实 object path、database key、provider SDK result、reviewer text |
| Provider Recovery Handoff Consumer | `ProviderWriteAttemptPreview` | `emit-durable-store-import-provider-recovery-handoff`、`durable_store_import::ProviderRecoveryHandoffPreview`、`validate_provider_recovery_handoff_preview(...)` | provider write attempt identity、planning status、provider persistence id、retry / resume placeholder、failure attribution、next action、next-step recommendation | recovery daemon ABI、真实 retry token、真实 resume token、operator payload、host telemetry |

## Dependency Order

V0.21 的 provider-neutral adapter consumer 顺序是：

1. `durable_store_import::build_adapter_execution_receipt(...)`
2. `durable_store_import::validate_adapter_execution_receipt(...)`
3. `durable_store_import::build_default_provider_adapter_config(...)`
4. `durable_store_import::build_default_provider_capability_matrix(...)`
5. `durable_store_import::build_provider_write_attempt_preview(...)`
6. `durable_store_import::validate_provider_write_attempt_preview(...)`
7. `durable_store_import::build_provider_recovery_handoff_preview(...)`
8. `durable_store_import::validate_provider_recovery_handoff_preview(...)`
9. `ahflc emit-durable-store-import-provider-write-attempt`
10. `ahflc emit-durable-store-import-provider-recovery-handoff`

## Accepted Path

accepted path 表示 V0.20 `AdapterExecutionReceipt` 已经 persisted。V0.21 provider-neutral consumer 可以依赖：

1. mutating `ProviderPersistReceipt`
2. `planning_status = planned`
3. deterministic provider-neutral `provider_persistence_id`
4. retry / resume placeholder identity

该 path 仍不代表真实 provider write 已发生。

## Non-Mutating Paths

blocked、deferred、rejected 与 provider capability gap path 必须保持 non-mutating：

1. 不生成 provider persistence id
2. 不声明真实 provider mutation
3. 保留 provider planning failure attribution
4. recovery handoff 只给 reviewer next-step，不定义 recovery daemon ABI

## CI Labels

V0.21 consumer regression 使用：

- `ahfl-v0.21`
- `v0.21-durable-store-import-provider-adapter-config-model`
- `v0.21-durable-store-import-provider-write-attempt-model`
- `v0.21-durable-store-import-provider-write-attempt-bootstrap`
- `v0.21-durable-store-import-provider-recovery-handoff-model`
- `v0.21-durable-store-import-provider-adapter-emission`
- `v0.21-durable-store-import-provider-adapter-golden`
