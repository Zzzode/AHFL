# AHFL V0.35 Native Durable Store Provider Idempotency Retry Bootstrap

V0.35 冻结 provider write retry decision。它把 mock adapter execution result 转为稳定的 idempotency / retry artifact，让 durable write 后续可以明确判断是否允许重试、是否需要 duplicate review。

## Artifact Chain

```text
ProviderSdkMockAdapterExecutionResult
-> ProviderWriteRetryDecision
```

formats：

```text
ahfl.durable-store-import-provider-write-retry-decision.v1
ahfl.provider-write-retry-token.v1
```

## Idempotency Boundary

V0.35 固定 namespace：

```text
ahfl.durable-store-import.provider-write.v1
```

decision 记录 idempotency key、retry token placeholder identity、duplicate detection summary 与 retry decision summary。它不实现 production retry scheduler，也不隐式重试真实 provider 写入。

## Retry Mapping

V0.35 与 mock adapter taxonomy 对齐：

1. `accepted` -> `not-applicable`，不允许 retry。
2. `timeout` / `throttled` -> `retryable`，允许 retry。
3. `conflict` -> `duplicate-review-required`，需要 duplicate review。
4. `provider-failure` / `schema-mismatch` -> `non-retryable`。
5. `blocked` -> `blocked`。

## Entry Points

- `include/ahfl/durable_store_import/provider_retry.hpp`
- `src/durable_store_import/provider_retry.cpp`
- `emit-durable-store-import-provider-write-retry-decision`
