# AHFL V0.36 Native Durable Store Provider Write Commit Receipt Bootstrap

V0.36 将 provider write 的最终 commit 状态提升为稳定 artifact。它消费 V0.35 retry decision，输出 machine-facing commit receipt 与 reviewer-facing commit review。

## Artifact Chain

```text
ProviderWriteRetryDecision
-> ProviderWriteCommitReceipt
-> ProviderWriteCommitReview
```

formats：

```text
ahfl.durable-store-import-provider-write-commit-receipt.v1
ahfl.durable-store-import-provider-write-commit-review.v1
```

## Stable Fields

commit receipt 稳定包含：

1. commit receipt identity
2. provider commit reference
3. provider commit digest
4. idempotency key
5. commit status
6. secret-free / raw payload persistence flags
7. failure attribution

receipt 必须 `secret_free=true` 且 `raw_provider_payload_persisted=false`。V0.36 不公开 secret-bearing provider response，也不定义跨 provider transaction protocol。

## Commit Mapping

V0.36 从 retry eligibility 映射 commit 状态：

1. `not-applicable` -> `committed`
2. `duplicate-review-required` -> `duplicate`
3. `retryable` -> `partial`
4. `non-retryable` -> `failed`
5. `blocked` -> `blocked`

## Entry Points

- `include/ahfl/durable_store_import/provider_commit.hpp`
- `src/durable_store_import/provider_commit.cpp`
- `emit-durable-store-import-provider-write-commit-receipt`
- `emit-durable-store-import-provider-write-commit-review`
