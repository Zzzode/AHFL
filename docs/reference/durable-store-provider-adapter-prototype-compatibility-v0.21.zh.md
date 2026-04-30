# AHFL V0.21 Durable Store Provider Adapter Prototype Compatibility

本文冻结 V0.21 `ProviderWriteAttemptPreview` 与 `ProviderRecoveryHandoffPreview` 的 compatibility contract。

## Stable Machine Artifacts

Provider adapter config 格式：

```text
ahfl.durable-store-import-provider-adapter-config.v1
```

Provider capability matrix 格式：

```text
ahfl.durable-store-import-provider-capability-matrix.v1
```

Provider write attempt preview 格式：

```text
ahfl.durable-store-import-provider-write-attempt-preview.v1
```

稳定字段：

1. source adapter execution / response format version chain
2. workflow identity：`workflow_canonical_name`、`session_id`、`run_id`、`input_fixture`
3. source response identity、source adapter execution identity、provider write attempt identity
4. response status / outcome 与 adapter mutation status
5. optional source persistence id
6. secret-free provider adapter config
7. provider capability matrix
8. provider write intent
9. planning status
10. retry / resume placeholder
11. optional provider planning failure attribution

## Stable Reviewer Projection

Provider recovery handoff preview 格式：

```text
ahfl.durable-store-import-provider-recovery-handoff-preview.v1
```

稳定字段：

1. source provider write attempt / adapter execution format version chain
2. workflow identity
3. adapter execution identity
4. provider write attempt identity
5. planning status
6. optional provider persistence id
7. retry / resume placeholder
8. optional failure attribution
9. recovery handoff boundary summary
10. next action
11. next-step recommendation

## Compatibility Rules

1. planned provider write 必须来自 accepted + persisted `AdapterExecutionReceipt`。
2. planned provider write 必须有 mutating `ProviderPersistReceipt` intent 与 provider persistence id。
3. not-planned provider write 必须是 non-mutating noop intent，且不得有 provider persistence id。
4. unsupported provider capability 必须记录 `missing_capability`，不能伪装成真实 provider write。
5. retry / resume placeholder 只能是 placeholder identity，不能包含真实 retry token 或 resume token。
6. breaking change 需要 bump 对应 format version，并同步 docs、golden、consumer matrix、contributor guide 与 CI 标签。

## Forbidden Fields

V0.21 stable artifact 禁止包含：

1. provider credential、secret、token
2. endpoint secret reference
3. 真实 object path、database key、store URI
4. tenant secret、KMS key、IAM policy
5. host telemetry、SIEM event、operator payload
6. recovery daemon ABI、真实 retry token、真实 resume token
7. 从 `RecoveryCommandPreview`、review summary 或 CLI 文本反推得到的 provider state

## Regression Contract

最小回归：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.21
ctest --preset test-dev --output-on-failure -L v0.21-durable-store-import-provider-adapter-golden
```

golden 文件位于 `tests/durable_store_import/`，覆盖 single-file accepted、project rejected 与 workspace partial accepted。
