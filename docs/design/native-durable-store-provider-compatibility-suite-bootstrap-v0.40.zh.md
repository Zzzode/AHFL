# AHFL V0.40 Native Durable Store Provider Compatibility Suite Bootstrap

V0.40 建立 provider compatibility suite artifact 边界。它消费 V0.39 operator review / telemetry summary，输出 test manifest、fixture matrix 与 compatibility report，不接入外部 provider 服务。

## Artifact Chain

```text
ProviderOperatorReviewEvent
-> ProviderCompatibilityTestManifest
-> ProviderFixtureMatrix
ProviderTelemetrySummary
-> ProviderCompatibilityReport
```

formats：

```text
ahfl.durable-store-import-provider-compatibility-test-manifest.v1
ahfl.durable-store-import-provider-fixture-matrix.v1
ahfl.durable-store-import-provider-compatibility-report.v1
```

## Coverage Boundary

1. manifest 必须覆盖 SDK mock adapter 与 local filesystem alpha provider。
2. fixture matrix 必须覆盖 success、timeout、conflict 与 schema mismatch 基础路径。
3. report 必须明确 compatibility status、capability matrix completeness 与 external service requirement。
4. 默认 compatibility suite 不依赖外部服务。

## Entry Points

- `include/ahfl/durable_store_import/provider_compatibility.hpp`
- `src/durable_store_import/provider_compatibility.cpp`
- `emit-durable-store-import-provider-compatibility-test-manifest`
- `emit-durable-store-import-provider-fixture-matrix`
- `emit-durable-store-import-provider-compatibility-report`
