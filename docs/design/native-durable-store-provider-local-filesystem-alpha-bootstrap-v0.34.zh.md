# AHFL V0.34 Native Durable Store Provider Local Filesystem Alpha Bootstrap

V0.34 引入第一个真实 provider adapter alpha：`local-filesystem-alpha`。它用于验证真实 provider 结果如何进入统一 artifact chain，但默认仍走 dry-run，不在 CLI 或 CI 中执行写入。

## Artifact Chain

```text
ProviderSdkMockAdapterReadiness
-> ProviderLocalFilesystemAlphaPlan
-> ProviderLocalFilesystemAlphaResult
-> ProviderLocalFilesystemAlphaReadiness
```

formats：

```text
ahfl.durable-store-import-provider-local-filesystem-alpha-plan.v1
ahfl.durable-store-import-provider-local-filesystem-alpha-result.v1
ahfl.durable-store-import-provider-local-filesystem-alpha-readiness.v1
```

## Safety Boundary

V0.34 的 alpha provider 固定为本地文件系统，边界如下：

1. 默认 `opt_in_enabled=false`，CLI 只产生 dry-run result。
2. 不打开 network connection。
3. 不读取 secret material。
4. 不调用 cloud provider SDK。
5. fake / mock adapter 默认路径保持不变。

真实写入只允许在显式 opt-in 的直接集成测试中触发，用来证明 provider result normalization 可以承载实际 side effect reference。

## Normalized Result

V0.34 result 覆盖：

1. `accepted`：显式 opt-in 写入成功。
2. `dry-run-only`：默认路径，因 opt-in required 不写入。
3. `write-failed`：写入失败并带 failure attribution。
4. `blocked`：上游 mock adapter readiness 未就绪或 alpha plan 不可运行。

## Entry Points

- `include/ahfl/durable_store_import/provider_local_filesystem_alpha.hpp`
- `src/durable_store_import/provider_local_filesystem_alpha.cpp`
- `emit-durable-store-import-provider-local-filesystem-alpha-plan`
- `emit-durable-store-import-provider-local-filesystem-alpha-result`
- `emit-durable-store-import-provider-local-filesystem-alpha-readiness`
