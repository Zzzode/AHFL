# AHFL CLI Commands V0.11

本文给出 `ahflc` 在 V0.11 阶段的命令参考，在 V0.10 基础上新增 Provider Artifact 可见性分层 (`--emit-internal`) 与声明式选项表。

关联文档：

- [cli-commands-v0.10.zh.md](./cli-commands-v0.10.zh.md)
- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [durable-store-import-reference-v0.1.zh.md](./durable-store-import-reference-v0.1.zh.md)
- [contributor-guide-v0.14.zh.md](./contributor-guide-v0.14.zh.md)

## V0.11 变更摘要

1. **Provider Artifact 可见性分层** — 62 个 provider 命令分为 17 个 Public 和 45 个 Internal。默认 `--help` 仅显示 Public 命令。
2. **`--emit-internal` 选项** — 在 help 中显示所有 Internal provider 命令，同时允许直接执行 Internal 命令。
3. **声明式选项表** — 所有 CLI 选项统一通过 `OptionSpec` 表驱动解析，替代手写 if/else。
4. **`CliDriver` 封装** — `ahflc.cpp` 瘦身为 ~10 行，所有逻辑移入 `CliDriver` 类。
5. **`ExitCode` 枚举** — 退出码语义不变 (0/1/2)，新增 `InternalError = 3`。
6. **`DiagnosticConsumer` 抽象** — 支持 `--diagnostics-format=text|json` (默认 text，行为不变)。

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
| `--emit-internal` | - | 在 help 中显示 Internal provider 命令，并允许直接执行 |
| `--help` / `-h` | - | 显示用法帮助 (可组合 `--emit-internal` 查看完整清单) |

## Provider Artifact 可见性

V0.11 将 62 个 `emit-durable-store-import-provider-*` 命令分为两个可见性层级：

- **Public (17 个)** — 代表用户可直接消费的终端 artifact，默认在 `--help` 中显示。
- **Internal (45 个)** — 代表 pipeline 内部中间节点，仅在 `--emit-internal` 模式下显示。

Internal 命令仍可正常执行（不阻塞），`--emit-internal` 仅控制 help 显示。

### Public Provider 命令 (17)

以下命令代表 provider pipeline 的用户可交付产物：

| 命令 | 用途 |
|------|------|
| `emit-durable-store-import-provider-write-attempt` | 写入尝试预览 |
| `emit-durable-store-import-provider-driver-binding` | 驱动绑定计划 |
| `emit-durable-store-import-provider-sdk-mock-adapter-execution` | SDK mock 适配器执行结果 |
| `emit-durable-store-import-provider-write-recovery-plan` | 写入恢复计划 |
| `emit-durable-store-import-provider-failure-taxonomy-report` | 故障分类报告 |
| `emit-durable-store-import-provider-compatibility-report` | 兼容性报告 |
| `emit-durable-store-import-provider-registry` | Provider 注册表 |
| `emit-durable-store-import-provider-selection-plan` | Provider 选择计划 |
| `emit-durable-store-import-provider-production-readiness-evidence` | 生产就绪证据 |
| `emit-durable-store-import-provider-production-readiness-report` | 生产就绪报告 |
| `emit-durable-store-import-provider-conformance-report` | 一致性报告 |
| `emit-durable-store-import-provider-schema-compatibility-report` | Schema 兼容性报告 |
| `emit-durable-store-import-provider-config-bundle-validation-report` | 配置包验证报告 |
| `emit-durable-store-import-provider-release-evidence-archive-manifest` | 发布证据归档清单 |
| `emit-durable-store-import-provider-approval-receipt` | 审批回执 |
| `emit-durable-store-import-provider-runtime-policy-report` | 运行时策略报告 |
| `emit-durable-store-import-provider-production-integration-dry-run-report` | 生产集成干跑报告 |

### Internal Provider 命令 (45)

Internal 命令是 pipeline DAG 的中间节点，通常不直接消费。使用 `--emit-internal --help` 查看完整列表。

典型 Internal 命令示例：

- `emit-durable-store-import-provider-recovery-handoff` — 恢复交接预览
- `emit-durable-store-import-provider-runtime-preflight` — 运行时预检计划
- `emit-durable-store-import-provider-sdk-envelope` — SDK 请求封装计划
- `emit-durable-store-import-provider-config-load` — 配置加载计划
- `emit-durable-store-import-provider-secret-resolver-request` — 密钥解析请求
- `emit-durable-store-import-provider-telemetry-summary` — 遥测摘要

### 使用示例

```bash
# 查看默认帮助 (仅 Public 命令)
ahflc --help

# 查看完整帮助 (含 Internal 命令)
ahflc --emit-internal --help

# 执行 Public 命令
ahflc emit-durable-store-import-provider-write-attempt \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial

# 执行 Internal 命令 (无需 --emit-internal，直接执行即可)
ahflc emit-durable-store-import-provider-runtime-preflight \
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

### Provider Artifact 可见性声明

可见性在 `pipeline_durable_store_import_provider_artifacts.def` 的第 6 列声明：

```cpp
AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(
    WriteAttempt,                          // kind
    ProviderWriteAttemptPreview,           // artifact_type
    build_provider_write_attempt_for_cli,  // builder
    print_provider_write_attempt_preview,  // printer
    "emit-durable-store-import-provider-write-attempt",  // command_token
    Public)                                // visibility: Public | Internal
```

### 泛型 Provider Step 宏

`provider_step_generic.hpp` 提供 `AHFL_DEFINE_PROVIDER_STEP_1` / `AHFL_DEFINE_PROVIDER_STEP_2` 宏，为单/双依赖 builder 自动生成 null-check → invoke → render → extract 模式，消除 ~800 行重复代码。

## 最小验证建议

```bash
# 全量测试 (872 tests)
ctest --preset test-dev --output-on-failure

# 仅 CLI 相关
ctest --preset test-dev --output-on-failure -R "cli"
```
