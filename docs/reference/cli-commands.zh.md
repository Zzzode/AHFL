# AHFL CLI Commands

本文是 `docs/reference` 中 CLI 命令参考的合并入口，统一覆盖原多版本文档。当前为唯一维护口径；历史版本内容仅作为演进背景纳入摘要，不再保留独立入口。

关联文档：

- [project-usage.zh.md](./project-usage.zh.md)
- [durable-store-import-reference.zh.md](./durable-store-import-reference.zh.md)
- [migration-policy.zh.md](./migration-policy.zh.md)
- [contributor-guide.zh.md](./contributor-guide.zh.md)

## 合并范围

| 演进阶段 | 合并后保留的信息 |
|----------|------------------|
| 早期 core 命令族 | core check / dump / emit / formal 命令族、基础输入模式、退出码。 |
| PackageGraph 扩展 | `ahfl.toml`、`ahfl.workspace.toml`、`dump package-graph`、manifest/workspace 命令形态。 |
| Handoff target authoring | handoff target、`emit native-json --manifest --target`、native package 路径。 |
| Runtime artifact 链 | runtime session / journal / replay / scheduler / persistence / store import 输出链路。 |
| Provider 与选项表 | Provider artifact 可见性、声明式选项表、`CliDriver`、结构化 diagnostics 与 `run` 入口。 |

## 当前口径摘要

1. `ahflc` 是主要编译、验证、artifact 与 runtime CLI 入口；真实 workflow 执行统一进入 `ahflc run`。
2. package-aware 输入优先使用 `--manifest <ahfl.toml>`，workspace 输入使用 `--workspace <ahfl.workspace.toml> --package <name>`。
3. 单文件 `<input.ahfl>` 和 `--search-root` 仍可用于草稿、smoke 和低层调试，但不再是工程配置模型。
4. Native handoff package 由 manifest target metadata 驱动：`emit native-json --manifest <ahfl.toml> --target <name>`。
5. Provider diagnostic artifact 使用内部入口 `emit-provider-artifact provider/<artifact>`；Internal artifact 必须显式传入 `--show-hidden`。
6. Optimization IR 通过 `emit opt-ir` / `emit-opt-ir` 输出文本 artifact，通过 `emit opt-ir-json` / `emit-opt-ir-json` 输出 `AHFL_OPT_IR_V1` JSON artifact；普通 backend 路径仍消费 Semantic IR。
7. `ahfl-repl` 与 `ahfl-dap` 是独立开发者工具入口，分别面向交互式求值和 Debug Adapter Protocol。
8. 退出码稳定为 `0` 成功、`1` 编译/验证/runtime 错误、`2` 参数错误、`3` 内部错误。
9. 新增 `ahflc` 选项必须维护 `OptionSpec` 声明式选项表，不再扩散手写解析逻辑。

## 总览

项目输入模式：

```text
<input-mode> ::=
    --manifest <ahfl.toml> [--target <name>]
  | --workspace <ahfl.workspace.toml> --package <name> [--target <name>]
  | <input.ahfl>
  | [--search-root <dir>]... <input.ahfl>
```

核心命令：

```text
ahflc check <input-mode>
ahflc run --workflow <name> --input '<json>' [--llm-config <path>] [--tool-catalog <path>] <input-mode>
ahflc fmt [--check] <input.ahfl|dir>...
ahflc fmt [--check] --manifest <ahfl.toml>
ahflc fmt [--check] --workspace <ahfl.workspace.toml> --package <name>
ahflc dump-ast <input-mode>
ahflc dump-types <input-mode>
ahflc dump-project <input-mode>
ahflc dump package-graph --manifest <ahfl.toml>
ahflc dump package-graph --workspace <ahfl.workspace.toml> --package <name>
ahflc emit-ir <input-mode>
ahflc emit-ir-json <input-mode>
ahflc emit-opt-ir <input-mode>
ahflc emit-opt-ir-json <input-mode>
ahflc emit-native-json --manifest <ahfl.toml> --target <handoff-target>
ahflc emit-execution-plan <input-mode>
ahflc emit-runtime-session --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-execution-journal --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-replay-view --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-scheduler-snapshot --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-scheduler-review --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-audit-report --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-dry-run-trace --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-package-review <input-mode>
ahflc emit-summary <input-mode>
ahflc emit-smv [--smv-size-report] <input-mode>
ahflc emit-assurance-json <input-mode>
ahflc validate-assurance <input-mode>
ahflc verify-formal [--formal-backend <nuxmv|nusmv|spin|tlaplus>] [--model-checker <path>] [--formal-model-out <model.smv>] <input-mode>
```

