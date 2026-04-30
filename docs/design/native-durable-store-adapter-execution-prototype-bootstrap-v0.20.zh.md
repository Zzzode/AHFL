# AHFL V0.20 Native Durable Store Adapter Execution Prototype Bootstrap

本文定义 V0.20 production-adjacent durable store adapter execution 的最小实现边界。V0.20 不引入真实数据库、object storage、provider SDK、credential、operator console 或 recovery daemon，而是在 V0.19 `Response` 之后冻结一个 deterministic local fake durable store contract。

## Artifact Chain

V0.20 的新增链路是：

```text
Response
-> AdapterExecutionReceipt
-> RecoveryCommandPreview
```

`AdapterExecutionReceipt` 是 machine-facing artifact，格式为：

```text
ahfl.durable-store-import-adapter-execution.v1
```

`RecoveryCommandPreview` 是 reviewer-facing projection，格式为：

```text
ahfl.durable-store-import-recovery-command-preview.v1
```

## Local Fake Durable Store Boundary

V0.20 使用固定的 fake store contract：

```text
ahfl.local-fake-durable-store.v1
```

该 contract 只做 deterministic regression：

1. `accepted` response 生成 `PersistReceipt` mutation intent、`persisted` mutation status 与 deterministic `fake-persistence::<package>::<session>::accepted` persistence id。
2. `blocked`、`deferred`、`rejected` response 生成 non-mutating intent、`not_mutated` status、无 persistence id，并保留 failure attribution。
3. artifact 不包含 provider credential、真实 store URI、object path、database key、host telemetry 或 provider payload。

## Layering Rules

1. `AdapterExecutionReceipt` 只能消费 V0.19 `Response`。
2. `RecoveryCommandPreview` 只能消费 `AdapterExecutionReceipt`。
3. reviewer summary、CLI text、trace、AST、host log 与私有脚本不能成为 adapter execution 的第一事实来源。
4. fake store 可以被 future provider adapter 替换，但不能修改 V0.19 `Response` 稳定字段。

## Failure Model

V0.20 区分以下失败归因：

1. source response blocked：源 response 仍缺少 adapter capability 或人工 review artifact。
2. source response deferred：源 response 需要保留 partial workflow state。
3. source response rejected：源 workflow 已失败，不能产生 store mutation。

这些归因只说明 adapter execution 没有形成 store mutation，不等价于真实 recovery daemon ABI。

## Implementation Entry Points

代码入口：

- `include/ahfl/durable_store_import/adapter_execution.hpp`
- `src/durable_store_import/adapter_execution.cpp`
- `include/ahfl/durable_store_import/recovery_preview.hpp`
- `src/durable_store_import/recovery_preview.cpp`

CLI / backend 入口：

- `emit-durable-store-import-adapter-execution`
- `emit-durable-store-import-recovery-preview`

回归入口：

- `ahfl-v0.20`
- `v0.20-durable-store-import-adapter-execution-model`
- `v0.20-durable-store-import-adapter-execution-bootstrap`
- `v0.20-durable-store-import-recovery-preview-model`
- `v0.20-durable-store-import-adapter-execution-emission`
- `v0.20-durable-store-import-adapter-execution-golden`
