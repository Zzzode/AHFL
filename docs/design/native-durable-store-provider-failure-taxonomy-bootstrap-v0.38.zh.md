# AHFL V0.38 Native Durable Store Provider Failure Taxonomy Bootstrap

V0.38 定义 provider failure taxonomy v1。它把 mock adapter normalized result 与 recovery plan 摘要映射为稳定 failure kind、category、retryability、operator action 与 security sensitivity。

## Artifact Chain

```text
ProviderSdkMockAdapterExecutionResult
ProviderWriteRecoveryPlan
-> ProviderFailureTaxonomyReport
-> ProviderFailureTaxonomyReview
```

formats：

```text
ahfl.durable-store-import-provider-failure-taxonomy-report.v1
ahfl.durable-store-import-provider-failure-taxonomy-review.v1
```

## Taxonomy Scope

V0.38 taxonomy v1 覆盖：

1. authentication
2. authorization
3. network
4. throttling
5. conflict
6. schema mismatch
7. provider internal
8. unknown
9. blocked

mock adapter 当前自然覆盖 network、throttling、conflict、schema mismatch、provider internal、blocked 与 none。authentication / authorization 作为稳定 taxonomy kind 预留给后续真实 provider mapping。

## Safety Boundary

1. 不持久化 raw provider error。
2. 不持久化 secret-bearing error message。
3. operator action 只是 review hint，不自动修复。

## Entry Points

- `include/ahfl/durable_store_import/provider_failure_taxonomy.hpp`
- `src/durable_store_import/provider_failure_taxonomy.cpp`
- `emit-durable-store-import-provider-failure-taxonomy-report`
- `emit-durable-store-import-provider-failure-taxonomy-review`
