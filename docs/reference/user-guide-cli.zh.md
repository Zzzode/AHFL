# AHFL CLI 工作流

本文说明当前 `ahflc` 命令面、输入模式、常用 artifact 和诊断方式。历史版本命令参考见 [cli-commands.zh.md](./cli-commands.zh.md)，但本文以当前构建产物的 `ahflc --help` 为准。

## 构建

```bash
cmake --preset dev
cmake --build --preset build-dev
```

本文示例默认使用：

```bash
./build/dev/src/tooling/cli/ahflc
./build/dev/src/tooling/repl/ahfl-repl
./build/dev/src/tooling/dap/ahfl-dap
./build/dev/src/tooling/incremental/ahfl-incremental
```

## 命令形态

当前 CLI 的主命令形态是：

```text
ahflc check [options] <input.ahfl>
ahflc run --workflow <name> --input '<json>' [--llm-config <path>] [--tool-catalog <path>] <input.ahfl>
ahflc fmt [--check] <input.ahfl>
ahflc emit <artifact> [options] <input.ahfl>
ahflc dump <target> [options] <input.ahfl>
ahflc verify [options] <input.ahfl>
ahflc validate [options] <input.ahfl>
ahfl-repl [--help]
ahfl-dap [--help]
ahfl-incremental [--help] <changed.ahfl>...
```

核心动作：

| 动作 | 用途 | 示例 |
|------|------|------|
| `check` | 解析、命名解析、类型检查、结构验证 | `ahflc check examples/refund_audit_core_v0_1.ahfl` |
| `fmt` | 按 `.ahfl-format` 或默认规则格式化单个源文件；`--check` 只检查不写回 | `ahflc fmt --check examples/refund_audit_core_v0_1.ahfl` |
| `dump ast` | 输出 AST outline | `ahflc dump ast <input>` |
| `dump types` | 输出类型环境 | `ahflc dump types <input>` |
| `dump project` | 输出 project source graph | `ahflc dump project --project <manifest>` |
| `emit <artifact>` | 输出编译、执行、审计或持久化 artifact | `ahflc emit summary <input>` |
| `validate` | 运行 assurance production gate | `ahflc validate <input>` |
| `verify` | 调用 SMV 兼容 checker 做 formal verification | `ahflc verify --formal-backend nuxmv --model-checker <path> <input>` |
| `run` | 使用 LLM Provider 执行 workflow | `ahflc run --workflow <name> --input '<json>' <input>` |
| `ahfl-repl` | 启动开发者 REPL，支持表达式求值、`:type`、`:verify` | `ahfl-repl` |
| `ahfl-dap` | 启动 Debug Adapter Protocol server | `ahfl-dap` |
| `ahfl-incremental` | 对 changed source paths 运行 incremental compiler 原型入口 | `ahfl-incremental tests/golden/ir/ok_workflow_value_flow.ahfl` |

## 输入模式

同一套编译链支持三种输入方式。

| 模式 | 适用场景 | 示例 |
|------|----------|------|
| 单文件 | 快速试验、最小复现 | `ahflc check examples/refund_audit_core_v0_1.ahfl` |
| `--search-root` | 单文件入口加自定义 import 搜索根 | `ahflc check --search-root tests/integration/workflow_value_flow tests/integration/workflow_value_flow/app/main.ahfl` |
| `--project` | 稳定项目入口 | `ahflc check --project tests/integration/workflow_value_flow/ahfl.project.json` |
| `--workspace --project-name` | 多项目 workspace | `ahflc check --workspace tests/integration/ahfl.workspace.json --project-name check-ok` |

项目 manifest 示例：

```json
{
  "format_version": "ahfl.project",
  "name": "workflow-value-flow",
  "search_roots": ["."],
  "entry_sources": ["app/main.ahfl"]
}
```

Workspace descriptor 示例：

```json
{
  "format_version": "ahfl.workspace",
  "name": "workspace-ok",
  "projects": ["check_ok/ahfl.project.json"]
}
```

## 常用选项

