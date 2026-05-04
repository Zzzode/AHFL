# AHFL V0.31 Native Durable Store Provider Local Host Harness Bootstrap

V0.31 建立 test-only local host harness。它把 V0.30 secret policy review 推进为受控 harness request、execution record 与 reviewer summary，但仍禁止 network、secret、host environment、filesystem write 与真实 provider SDK。

## Artifact Chain

```text
ProviderSecretPolicyReview
-> ProviderLocalHostHarnessExecutionRequest
-> ProviderLocalHostHarnessExecutionRecord
-> ProviderLocalHostHarnessReview
```

formats：

```text
ahfl.durable-store-import-provider-local-host-harness-request.v1
ahfl.durable-store-import-provider-local-host-harness-execution-record.v1
ahfl.durable-store-import-provider-local-host-harness-review.v1
```

## Boundary

V0.31 的 runner 是 deterministic test harness，不是 production process manager。它覆盖：

1. success
2. nonzero exit
3. timeout
4. sandbox denied

record 保存 exit code、timeout flag、sandbox denial flag 与 captured diagnostic summary。review 的 success path 才能进入 V0.32 payload materialization。

## Entry Points

- `include/ahfl/durable_store_import/provider_local_host_harness.hpp`
- `src/durable_store_import/provider_local_host_harness.cpp`
- `emit-durable-store-import-provider-local-host-harness-request`
- `emit-durable-store-import-provider-local-host-harness-record`
- `emit-durable-store-import-provider-local-host-harness-review`
