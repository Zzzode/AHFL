# AHFL V0.20 Native Consumer Matrix

本文列出 V0.20 durable store adapter execution 相关 native consumer 边界。

| Consumer | 输入 | 入口 | 可依赖字段 | 不可依赖字段 |
| --- | --- | --- | --- | --- |
| Durable Store Adapter Execution Consumer | V0.19 `Response` | `emit-durable-store-import-adapter-execution`、`durable_store_import::AdapterExecutionReceipt`、`validate_adapter_execution_receipt(...)` | source response identity、response status / outcome、fake store contract version、mutation intent、mutation status、persistence id、failure attribution | provider credential、真实 object path、database key、host telemetry、review summary text |
| Durable Store Recovery Preview Consumer | `AdapterExecutionReceipt` | `emit-durable-store-import-recovery-preview`、`durable_store_import::RecoveryCommandPreview`、`validate_recovery_command_preview(...)` | adapter execution identity、mutation status、persistence id、failure attribution、next action、next-step recommendation | recovery daemon ABI、operator payload、provider retry token、resume token、真实 store mutation command |

## Dependency Order

V0.20 的 durable store adapter execution consumer 顺序是：

1. `durable_store_import::build_persistence_response(...)`
2. `durable_store_import::validate_persistence_response(...)`
3. `durable_store_import::build_adapter_execution_receipt(...)`
4. `durable_store_import::validate_adapter_execution_receipt(...)`
5. `durable_store_import::build_recovery_command_preview(...)`
6. `durable_store_import::validate_recovery_command_preview(...)`
7. `ahflc emit-durable-store-import-adapter-execution`
8. `ahflc emit-durable-store-import-recovery-preview`

## Accepted Path

accepted path 表示 V0.19 `Response` 已经 adapter-response-consumable。V0.20 fake store consumer 可以依赖：

1. mutating `PersistReceipt`
2. `mutation_status = persisted`
3. deterministic fake `persistence_id`

该 path 仍不代表真实 provider write 已发生。

## Non-Mutating Paths

blocked、deferred、rejected path 必须保持 non-mutating：

1. 不生成 persistence id
2. 不声明真实 store mutation
3. 保留 failure attribution
4. recovery preview 只给 reviewer next-step，不定义 recovery daemon ABI

## CI Labels

V0.20 consumer regression 使用：

- `ahfl-v0.20`
- `v0.20-durable-store-import-adapter-execution-model`
- `v0.20-durable-store-import-adapter-execution-bootstrap`
- `v0.20-durable-store-import-recovery-preview-model`
- `v0.20-durable-store-import-adapter-execution-emission`
- `v0.20-durable-store-import-adapter-execution-golden`