开发者工具入口：

```text
ahfl-repl [--help]
ahfl-dap [--help]
ahfl-incremental [--help] <changed.ahfl>...
```

## 选项

### 输入选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--manifest` | `<ahfl.toml>` | AHFL package manifest；用于 package-aware check / fmt / emit / dump |
| `--workspace` | `<ahfl.workspace.toml>` | AHFL workspace manifest；必须配合 `--package <name>` 选择 member package |
| `--package` | `<name>` | 与 `--workspace` 配合选择 workspace package |
| `--target` | `<name>` | 选择 manifest target；多 target package 必须显式传入 |
| `--sysroot` | `<path>` | AHFL sysroot；目录下必须包含 `std/ahfl.toml` |
| `--search-root` | `<dir>` | 额外搜索根目录 (可重复) |
| `--capability-mocks` | `<mocks.json>` | deterministic capability mock 输入；`run` 中作为 LLM function tools source |
| `--tool-catalog` | `<tools.json>` | 仅 `run`：deterministic runtime tool catalog 输入，作为 LLM function tools source |
| `--capability-bindings` | `<bindings.json>` | 仅 `run`：HTTP/gRPC JSON transcoding runtime capability binding 输入 |
| `--input-fixture` | `<fixture>` | package fixture request 选择 |
| `--run-id` | `<id>` | 稳定 run identity |
| `--workflow` | `<canonical>` | 显式选择目标 workflow |
| `--input` | `<json>` | `run` 命令的真实 runtime 输入 JSON |
| `--llm-config` | `<path>` | `run` 命令的 LLM Provider 配置，默认 `~/.ahfl/llm_config.json` |
| `--llm-observability` | `<llm-observability.json>` | 将 `run` 的 LLM cache / fallback / streaming / token usage / token budget / secret lifecycle secret-free 事件写入机器可读 JSON；usage 超过 `max_total_tokens` 时仍先写出该 artifact，再以 `runtime.LLM_TOKEN_BUDGET_EXCEEDED` 失败 |

### 输出控制选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--formal-backend` | `<nuxmv\|nusmv\|spin\|tlaplus>` | 仅 `verify-formal`: 选择形式化 backend。当前只有 `nuxmv` / `nusmv` 支持 AHFL SMV 外部验证；`spin` / `tlaplus` 会报告 `verification_unsupported` |
| `--model-checker` | `<path>` | 仅 `verify-formal`: NuSMV/nuXmv checker 路径 |
| `--checker-timeout-seconds` | `<seconds>` | 仅 `verify-formal`: 外部 checker 进程 timeout；超时会报告 `checker_status: checker_error` 与 `checker_timed_out: true` |
| `--formal-model-out` | `<model.smv>` | 仅 `verify-formal`: 保留 SMV 模型文件 |
| `--dump-ast` | - | 输出 AST outline |
| `--dump-types` | - | 输出类型环境 |
| `--explain` | - | 启用结构化解释 |
| `--check` | - | 仅 `fmt`: 检查格式化状态，不写回文件 |
| `-O` / `--optimize` | - | 启用优化 passes；普通 backend 路径运行 Semantic IR passes，`emit opt-ir` / `emit opt-ir-json` 输出 Opt IR passes 后的结果 |
| `--time-passes` | - | 与 `-O` 配合使用，把 Semantic IR pass pipeline 耗时输出到 stderr |
| `--smv-size-report` | - | 仅 `emit smv`: 把 SMV artifact 的 byte、line、LTLSPEC 数量输出到 stderr |
| `--trace-export` | `<trace.jsonl>` | 将 CLI command span 以 JSONL 写入文件，不改变 stdout artifact |
| `--metrics-export` | `<metrics.jsonl>` | 将 CLI duration / exit_code metrics 以 JSONL 写入文件，不改变 stdout artifact |
| `--structured-log` | `<log.jsonl>` | 将 CLI command completion 结构化日志以 JSONL 写入文件，不改变 stdout artifact |
| `--memory-report` | `<memory.json>` | 将 source、TypedProgram、IR 规模和结构性 memory proxy 写入 JSON 文件，不改变 stdout artifact |

## Formatter

