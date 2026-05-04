# AHFL V0.37 Durable Store Provider Recovery Resume Compatibility

V0.37 的兼容边界是 provider write recovery checkpoint、recovery plan 与 recovery review。它消费 V0.36 commit receipt，不执行生产恢复 daemon。

## Stable Formats

- `ahfl.durable-store-import-provider-write-recovery-checkpoint.v1`
- `ahfl.durable-store-import-provider-write-recovery-plan.v1`
- `ahfl.durable-store-import-provider-write-recovery-review.v1`
- `ahfl.provider-write-resume-token.v1`

## Compatibility Rules

1. checkpoint 必须保留 commit receipt identity、source commit status、idempotency key 与 resume token placeholder identity。
2. plan 必须显式表达 recovery eligibility 与 plan action。
3. `committed` 必须映射为 no-op recovery，并可进入 audit event。
4. `duplicate` 必须要求 duplicate commit lookup，且需要 operator approval。
5. `partial` 必须通过 idempotency key 与 resume token placeholder 表达恢复路径。
6. `failed` 与 `blocked` 必须保留 failure attribution。
7. plan 必须 `secret_free=true`，不得通过 recovery artifact 持久化 provider secret。
8. recovery review 是 reviewer-facing projection，不替代 machine-facing checkpoint 或 plan。
