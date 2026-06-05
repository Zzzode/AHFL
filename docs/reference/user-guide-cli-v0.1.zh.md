# AHFL CLI 工作流 V0.1

本文说明当前 `ahflc` 命令面、输入模式、常用 artifact 和诊断方式。历史版本命令参考见 [cli-commands-v0.11.zh.md](./cli-commands-v0.11.zh.md)，但本文以当前构建产物的 `ahflc --help` 为准。

## 构建

```bash
cmake --preset dev
cmake --build --preset build-dev
```

本文示例默认使用：

```bash
./build/dev/src/tooling/cli/ahflc
```

## 命令形态

当前 CLI 的主命令形态是：

```text
ahflc check [options] <input.ahfl>
ahflc run --workflow <name> --input '<json>' [options] <input.ahfl>
ahflc emit <artifact> [options] <input.ahfl>
ahflc dump <target> [options] <input.ahfl>
ahflc verify [options] <input.ahfl>
ahflc validate [options] <input.ahfl>
```

核心动作：

| 动作 | 用途 | 示例 |
|------|------|------|
| `check` | 解析、命名解析、类型检查、结构验证 | `ahflc check examples/refund_audit_core_v0_1.ahfl` |
| `dump ast` | 输出 AST outline | `ahflc dump ast <input>` |
| `dump types` | 输出类型环境 | `ahflc dump types <input>` |
| `dump project` | 输出 project source graph | `ahflc dump project --project <manifest>` |
| `emit <artifact>` | 输出编译、执行、审计或持久化 artifact | `ahflc emit summary <input>` |
| `validate` | 运行 assurance production gate | `ahflc validate <input>` |
| `verify` | 调用 SMV 兼容 checker 做 formal verification | `ahflc verify --model-checker <path> <input>` |
| `run` | 使用 LLM Provider 执行 workflow | `ahflc run --workflow <name> --input '<json>' <input>` |

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
  "format_version": "ahfl.project.v0.3",
  "name": "workflow-value-flow",
  "search_roots": ["."],
  "entry_sources": ["app/main.ahfl"]
}
```

Workspace descriptor 示例：

```json
{
  "format_version": "ahfl.workspace.v0.3",
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
| `--capability-mocks <path>` | 为 dry run / runtime artifact 提供 mock capability 输入 |
| `--input-fixture <fixture>` | 选择 runtime fixture request |
| `--run-id <id>` | 固定 run identity，便于稳定输出 |
| `--workflow <canonical>` | 选择目标 workflow |
| `--input '<json>'` | `run` 的真实 runtime 输入 JSON |
| `--llm-config <path>` | `run` 的 LLM Provider 配置，默认 `~/.ahfl/llm_config.json` |
| `--model-checker <path>` | `verify` 使用的 NuSMV / nuXmv 可执行文件 |
| `--formal-model-out <path>` | `verify` 保留生成的 SMV 模型 |
| `--show-hidden` | 显示或启用内部诊断 artifact |
| `-O` | 启用优化 passes |
| `-h` / `--help` | 查看帮助 |

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