`ahflc fmt <input.ahfl|dir>...` 使用 formatter library 格式化源文件并原地写回；目录输入会递归收集 `.ahfl` 文件并按路径稳定排序。`ahflc fmt --manifest <ahfl.toml>` 会格式化 package module root 内由 PackageGraph 覆盖的 `.ahfl` source；`ahflc fmt --workspace <ahfl.workspace.toml> --package <name>` 会格式化选中 workspace package 的 source。

`ahflc fmt --check ...` 只检查文件是否已经符合格式，不写回；如果需要改动，命令返回非零退出码。批量模式会继续检查所有可读取文件，并在 stderr 输出 `format failed for X of N input(s)` 作为 partial failure 汇总。

配置文件名为 `.ahfl-format`，CLI 会从输入文件所在目录向上查找。当前支持：

```text
indent_width = 4
use_tabs = false
max_line_length = 100
trailing_newline = true
align_fields = true
sort_imports = true
```

## REPL

`ahfl-repl` 启动 AHFL 交互式 REPL；也支持通过 stdin 管道输入命令，便于 smoke test 或一次性诊断。

```bash
ahfl-repl
printf ':help\n:quit\n' | ahfl-repl
```

当前命令：

| 命令 | 用途 |
|------|------|
| `:type <expr>` | 输出表达式推导类型 |
| `:verify <code>` | 对 agent 声明运行形式化验证路径 |
| `:simulate <code>` | 对输入中的 agent 声明运行结构化状态机 step simulation，按 `initial` 到可达 `final` 的最短路径输出状态迁移 |
| `:help` | 输出帮助 |
| `:quit` | 退出 |
| `<expr>` | 求值表达式或输出声明 IR 摘要 |

## DAP

`ahfl-dap` 启动 AHFL Debug Adapter Protocol server，通过 stdin/stdout 读写 `Content-Length` frame。当前入口暴露现有 DAP library handler：`initialize`、`launch`、`disconnect`、`configurationDone`、`setBreakpoints`、`threads`、`stackTrace`、`scopes`、`variables`、`continue`、`next`、`evaluate`。

当前边界：

1. `ahfl-dap` 已有进程级 smoke test。
2. breakpoint、state inspector 与基础 command handler 已在库级测试覆盖。
3. runtime state、capability breakpoint 与真实 step execution 仍属于 DAP 产品化后续工作。

## Incremental

`ahfl-incremental` 是当前 incremental compiler 的窄入口，接收一组 changed source paths，构建单层 dependency graph，运行 parse / resolve / typecheck / IR lowering，并输出模块状态和 cache stats。

```bash
ahfl-incremental tests/golden/ir/ok_workflow_value_flow.ahfl
```

当前边界：

1. 入口使用进程内 `IrCache`，适合 smoke test 和 daemon-facing 原型验证，不提供跨进程持久缓存。
2. 入口不会读取 PackageGraph manifest，也不会从 import graph 自动发现 transitive dependents；package-aware invalidation contract 仍是后续工作。

## Profiling

`--time-passes`、`--smv-size-report`、`--trace-export`、`--metrics-export`、`--structured-log` 和 `--memory-report` 是当前已产品化的 CLI profiling / size-report / observability flag。它们都不改变 stdout artifact；`--time-passes` 和 `--smv-size-report` 报告写入 stderr，`--trace-export`、`--metrics-export`、`--structured-log` 和 `--memory-report` 写入侧路文件。

```bash
ahflc emit summary -O --time-passes examples/refund_audit_core_v0_1.ahfl
ahflc emit smv --smv-size-report examples/refund_audit_core_v0_1.ahfl
ahflc emit summary \
  --trace-export trace.jsonl \
  --metrics-export metrics.jsonl \
  --structured-log ahflc.jsonl \
  --memory-report memory.json \
  examples/refund_audit_core_v0_1.ahfl
```

当前边界：

1. `--time-passes` 必须与 `-O` / `--optimize` 一起使用。
2. `--smv-size-report` 只适用于 `emit smv`，报告 byte、line、LTLSPEC 数量。
3. `--trace-export` 输出 `ahflc.command` span，包含 command、exit_code 和 duration_ms attributes。
4. `--metrics-export` 输出 `ahfl.cli.duration_ms` 与 `ahfl.cli.exit_code` metrics，包含 command label。
5. `--structured-log` 输出 `ahflc command completed` 事件，包含 level、command、exit_code 和 duration_ms fields。
6. `--memory-report` 输出 AHFL 内存报告格式 JSON，包含 source、TypedProgram、IR 规模和结构性 memory proxy；这不是 RSS / allocator 观测。
7. Opt IR function-level optimization timing、pass-level trace schema 和平台可比 RSS / allocator memory report 仍是后续工作。

