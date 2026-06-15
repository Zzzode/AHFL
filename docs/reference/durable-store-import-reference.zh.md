# AHFL Durable Store Import 参考

本文合并原先 durable-store-import / durable-store-provider 的逐版本 compatibility、consumer matrix 与 contributor guide 文档。日常开发只维护本文与当前设计文档：

- [durable-store-import-architecture.zh.md](../design/durable-store-import-architecture.zh.md)
- [migration-policy.zh.md](./migration-policy.zh.md)

这里提到的 compatibility 历史资料只用于回溯旧版本行为；当前项目不维护前向兼容，provider compatibility / schema compatibility artifact 的含义是 release gate 或 schema drift evidence。

## 事实来源

| Area | Source |
| --- | --- |
| Core durable-store-import models | `src/pipeline/persistence/durable_store_import/*.hpp` |
| Receipt persistence transition rules | `src/pipeline/persistence/durable_store_import/receipt_persistence_stage.hpp` |
| Provider model domains | `src/pipeline/persistence/durable_store_import/provider/**/*.hpp` |
| Provider artifact descriptors | `src/pipeline/persistence/durable_store_import/provider_artifacts.def` |
| Provider internal artifact/catalog registry | `src/tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def` |
| Provider internal catalog API | `src/tooling/cli/provider/provider_artifact_catalog.*` |
| Provider internal graph mapping | `src/tooling/cli/provider/provider_artifact_graph.*` |
| Provider command routing tests | `tests/unit/tooling/cli/command_routing.cpp` |
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

Provider artifact 元数据、依赖、builder 和 printer 由 `src/tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def` 单表注册。`provider_artifact_catalog` 服务 CLI 查询；`provider_artifact_graph` 服务 build-time dependency、builder 和 printer 调度。不要复制 command handler 或新增第二张手写 command/dependency registry；新增时应在该 artifact 记录中同时声明 provider-local `artifact_id`、visibility、provider-local order、dependency、builder 和 printer，再交给 `ProviderPipeline` 统一 dispatch。

用户命令面不解析 `ahflc emit provider/...`。Provider artifact 只能通过内部诊断入口 `ahflc emit-provider-artifact provider/<artifact>` 触达，主要用于 golden 覆盖、诊断和回归定位。`visibility = Internal` 的中间节点仍必须显式传入 `--show-hidden`。

## Provider 领域映射

| Domain | Primary artifact examples | Model headers |
| --- | --- | --- |
| Binding | `provider/write-attempt`, `provider/driver-binding` | `src/pipeline/persistence/durable_store_import/provider/binding/*.hpp` |
| Runtime | `provider/runtime-preflight`, `provider/sdk-envelope` | `src/pipeline/persistence/durable_store_import/provider/runtime/*.hpp` |
| Host execution | `provider/host-execution`, `provider/local-host-harness-request` | `src/pipeline/persistence/durable_store_import/provider/execution/*.hpp` |
| SDK | `provider/sdk-adapter-request`, `provider/sdk-payload-plan` | `src/pipeline/persistence/durable_store_import/provider/sdk/*.hpp` |
| Configuration | `provider/config-load`, `provider/secret-resolver-request` | `src/pipeline/persistence/durable_store_import/provider/configuration/*.hpp` |
| Reliability | `provider/write-retry-decision`, `provider/write-commit-receipt` | `src/pipeline/persistence/durable_store_import/provider/reliability/*.hpp` |
| Governance | `provider/compatibility-report`, `provider/registry` | `src/pipeline/persistence/durable_store_import/provider/governance/*.hpp` |
| Production | `provider/production-readiness-report`, `provider/runtime-policy-report` | `src/pipeline/persistence/durable_store_import/provider/production/*.hpp` |

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
3. Update `src/tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def` for internal provider artifact emission; provider-local `artifact_id`/visibility/order/dependencies/builder/printer must live in the same graph record.
4. Add or update model validation tests.
5. Add or update golden CLI output when the command surface changes.
6. Run `tests/unit/tooling/cli/command_routing.cpp` coverage through `ahfl.cli.command_routing_all`.
7. Keep artifact outputs secret-free unless the artifact explicitly models a secret handle. Never persist raw provider payload or credential material by default.