| 选项 | 用途 |
|------|------|
| `--project <path>` | 使用 project descriptor |
| `--workspace <path>` | 使用 workspace descriptor |
| `--project-name <name>` | 从 workspace 中选择 project |
| `--search-root <dir>` | 增加 import 搜索根，可重复 |
| `--package <path>` | 使用 package authoring descriptor |
| `--capability-mocks <path>` | 为 dry run / runtime artifact 提供 mock capability 输入；在 `run` 中作为 LLM tool source |
| `--tool-catalog <path>` | 仅 `run`：提供 deterministic runtime tool catalog，并作为 LLM function tools source |
| `--input-fixture <fixture>` | 选择 runtime fixture request |
| `--run-id <id>` | 固定 run identity，便于稳定输出 |
| `--workflow <canonical>` | 选择目标 workflow |
| `--input '<json>'` | `run` 的真实 runtime 输入 JSON |
| `--llm-config <path>` | `run` 的 LLM Provider 配置，默认 `~/.ahfl/llm_config.json` |
| `--llm-observability <path>` | 将 `run` 的 LLM cache / fallback / streaming secret-free 事件写入机器可读 JSON |
| `--formal-backend <name>` | `verify` 使用的 backend；当前 `nuxmv` / `nusmv` 可外部验证，`spin` / `tlaplus` 会报告 unsupported |
| `--model-checker <path>` | `verify` 使用的 NuSMV / nuXmv 可执行文件 |
| `--checker-timeout-seconds <seconds>` | `verify` 的外部 checker 进程 timeout；卡死会报告 `checker_timed_out: true` |
| `--formal-model-out <path>` | `verify` 保留生成的 SMV 模型 |
| `--show-hidden` | 显示或启用内部诊断 artifact |
| `--check` | 仅 `fmt` 使用；检查格式化状态但不写回文件 |
| `-O` | 启用优化 passes |
| `--time-passes` | 与 `-O` 配合输出 Semantic IR pass pipeline 耗时 |
| `--trace-export <path>` | 将 CLI command span 写入 JSONL 文件 |
| `--metrics-export <path>` | 将 CLI duration / exit_code metrics 写入 JSONL 文件 |
| `--structured-log <path>` | 将 CLI command completion 结构化日志写入 JSONL 文件 |
| `--memory-report <path>` | 将 source、TypedProgram、IR 规模和结构性 memory proxy 写入 JSON 文件 |
| `-h` / `--help` | 查看帮助 |

## Formatter

`ahflc fmt` 支持文件、目录、project descriptor 和 workspace descriptor。默认行为会原地改写输入文件；`--check` 只比较格式化结果，不写回，发现差异时返回非零退出码。

```bash
./build/dev/src/tooling/cli/ahflc fmt examples/refund_audit_core_v0_1.ahfl
./build/dev/src/tooling/cli/ahflc fmt examples/
./build/dev/src/tooling/cli/ahflc fmt --project ahfl.project.json
./build/dev/src/tooling/cli/ahflc fmt --workspace ahfl.workspace.json --project-name app
./build/dev/src/tooling/cli/ahflc fmt --check examples/refund_audit_core_v0_1.ahfl
```

目录输入会递归收集 `.ahfl` 文件并按路径稳定排序。`--project` / `--workspace --project-name` 会格式化 descriptor 中声明的 `entry_sources`。批量 `--check` 会继续检查所有可读取文件，并输出 partial failure 汇总，例如 `format failed for 2 of 3 input(s)`。

formatter 会从输入文件所在目录向上查找 `.ahfl-format`。当前配置格式是简单 `key = value`：

```text
indent_width = 4
use_tabs = false
max_line_length = 100
trailing_newline = true
align_fields = true
sort_imports = true
```

formatter 语法覆盖仍需要继续扩展；当前批量 project/workspace 模式覆盖 manifest `entry_sources`，不主动追踪完整 import graph。

## REPL

`ahfl-repl` 是独立可执行入口，不挂在 `ahflc` 子命令下。交互式终端会显示 `ahfl> ` prompt；非交互式 stdin 可直接管道输入命令。

```bash
./build/dev/src/tooling/repl/ahfl-repl
printf ':help\n:quit\n' | ./build/dev/src/tooling/repl/ahfl-repl
```

常用命令：

| 命令 | 用途 |
|------|------|
| `:type <expr>` | 查看表达式类型 |
| `:verify <code>` | 对 agent 声明执行形式化验证路径 |
| `:simulate <code>` | 对输入中的 agent 声明运行结构化状态机 step simulation，输出从 `initial` 到可达 `final` 的状态迁移 |
| `:help` | 查看 REPL 帮助 |
| `:quit` | 退出 |

## DAP

`ahfl-dap` 是独立 Debug Adapter Protocol server 入口，通过 stdin/stdout 接收 `Content-Length` frame。

```bash
./build/dev/src/tooling/dap/ahfl-dap --help
```

当前入口已经能响应现有库级 DAP handler，但 runtime state、capability breakpoint 和真实 step execution 仍是后续产品化工作。

## Incremental

`ahfl-incremental` 接收一组变更源文件，运行 incremental compiler 库并输出模块状态和 cache 统计。

```bash
./build/dev/src/tooling/incremental/ahfl-incremental \
  tests/golden/ir/ok_workflow_value_flow.ahfl
```

当前入口使用进程内 cache，不读取 project/workspace descriptor；跨文件 import graph、持久缓存和 daemon invalidation contract 仍是后续工作。

## Profiling

`--time-passes` 用于检查 `-O` 启用的 Semantic IR pass pipeline 耗时。`--smv-size-report` 用于检查 `emit smv` 产物的 byte、line、LTLSPEC 数量。`--trace-export`、`--metrics-export` 和 `--structured-log` 用于把 CLI command span、duration、exit_code 和 completion log 写入 JSONL 文件。`--memory-report` 用于把 source、TypedProgram、IR 规模和结构性 memory proxy 写入 JSON 文件。stdout 仍保留原命令 artifact。

