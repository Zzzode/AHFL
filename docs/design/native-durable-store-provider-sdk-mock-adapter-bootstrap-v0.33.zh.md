# AHFL V0.33 Native Durable Store Provider SDK Mock Adapter Bootstrap

V0.33 建立 provider SDK mock adapter，用 fake provider payload audit 驱动 contract、mock execution result 与 readiness review。它是未来真实 adapter 的回归替身，不访问 network、不读取 secret、不调用真实 provider SDK。

## Artifact Chain

```text
ProviderSdkPayloadAuditSummary
-> ProviderSdkMockAdapterContract
-> ProviderSdkMockAdapterExecutionResult
-> ProviderSdkMockAdapterReadiness
```

formats：

```text
ahfl.durable-store-import-provider-sdk-mock-adapter-contract.v1
ahfl.durable-store-import-provider-sdk-mock-adapter-execution-result.v1
ahfl.durable-store-import-provider-sdk-mock-adapter-readiness.v1
```

## Scenario Matrix

V0.33 直接覆盖：

1. success -> accepted
2. failure -> provider_failure
3. timeout -> timeout
4. throttle -> throttled
5. conflict -> conflict
6. schema mismatch -> schema_mismatch

## Entry Points

- `include/ahfl/durable_store_import/provider_sdk_mock_adapter.hpp`
- `src/durable_store_import/provider_sdk_mock_adapter.cpp`
- `emit-durable-store-import-provider-sdk-mock-adapter-contract`
- `emit-durable-store-import-provider-sdk-mock-adapter-execution`
- `emit-durable-store-import-provider-sdk-mock-adapter-readiness`
