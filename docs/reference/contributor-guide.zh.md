# AHFL Contributor Guide

本文是 AHFL 贡献者的当前入口。工程原则以根 `AGENTS.md` 为准：优先行业标准，大重构优于兼容壳，canonical identity 使用数值 ID，数据模型使用 flat stores、hash-consed types 和 `std::variant`，用户诊断必须携带 `SourceRange`。

关联文档：

- [编译器架构](../design/compiler-architecture.zh.md)
- [语义架构](../design/semantics-architecture.zh.md)
- [Native runtime 架构](../design/native-runtime-architecture.zh.md)
- [测试策略](../design/testing-strategy.zh.md)
- [项目状态](../plans/project-status.zh.md)

## 首次设置

```bash
scripts/install-githooks.sh
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
```

提交信息必须使用英文 conventional commit。所有计划文档进入 `docs/plans/`。

## 架构规则

### Identity

1. Symbol、type substitution、runtime entity 和 flat-store reference 使用 index / ID。
2. 字符串只用于源码名称、diagnostic 和展示。
3. 禁止把 canonical association 放入 string-keyed map 后再做 name join。

### Data model

1. AST、HIR、IR、event payload 和 value payload 使用 `std::variant`。
2. data-carrying model 禁止 inheritance、virtual dispatch 和 `dynamic_cast`。
3. tree-heavy model 优先 `vector<T>` + index reference。
4. type equality 使用 `TypeContext` interned pointer equality。

### Diagnostics

1. 每个用户可见 error / warning 必须有 `SourceRange`。
2. message 必须说明用户如何修复。
3. parser、typechecker 和 runtime failure 不能降成无上下文的 “internal error”。

## 按领域找入口

| 领域 | 主要入口 | 最小验证 |
|------|----------|----------|
| Grammar / AST | `grammar/AHFL.g4`、`include/ahfl/compiler/frontend/ast.hpp`、`src/compiler/syntax/frontend/` | syntax + formatter tests |
| Resolver / Sema | `src/compiler/semantics/` | type resolver、effects、flow、CLI diagnostics |
| Typed HIR / IR | `src/compiler/ir/` | typed HIR、IR verify、backend registry |
| Formal | `src/compiler/backends/smv/`、`src/verification/formal/` | SMV golden + real checker gates |
| Runtime | `include/ahfl/runtime/`、`src/runtime/engine/` | workflow/event/report/projection/recovery |
| Providers | `src/runtime/providers/` | provider unit + local transport integration |
| CLI | `src/tooling/cli/` | command routing + package/single-file smoke |
| LSP | `src/tooling/lsp/`、`tools/vscode/` | handler + extension host tests |
| Formatter | `src/tooling/formatter/` | idempotence over examples/tests/std |

## Runtime 贡献路径

运行期动态事实的唯一 owner 是 `ExecutionEventStore`：

```text
WorkflowRuntime
  -> ExecutionEventSink
  -> ExecutionEventStore
  -> ExecutionReport / replay / audit / scheduler / checkpoint
  -> WorkflowRecoverySnapshot
```

增加 runtime 行为时：

1. 先写 failing lifecycle/projection test。
2. 确定需要的 strong ID 和 event payload。
3. 定义 start/terminal pairing、retry/fallback/cancel/interruption path。
4. 在 `WorkflowRuntime` 产生 event。
5. 同步 report 和所有受影响 projection。
6. 若影响恢复，更新 `ahfl.workflow-recovery.v1` 的迁移或拒绝策略。
7. 最后更新 renderer；renderer 不得补造运行事实。

禁止：

1. 新增平行 session/journal/snapshot 事实源。
2. 从 human output 或 provider log 反推 scheduler/checkpoint。
3. 用 node/workflow/provider name 作为 runtime canonical identity。
4. 把 raw secret 或 provider credential 写入 event/recovery snapshot。

## Sema 贡献路径

当前拆分目标是 `DeclarationSema`、`ExpressionSema`、`FlowWorkflowSema` 和 `ConstSema`。新增语义不要继续堆进 monolithic `TypeCheckPass`：

1. 先确定 owner layer。
2. 通过 services/context 注入最小依赖。
3. 稳定 semantic fact 写入 Typed HIR。
4. assurance/formal 消费 Typed HIR facts，不重扫 AST。
5. 用正例、负例和 CLI diagnostic 覆盖 `SourceRange`。

## Corelib / std

修改 `std/` 时必须显式选择当前 checkout 作为 sysroot：

```bash
./build/dev/src/tooling/cli/ahflc check --manifest std/ahfl.toml --sysroot .
./build/dev/src/tooling/cli/ahflc check std/json.ahfl --sysroot .
./build/dev/src/tooling/cli/ahflc fmt --check std/ --sysroot .
```

VS Code workspace settings：

```json
{
  "ahfl.toolchain.sysroot": "${workspaceFolder}"
}
```

container 使用 nominal `Option` / `List` / `Set` / `Map`。禁止恢复 legacy type payload 或 detached-source fallback。

## 文档规则

1. 先按 `spec`、`design`、`plans`、`reference` 分类。
2. 当前文件名不带版本后缀，以 `docs/README.md` 为准。
3. 修改文档路径时更新所有 inbound links 和索引。
4. capability status 必须区分“已实现”“已落库但未产品化”“未做/证据不足”。
5. gate、golden 和 evidence report 不能代替 prompt-to-artifact 行为审计。

## 验证顺序

从最具体到最广：

```bash
ctest --preset test-dev --output-on-failure -R '<focused-regex>'
python3 scripts/check-architecture.py
python3 scripts/check-product-scope-freeze.py
cmake --build --preset build-dev -j1
ctest --preset test-dev --output-on-failure -L '^ahfl-controlled-pilot$'
ctest --preset test-dev --output-on-failure -L '^ahfl-beta-gate$'
ctest --preset test-dev --output-on-failure
```

`ahfl-controlled-pilot` 和 `ahfl-beta-gate` 都校验 evidence 的
`source_revision` 等于当前 checkout；修改源码后必须重新生成，不能复用旧工作区产物。

`production-confidence` evidence 还必须证明 long-lived worker 在受控 provider
断连后通过 runtime retry 恢复。`provider_retry_count` 至少达到 gate contract 下限，
且 `provider_request_count` 必须严格等于 workflow iterations 加 canonical retry 数；
不能通过直接修改 evidence revision 或计数绕过真实重跑。

runtime kernel 变更至少覆盖：

```bash
ctest --preset test-dev --output-on-failure \
  -R 'ahfl\.runtime\.(workflow_runtime|execution_event|execution_report|execution_projection|workflow_recovery)_all|ahfl\.reference_workflow\.recovery_smoke'
```

## 完成标准

完成不等于“有源码”“golden 更新”或“beta gate 绿色”。最终声明前必须确认：

1. 用户请求的每个行为都能从真实入口到达。
2. 删除的旧面不再存在于 source、CMake、tests、docs 和 install targets。
3. build 与相关测试是 fresh run。
4. README、status 和 backlog 不夸大 capability。
5. 没有把 draft/No-Go 的 Native gRPC、Web Playground 或 ecosystem adapter 偷偷产品化。