```bash
./build/dev/src/tooling/cli/ahflc emit summary -O --time-passes \
  examples/refund_audit_core_v0_1.ahfl

./build/dev/src/tooling/cli/ahflc emit smv --smv-size-report \
  examples/refund_audit_core_v0_1.ahfl

./build/dev/src/tooling/cli/ahflc emit summary \
  --trace-export trace.jsonl \
  --metrics-export metrics.jsonl \
  --structured-log ahflc.jsonl \
  --memory-report memory.json \
  examples/refund_audit_core_v0_1.ahfl
```

当前 memory report 是结构性 proxy，不是平台 RSS / allocator 观测；trace/metrics/logging 目前是 CLI command 级，pass-level event schema 仍是后续工作。

## Core artifact

`ahflc emit <artifact>` 支持的常用 core artifact：

| Artifact | 用途 |
|----------|------|
| `ir` | 文本 IR |
| `ir-json` | JSON IR |
| `native-json` | Native backend JSON |
| `execution-plan` | Workflow 执行计划 |
| `runtime-session` | Runtime session snapshot |
| `dry-run-trace` | Mock dry-run 执行跟踪 |
| `execution-journal` | 执行日志 |
| `replay-view` | 回放视图 |
| `scheduler-snapshot` | 调度器状态 |
| `scheduler-review` | 调度器审查输出 |
| `checkpoint-record` | 检查点记录 |
| `checkpoint-review` | 检查点审查 |
| `persistence-descriptor` | 持久化层描述 |
| `persistence-review` | 持久化审查 |
| `export-manifest` | 持久化导出清单 |
| `export-review` | 导出审查 |
| `store-import-descriptor` | Store import 描述 |
| `store-import-review` | Store import 审查 |
| `package-review` | Package reader 与 execution planner 审查 |
| `summary` | 人类可读摘要 |
| `smv` | NuSMV 模型 |
| `assurance-json` | Assurance bundle |

最小示例：

```bash
./build/dev/src/tooling/cli/ahflc emit summary \
  examples/refund_audit_core_v0_1.ahfl

./build/dev/src/tooling/cli/ahflc emit ir-json \
  examples/refund_audit_core_v0_1.ahfl

./build/dev/src/tooling/cli/ahflc emit smv \
  examples/refund_audit_core_v0_1.ahfl
```

带 package 的示例：

```bash
./build/dev/src/tooling/cli/ahflc emit package-review \
  --project tests/integration/workflow_value_flow/ahfl.project.json \
  --package tests/integration/workflow_value_flow/ahfl.package.json
```

## Store pipeline artifact

Store pipeline 使用 `store/...` artifact id：

| Artifact | 用途 |
|----------|------|
| `store/request` | Durable store import request |
| `store/review` | Import request review |
| `store/decision` | Adapter decision |
| `store/receipt` | Adapter receipt |
| `store/receipt-persistence-request` | Receipt persistence request |
| `store/decision-review` | Decision review |
| `store/receipt-review` | Receipt review |
| `store/receipt-persistence-review` | Receipt persistence review |
| `store/receipt-persistence-response` | Receipt persistence response |
| `store/receipt-persistence-response-review` | Receipt persistence response review |
| `store/adapter-execution` | Adapter execution projection |
| `store/recovery-preview` | Recovery preview |

示例：

```bash
./build/dev/src/tooling/cli/ahflc emit store/request \
  --project tests/integration/workflow_value_flow/ahfl.project.json \
  --package tests/integration/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/golden/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.ok \
  --run-id docs-guide-run
```

## Provider diagnostic artifact

Provider artifact 不走 `ahflc emit provider/...` 用户命令面。需要诊断 Provider pipeline 时，使用内部入口：

```bash
./build/dev/src/tooling/cli/ahflc emit-provider-artifact provider/production-readiness-report \
  --project tests/integration/workflow_value_flow/ahfl.project.json \
  --package tests/integration/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/golden/dry_run/project_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.ok \
  --run-id docs-guide-run
```

查看内部 artifact：

```bash
./build/dev/src/tooling/cli/ahflc --show-hidden --help
```

普通用户应优先消费 public provider artifact，例如 production readiness、compatibility、conformance、schema compatibility、runtime policy 和 integration dry run 报告；internal artifact 主要用于 golden、诊断和回归定位。

## 诊断顺序

推荐从低层到高层排查：

1. `ahflc check`：先确认源码能过 parse / resolve / typecheck / validate。
2. `ahflc dump ast`：源码结构是否按预期解析。
3. `ahflc dump types`：类型和路径是否解析到预期声明。
4. `ahflc dump project`：多文件项目是否装载了正确 source graph。
5. `ahflc emit summary`：IR 降级后的声明数量、flow summary、workflow summary 是否合理。
6. `ahflc emit package-review`：package entry、exports、capability binding 是否正确。
7. `ahflc emit dry-run-trace`：DAG 执行顺序和 mock capability 是否正确。
8. `ahflc validate` / `ahflc verify`：进入 assurance 和 formal 门禁。

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功 |
| `1` | 编译、验证、runtime 或 checker 失败 |
| `2` | 命令行参数非法 |
| `3` | 内部错误 |

CI 中应把 `check`、关键 `emit`、`validate`、`verify` 和必要 provider evidence 命令分开运行，避免一个大命令掩盖具体失败层。
