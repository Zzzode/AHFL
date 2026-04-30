# AHFL V0.22 Durable Store Provider Driver Prototype Compatibility

本文冻结 V0.22 `ProviderDriverBindingPlan` 与 `ProviderDriverReadinessReview` 的 compatibility contract。

## Stable Machine Artifacts

Provider driver profile 格式：

```text
ahfl.durable-store-import-provider-driver-profile.v1
```

Provider driver binding plan 格式：

```text
ahfl.durable-store-import-provider-driver-binding-plan.v1
```

稳定字段：

1. source provider write attempt / provider config / capability matrix format version chain
2. workflow identity：`workflow_canonical_name`、`session_id`、`run_id`、`input_fixture`
3. provider write attempt identity
4. provider driver binding identity
5. source planning status
6. optional provider persistence id
7. secret-free provider driver profile
8. driver operation kind
9. binding status
10. optional operation descriptor identity
11. `invokes_provider_sdk = false`
12. optional driver binding failure attribution

## Stable Reviewer Projection

Provider driver readiness review 格式：

```text
ahfl.durable-store-import-provider-driver-readiness-review.v1
```

稳定字段：

1. source provider driver binding plan / provider write attempt format version chain
2. workflow identity
3. provider write attempt identity
4. provider driver binding identity
5. binding status
6. operation kind
7. optional operation descriptor identity
8. `invokes_provider_sdk`
9. optional failure attribution
10. driver boundary summary
11. next action
12. next-step recommendation

## Compatibility Rules

1. bound driver plan 必须来自 planned provider write attempt。
2. bound driver plan 必须有 operation descriptor identity，且不得包含 failure attribution。
3. not-bound driver plan 必须是 noop operation，且不得有 operation descriptor identity。
4. profile mismatch 与 unsupported driver capability 必须有明确 failure attribution。
5. V0.22 artifact 必须保持 `invokes_provider_sdk = false`。
6. breaking change 需要 bump 对应 format version，并同步 docs、golden、consumer matrix、contributor guide 与 CI 标签。

## Forbidden Fields

V0.22 stable artifact 禁止包含：

1. provider credential、secret、token
2. endpoint URI 或 endpoint secret reference
3. 真实 object path、database table、store URI
4. tenant secret、KMS key、IAM policy
5. provider SDK request / response payload schema
6. host telemetry、SIEM event、operator payload
7. recovery daemon ABI、真实 retry token、真实 resume token
8. 从 readiness review、recovery handoff、review summary 或 CLI 文本反推得到的 driver state

## Regression Contract

最小回归：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.22
ctest --preset test-dev --output-on-failure -L v0.22-durable-store-import-provider-driver-golden
```

golden 文件位于 `tests/durable_store_import/`，覆盖 single-file accepted、project rejected 与 workspace partial accepted。
