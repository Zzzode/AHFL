# AHFL Durable Store Import 参考 V0.1

本文合并原先 durable-store-import / durable-store-provider 的逐版本 compatibility、consumer matrix 与 contributor guide 文档。日常开发只维护本文与当前设计文档：

- [durable-store-import-architecture-v0.1.zh.md](../design/durable-store-import-architecture-v0.1.zh.md)

## 事实来源

| Area | Source |
| --- | --- |
| Core durable-store-import models | `src/pipeline/persistence/durable_store_import/*.hpp` |
| Receipt persistence transition rules | `src/pipeline/persistence/durable_store_import/receipt_persistence_stage.hpp` |
| Provider model domains | `src/pipeline/persistence/durable_store_import/provider/**/*.hpp` |
| Provider artifact descriptors | `src/pipeline/persistence/durable_store_import/provider_artifacts.def` |
| Provider CLI artifact registry | `src/tooling/cli/pipeline_durable_store_import_provider_artifacts.def` |
| Provider CLI command registry | `src/tooling/cli/durable_store_import_provider_commands.def` |
| Provider command routing tests | `tests/cli/command_routing.cpp` |
| Golden command registration | `tests/cmake/TestTargets.cmake` |

## 命令族

Core durable-store-import commands:

```text
emit-durable-store-import-request
emit-durable-store-import-review
emit-durable-store-import-decision
emit-durable-store-import-receipt
emit-durable-store-import-receipt-persistence-request
emit-durable-store-import-receipt-review
emit-durable-store-import-receipt-persistence-review
emit-durable-store-import-receipt-persistence-response
emit-durable-store-import-receipt-persistence-response-review
emit-durable-store-import-adapter-execution
emit-durable-store-import-recovery-preview
emit-durable-store-import-decision-review
```

`emit-durable-store-import-receipt-persistence-request` 与
`emit-durable-store-import-receipt-persistence-response` 是稳定 CLI projection。不要在各自 builder 里复制状态映射；核心规则必须放在 `ReceiptPersistenceStage`。

内部 C++ API 不提供 `build_durable_store_import_*` 兼容 shim；新增模型和 review action 应使用短语义名，并由 artifact printer 映射到稳定 JSON 字符串。

Provider 命令由 `src/tooling/cli/durable_store_import_provider_commands.def` 注册生成。不要复制 command handler 来新增 provider 命令；新增时应在 `src/tooling/cli/pipeline_durable_store_import_provider_artifacts.def` 添加一条 artifact 记录，在 `src/tooling/cli/durable_store_import_provider_commands.def` 添加一条 command 记录，再交给 `ProviderPipeline` 统一 dispatch。

## Provider 领域映射

| Domain | Primary command examples | Model headers |
| --- | --- | --- |
| Binding | `emit-durable-store-import-provider-write-attempt`, `emit-durable-store-import-provider-driver-binding` | `src/pipeline/persistence/durable_store_import/provider/binding/*.hpp` |
| Runtime | `emit-durable-store-import-provider-runtime-preflight`, `emit-durable-store-import-provider-sdk-envelope` | `src/pipeline/persistence/durable_store_import/provider/runtime/*.hpp` |
| Host execution | `emit-durable-store-import-provider-host-execution`, `emit-durable-store-import-provider-local-host-harness-request` | `src/pipeline/persistence/durable_store_import/provider/execution/*.hpp` |
| SDK | `emit-durable-store-import-provider-sdk-adapter-request`, `emit-durable-store-import-provider-sdk-payload-plan` | `src/pipeline/persistence/durable_store_import/provider/sdk/*.hpp` |
| Configuration | `emit-durable-store-import-provider-config-load`, `emit-durable-store-import-provider-secret-resolver-request` | `src/pipeline/persistence/durable_store_import/provider/configuration/*.hpp` |
| Reliability | `emit-durable-store-import-provider-write-retry-decision`, `emit-durable-store-import-provider-write-commit-receipt` | `src/pipeline/persistence/durable_store_import/provider/reliability/*.hpp` |
| Governance | `emit-durable-store-import-provider-compatibility-report`, `emit-durable-store-import-provider-registry` | `src/pipeline/persistence/durable_store_import/provider/governance/*.hpp` |
| Production | `emit-durable-store-import-provider-production-readiness-report`, `emit-durable-store-import-provider-runtime-policy-report` | `src/pipeline/persistence/durable_store_import/provider/production/*.hpp` |

## 回归命令

Build:

```sh
cmake --build --preset build-dev
```

Run the full suite:

```sh
ctest --preset test-dev --output-on-failure
```

Run the durable-store/provider area:

```sh
ctest --preset test-dev --output-on-failure -L ahfl-v0.15
ctest --preset test-dev --output-on-failure -L ahfl-v0.21
ctest --preset test-dev --output-on-failure -L ahfl-v0.42
ctest --preset test-dev --output-on-failure -R 'ahflc\\.emit_durable_store_import_provider_.*with_package|ahfl\\.schema_compatibility|ahfl\\.cli\\.command_routing_all'
```

Use the exact version label only when bisecting historical artifact behavior. For normal provider work, prefer the regex run above plus the full suite before publishing.

## 贡献检查清单

When adding or changing a durable-store/provider artifact:

1. Update the model header and implementation in the relevant domain folder.
2. Update `src/pipeline/persistence/durable_store_import/provider_artifacts.def` if the artifact is provider-owned.
3. Update `src/tooling/cli/pipeline_durable_store_import_provider_artifacts.def` and `src/tooling/cli/durable_store_import_provider_commands.def` for provider CLI emission.
4. Add or update model validation tests.
5. Add or update golden CLI output when the command surface changes.
6. Run `tests/cli/command_routing.cpp` coverage through `ahfl.cli.command_routing_all`.
7. Keep artifact outputs secret-free unless the artifact explicitly models a secret handle. Never persist raw provider payload or credential material by default.
