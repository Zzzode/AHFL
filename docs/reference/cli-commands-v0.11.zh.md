# AHFL CLI Commands V0.11

本文是 `docs/reference` 中 CLI 命令参考的合并入口，统一覆盖原 `cli-commands-v0.2`、`v0.3`、`v0.5`、`v0.10` 与 `v0.11` 文档。当前维护口径以 V0.11 为准；旧版本仅作为演进背景纳入摘要，不再保留独立入口。

关联文档：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [durable-store-import-reference-v0.1.zh.md](./durable-store-import-reference-v0.1.zh.md)
- [migration-policy-v0.1.zh.md](./migration-policy-v0.1.zh.md)
- [contributor-guide-v0.14.zh.md](./contributor-guide-v0.14.zh.md)

## 合并范围

| 历史版本 | 合并后保留的信息 |
|----------|------------------|
| V0.2 | core check / dump / emit / formal 命令族、基础输入模式、退出码。 |
| V0.3 | project-aware 输入、`emit-native-json`、project/workspace 命令形态。 |
| V0.5 | package authoring、`emit-package-review`、native package 路径。 |
| V0.10 | runtime session / journal / replay / scheduler / persistence / store import 输出链路。 |
| V0.11 | Provider artifact 可见性、声明式选项表、`CliDriver`、结构化 diagnostics 与 `run` 入口。 |

## 当前口径摘要

1. `ahflc` 是唯一用户态 CLI 入口；真实 workflow 执行统一进入 `ahflc run`。
2. project 输入统一支持 `<input.ahfl>`、`--search-root`、`--project`、`--workspace --project-name`。
3. package authoring 仍通过独立 `--package <ahfl.package.json>` 输入进入 native package / review / runtime-adjacent artifact。
4. Provider diagnostic artifact 使用内部入口 `emit-provider-artifact provider/<artifact>`；Internal artifact 必须显式传入 `--show-hidden`。
5. 退出码稳定为 `0` 成功、`1` 编译/验证/runtime 错误、`2` 参数错误、`3` 内部错误。
6. 新增 CLI 选项必须维护 `OptionSpec` 声明式选项表，不再扩散手写解析逻辑。

## 总览

项目输入模式：

```text
<input-mode> ::=
    <input.ahfl>
  | [--search-root <dir>]... <input.ahfl>
  | --project <ahfl.project.json>
  | --workspace <ahfl.workspace.json> --project-name <name>
```

核心命令：

```text
ahflc check <input-mode>
ahflc run --workflow <name> --input '<json>' [--llm-config <path>] <input-mode>
ahflc dump-ast <input-mode>
ahflc dump-types <input-mode>
ahflc dump-project <input-mode>
ahflc emit-ir <input-mode>
ahflc emit-ir-json <input-mode>
ahflc emit-native-json [--package <ahfl.package.json>] <input-mode>
ahflc emit-execution-plan [--package <ahfl.package.json>] <input-mode>
ahflc emit-runtime-session --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-execution-journal --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-replay-view --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-scheduler-snapshot --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-scheduler-review --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-audit-report --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-dry-run-trace --package ... --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-package-review [--package <ahfl.package.json>] <input-mode>
ahflc emit-summary <input-mode>
ahflc emit-smv <input-mode>
ahflc emit-assurance-json [--package <ahfl.package.json>] <input-mode>
ahflc validate-assurance [--package <ahfl.package.json>] <input-mode>
ahflc verify-formal [--model-checker <path>] [--formal-model-out <model.smv>] [--package ...] <input-mode>
```

## 选项

### 输入选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--project` | `<ahfl.project.json>` | 单个 project descriptor 作为输入 |
| `--workspace` | `<ahfl.workspace.json>` | workspace descriptor 作为输入 |
| `--project-name` | `<name>` | 与 `--workspace` 配合选择目标 project |
| `--search-root` | `<dir>` | 额外搜索根目录 (可重复) |
| `--package` | `<ahfl.package.json>` | package authoring descriptor |
| `--capability-mocks` | `<mocks.json>` | deterministic capability mock 输入 |
| `--input-fixture` | `<fixture>` | package fixture request 选择 |
| `--run-id` | `<id>` | 稳定 run identity |
| `--workflow` | `<canonical>` | 显式选择目标 workflow |
| `--input` | `<json>` | `run` 命令的真实 runtime 输入 JSON |
| `--llm-config` | `<path>` | `run` 命令的 LLM Provider 配置，默认 `~/.ahfl/llm_config.json` |

### 输出控制选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--model-checker` | `<path>` | 仅 `verify-formal`: NuSMV/nuXmv checker 路径 |
| `--formal-model-out` | `<model.smv>` | 仅 `verify-formal`: 保留 SMV 模型文件 |
| `--dump-ast` | - | 输出 AST outline |
| `--dump-types` | - | 输出类型环境 |
| `--explain` | - | 启用结构化解释 |
| `-O` / `--optimize` | - | 启用优化 passes |

### V0.11 新增选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--show-hidden` | - | 在 help 中显示 Internal provider diagnostic artifact，并允许 `emit-provider-artifact` 解析 Internal artifact |
| `--help` / `-h` | - | 显示用法帮助 (可组合 `--show-hidden` 查看完整清单) |

## Provider Artifact 可见性

V0.11 将 62 个 provider artifact descriptor 分为两个可见性层级。Provider artifact 不再作为独立 `CommandKind` 暴露，也不再挂到用户态 `ahflc emit <artifact>` 命令面；CLI 只保留内部诊断入口 `ahflc emit-provider-artifact provider/<artifact>`：

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
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial

# 执行 Internal provider diagnostic artifact
ahflc emit-provider-artifact provider/runtime-preflight --show-hidden \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
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
    std::string_view long_name;     // "--project"
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
