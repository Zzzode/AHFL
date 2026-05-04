# AHFL V0.37 Native Durable Store Provider Recovery Resume Bootstrap

V0.37 冻结 provider write recovery / resume artifact 边界。它消费 V0.36 commit receipt，输出 recovery checkpoint、recovery plan 与 reviewer-facing recovery review。

## Artifact Chain

```text
ProviderWriteCommitReceipt
-> ProviderWriteRecoveryCheckpoint
-> ProviderWriteRecoveryPlan
-> ProviderWriteRecoveryReview
```

formats：

```text
ahfl.durable-store-import-provider-write-recovery-checkpoint.v1
ahfl.durable-store-import-provider-write-recovery-plan.v1
ahfl.durable-store-import-provider-write-recovery-review.v1
ahfl.provider-write-resume-token.v1
```

## Recovery Mapping

V0.37 从 commit status 映射 recovery：

1. `committed` -> no-op recovery，进入 audit event。
2. `duplicate` -> lookup duplicate commit，需要 operator approval。
3. `partial` -> 使用 idempotency key 和 resume token placeholder 恢复。
4. `failed` -> manual remediation。
5. `blocked` -> wait for commit receipt。

## Non-Goals

V0.37 不实现 production recovery daemon、remote lock manager、provider-specific rollback 或 operator console。

## Entry Points

- `include/ahfl/durable_store_import/provider_recovery.hpp`
- `src/durable_store_import/provider_recovery.cpp`
- `emit-durable-store-import-provider-write-recovery-checkpoint`
- `emit-durable-store-import-provider-write-recovery-plan`
- `emit-durable-store-import-provider-write-recovery-review`
