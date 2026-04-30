# AHFL V0.20 Durable Store Adapter Execution Prototype Compatibility

本文冻结 V0.20 `AdapterExecutionReceipt` 与 `RecoveryCommandPreview` 的 compatibility contract。

## Stable Machine Artifact

`AdapterExecutionReceipt` 的稳定格式：

```text
ahfl.durable-store-import-adapter-execution.v1
```

稳定字段：

1. `format_version`
2. source response / request / receipt / decision format version chain
3. `source_package_identity`
4. workflow identity：`workflow_canonical_name`、`session_id`、`run_id`、`input_fixture`
5. source durable artifact identities
6. `durable_store_import_adapter_execution_identity`
7. `planned_durable_identity`
8. `response_status`、`response_outcome`、`response_boundary_kind`
9. `store_kind = local_fake_durable_store`
10. `local_fake_store_contract_version = ahfl.local-fake-durable-store.v1`
11. `mutation_intent`
12. `mutation_status`
13. optional `persistence_id`
14. optional `failure_attribution`

## Stable Reviewer Projection

`RecoveryCommandPreview` 的稳定格式：

```text
ahfl.durable-store-import-recovery-command-preview.v1
```

稳定字段：

1. source adapter execution / response format version chain
2. workflow identity
3. source response identity
4. source adapter execution identity
5. response status / outcome
6. mutation status
7. optional persistence id
8. optional failure attribution
9. execution summary
10. recovery boundary summary
11. next action
12. next-step recommendation

## Compatibility Rules

1. `accepted` 必须有 mutating `PersistReceipt` intent、`persisted` mutation status、persistence id，且没有 failure attribution。
2. `blocked`、`deferred`、`rejected` 必须是 non-mutating intent、`not_mutated` mutation status、无 persistence id，且有 failure attribution。
3. fake store persistence id 必须 deterministic，不能依赖 wall clock、host path、process id、provider endpoint 或 credential。
4. breaking change 需要 bump `AdapterExecutionReceipt` 或 `RecoveryCommandPreview` format version，并同步 golden、consumer matrix、contributor guide 和 CI 标签。

## Forbidden Fields

V0.20 stable artifact 禁止包含：

1. provider credential、secret、token
2. 真实 store URI、object path、database key
3. tenant / region / endpoint config
4. host telemetry、SIEM event、operator payload
5. recovery daemon ABI、retry token、resume token
6. 从 review summary 或 CLI 文本反推得到的 durable state

## Regression Contract

最小回归：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.20
ctest --preset test-dev --output-on-failure -L v0.20-durable-store-import-adapter-execution-golden
```

golden 文件位于 `tests/durable_store_import/`，覆盖 single-file accepted、project rejected 与 workspace partial accepted。