### Provider 与选项表阶段新增选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--show-hidden` | - | 在 help 中显示 Internal provider diagnostic artifact，并允许 `emit-provider-artifact` 解析 Internal artifact |
| `--help` / `-h` | - | 显示用法帮助 (可组合 `--show-hidden` 查看完整清单) |

## Optimization IR Artifact

`emit opt-ir` / `emit-opt-ir` 是当前 Opt IR 的文本诊断入口；`emit opt-ir-json` / `emit-opt-ir-json` 是同一 Opt IR 模型的机器可读入口，输出 `AHFL_OPT_IR_V1` JSON artifact。二者都会在完成 parse、resolve、typecheck、validate、Typed HIR lowering 和 Semantic IR lowering 之后，把 `ir::Program` 降到 `ir::opt::OptProgram`。artifact 包含 OptFunction、local、basic block、statement、terminator、source range，以及无法降为 pure expression fragment 的 temporal atom `skipped_temporal` 记录。

示例：

```bash
# 输出未运行 Opt IR passes 的 Opt IR
ahflc emit opt-ir tests/golden/ir/ok_expr_temporal.ahfl

# 先运行 Semantic IR passes，再输出 Opt IR passes 后的结果
ahflc emit opt-ir -O tests/golden/ir/ok_expr_temporal.ahfl

# 输出未运行 Opt IR passes 的 Opt IR JSON
ahflc emit opt-ir-json tests/golden/ir/ok_expr_temporal.ahfl

# 先运行 Semantic IR passes，再输出 Opt IR passes 后的 Opt IR JSON
ahflc emit opt-ir-json -O tests/golden/ir/ok_expr_temporal.ahfl
```

当前边界：

1. `emit opt-ir` 是 artifact dump，不是 core backend contract。
2. 普通 `emit-ir`、`emit-ir-json`、SMV、native / execution / assurance 输出仍以 `ir::Program` 为输入。
3. `-O` 在普通 backend 路径中会先运行 Semantic IR pass pipeline，因此普通 backend 输出可以变化；当前回归测试已覆盖 `emit ir` 和 `emit smv` 的 before/after 差异。
4. `-O` 不会把 Opt IR 回降到 Semantic IR，也不会改变 backend 消费类型。
5. Opt IR 当前生产路径是 artifact-only：它不会让普通 backend 隐式直连 Opt IR。
6. Opt IR 文本输出用于 golden、诊断和 pass 行为审查；机器消费 Opt IR 应使用 `AHFL_OPT_IR_V1` JSON。需要消费稳定 Semantic IR 时，仍应使用 `emit-ir-json`。

## Provider Artifact 可见性

该阶段将 62 个 provider artifact descriptor 分为两个可见性层级。Provider artifact 不再作为独立 `CommandKind` 暴露，也不再挂到用户态 `ahflc emit <artifact>` 命令面；CLI 只保留内部诊断入口 `ahflc emit-provider-artifact provider/<artifact>`：

- **Public (17 个)** — 代表 provider pipeline 的终端 diagnostic artifact，`emit-provider-artifact` 默认可解析。
- **Internal (45 个)** — 代表 pipeline 内部中间节点，仅在 `--show-hidden` 模式下显示和解析。

Provider artifact 主要用于 golden 覆盖、诊断和回归定位。普通用户命令体系只保留 `emit store/...` 等领域 artifact；`ahflc emit provider/...` 会按未知 `emit` artifact 处理。Internal artifact 仍可执行，但必须显式传入 `--show-hidden`，避免把中间节点混入默认诊断清单。

### Public Provider Artifact (17)

以下 artifact 代表 provider pipeline 的用户可交付产物：

| Artifact | 用途 |
|------|------|
| `provider/write-attempt` | 写入尝试预览 |
| `provider/driver-binding` | 驱动绑定计划 |
| `provider/sdk-mock-adapter-execution` | SDK mock 适配器执行结果 |
| `provider/write-recovery-plan` | 写入恢复计划 |
| `provider/failure-taxonomy-report` | 故障分类报告 |
| `provider/compatibility-report` | 兼容性报告 |
| `provider/registry` | Provider 注册表 |
| `provider/selection-plan` | Provider 选择计划 |
| `provider/production-readiness-evidence` | 生产就绪证据 |
| `provider/production-readiness-report` | 生产就绪报告 |
| `provider/conformance-report` | 一致性报告 |
| `provider/schema-compatibility-report` | Schema 兼容性报告 |
| `provider/config-bundle-validation-report` | 配置包验证报告 |
| `provider/release-evidence-archive-manifest` | 发布证据归档清单 |
| `provider/approval-receipt` | 审批回执 |
| `provider/runtime-policy-report` | 运行时策略报告 |
| `provider/production-integration-dry-run-report` | 生产集成干跑报告 |

### Internal Provider Artifact (45)

Internal artifact 是 pipeline DAG 的中间节点，通常不直接消费。使用 `--show-hidden --help` 查看内部诊断列表。

典型 Internal artifact 示例：

- `provider/recovery-handoff` — 恢复交接预览
- `provider/runtime-preflight` — 运行时预检计划
- `provider/sdk-envelope` — SDK 请求封装计划
- `provider/config-load` — 配置加载计划
- `provider/secret-resolver-request` — 密钥解析请求
- `provider/telemetry-summary` — 遥测摘要

### 使用示例

```bash
# 查看默认帮助
ahflc --help

# 查看内部诊断帮助 (含 Internal provider artifact)
ahflc --show-hidden --help

# 执行 Public provider diagnostic artifact
ahflc emit-provider-artifact provider/write-attempt \
  --manifest tests/integration/package_graph_manifest/ahfl.toml \
  --target workflow \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial

# 执行 Internal provider diagnostic artifact
ahflc emit-provider-artifact provider/runtime-preflight --show-hidden \
  --manifest tests/integration/package_graph_manifest/ahfl.toml \
  --target workflow \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial
```

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功 |
| `1` | 编译/验证错误 (parse / resolve / typecheck / validate / runtime) |
| `2` | 命令行参数非法 |
| `3` | 内部错误 (不变式违反) |

## 架构变更 (面向贡献者)

### 声明式选项表

所有 CLI 选项在 `src/tooling/cli/option_table.cpp` 中通过 `OptionSpec` 结构声明：

```cpp
struct OptionSpec {
    std::string_view long_name;     // "--manifest"
    std::string_view short_name;    // "" or "-O"
    OptionArgKind arg_kind;         // Flag / RequiredValue / RepeatableValue
    OptionSetter setter;            // void (*)(CommandLineOptions&, optional<string_view>)
    std::string_view help_text;
    std::string_view metavar;
};
```

添加新选项只需：
1. 在 `CommandLineOptions` 中添加字段
2. 写一个 setter 函数
3. 在 `kOptionSpecs[]` 表中添加一行

### Provider Artifact Graph

provider artifact 的 descriptor、依赖、builder 和 printer 都在 `pipeline_durable_store_import_provider_artifacts.def` 声明；`provider_artifact_catalog` 负责轻量 CLI 查询，`provider_artifact_graph` 负责 build-time dependency / builder / printer 调度：

```cpp
AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(
    WriteAttempt,                          // kind
    ProviderWriteAttemptPreview,           // artifact_type
    build_provider_write_attempt_for_cli,  // builder
    print_provider_write_attempt_preview,  // printer
    "write-attempt",                       // provider-local artifact_id
    Public,                                // visibility: Public | Internal
    0,                                     // provider-local CLI/list order
    0,                                     // dependency count
    AHFL_PROVIDER_DEPS())                  // ProviderArtifactKind dependencies
```

`Public` artifact 默认可通过内部入口 `ahflc emit-provider-artifact provider/...` 解析；`Internal` artifact 需要显式 `--show-hidden`，不属于默认诊断清单，更不属于用户态 `emit` artifact 命令面。新增 provider artifact 时，kind、builder、printer、artifact id、visibility、order 和依赖必须在同一行 registry 记录维护，避免 artifact 身份、DAG 依赖与打印逻辑分裂。

### 泛型 Provider Step 宏

`provider_step_generic.hpp` 提供 `AHFL_DEFINE_PROVIDER_STEP_1` / `AHFL_DEFINE_PROVIDER_STEP_2` 宏，为单/双依赖 builder 自动生成 null-check → invoke → render → extract 模式，消除 ~800 行重复代码。

## 最小验证建议

```bash
# 全量测试 (872 tests)
ctest --preset test-dev --output-on-failure

# 仅 CLI 相关
ctest --preset test-dev --output-on-failure -R "cli"
```
