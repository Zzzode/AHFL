# AHFL Core V0.5 Project Usage

本文给出 AHFL Core V0.5 的 project-aware 用法参考，重点覆盖 `ahfl.project.json` / `ahfl.workspace.json` 的输入模式、`ahfl.package.json` 的 authoring 输入，以及“author metadata -> emit package -> review / consume”这条完整路径。

关联文档：

- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](./native-package-authoring-compatibility-v0.5.zh.md)

## 术语

- `project manifest`
  - `ahfl.project.json`，声明 entry source、search roots 与项目名称。
- `workspace descriptor`
  - `ahfl.workspace.json`，声明一组 project manifest，并通过 `--project-name` 选择其中一个 project。
- `package authoring descriptor`
  - `ahfl.package.json`，声明 package identity、entry target、export targets 与 capability binding key。
- `source graph`
  - 由 entry source、递归装载到的 source、以及 import edge 组成的稳定前端输入模型。
- `handoff package`
  - compiler 输出给 runtime-adjacent consumer 的稳定 Native package 数据模型。

## 输入边界

V0.5 当前把“项目输入”和“package authoring 输入”明确拆成两层：

```text
project/workspace/search-root input
  -> ProjectInput
  -> SourceGraph
  -> resolve/typecheck/validate
  -> optional package authoring normalization
  -> handoff::Package
  -> review / consumer bootstrap
```

其中：

1. `ahfl.project.json` / `ahfl.workspace.json` 负责描述编译输入与 source graph 装载。
2. `ahfl.package.json` 负责描述 package metadata authoring。
3. `ahfl.package.json` 当前不会内联进 project 或 workspace descriptor。
4. 只有 `emit-native-json` 与 `emit-package-review` 会消费 `--package`。

## 最小目录约定

典型的 V0.5 project 目录可以像这样组织：

```text
tests/project/workflow_value_flow/
  ahfl.project.json
  ahfl.package.json
  ahfl.display.package.json
  app/main.ahfl
  lib/types.ahfl
  lib/agents.ahfl

tests/project/
  handoff.workspace.json
```

最小 `ahfl.project.json` 形态：

```json
{
  "format_version": "ahfl.project.v0.3",
  "name": "workflow-value-flow",
  "entry_sources": ["app/main.ahfl"],
  "search_roots": ["."]
}
```

最小 `ahfl.package.json` 形态：

```json
{
  "format_version": "ahfl.package-authoring.v0.5",
  "package": {
    "name": "workflow-value-flow",
    "version": "0.2.0"
  },
  "entry": {
    "kind": "workflow",
    "name": "app::main::ValueFlowWorkflow"
  },
  "exports": [
    {
      "kind": "workflow",
      "name": "app::main::ValueFlowWorkflow"
    },
    {
      "kind": "agent",
      "name": "lib::agents::AliasAgent"
    }
  ],
  "capability_bindings": [
    {
      "capability": "lib::agents::Echo",
      "binding_key": "runtime.echo"
    }
  ]
}
```

最小 `ahfl.workspace.json` 形态：

```json
{
  "format_version": "ahfl.workspace.v0.3",
  "name": "handoff-workspace",
  "projects": ["workflow_value_flow/ahfl.project.json"]
}
```

## 三种项目输入模式

V0.5 仍保留三条等价的 project-aware 输入路径：

1. `--search-root ... <entry.ahfl>`
   - 适合最小复现和快速实验。
2. `--project <ahfl.project.json>`
   - 适合稳定项目与文档化命令。
3. `--workspace <ahfl.workspace.json> --project-name <name>`
   - 适合多 project 工作区。

这三条路径最终都会进入同一条 compiler 主链路；差异只在 source graph 的定位方式，不在后续 handoff lowering 或 review / consumer bootstrap 语义。

## 典型 V0.5 路径

### 1. 只验证 project-aware 输入

```bash
./build/dev/src/cli/ahflc check \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

### 2. 发射带 package authoring 的 native package

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
```

### 3. 用 display name 做 authoring review

```bash
./build/dev/src/cli/ahflc emit-package-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.display.package.json
```

### 4. 进入 direct reference consumer helper

```text
load project/workspace
  -> parse ahfl.package.json
  -> handoff::lower_package(...)
  -> build_package_reader_summary(...)
  -> build_execution_planner_bootstrap(...)
```

## 支持矩阵

| 命令 | `--search-root` | `--project` | `--workspace --project-name` | `--package` |
| --- | --- | --- | --- | --- |
| `check` | 是 | 是 | 是 | 否 |
| `dump-ast` | 是 | 是 | 是 | 否 |
| `dump-project` | 是 | 是 | 是 | 否 |
| `dump-types` | 是 | 是 | 是 | 否 |
| `emit-ir` | 是 | 是 | 是 | 否 |
| `emit-ir-json` | 是 | 是 | 是 | 否 |
| `emit-native-json` | 是 | 是 | 是 | 是 |
| `emit-package-review` | 是 | 是 | 是 | 是 |
| `emit-summary` | 是 | 是 | 是 | 否 |
| `emit-smv` | 是 | 是 | 是 | 否 |

## 常见失败场景

- `unsupported package authoring descriptor format_version '...'`
  - `ahfl.package.json` 的版本标识不匹配当前 parser。
- `package entry target '...' is not a workflow target`
  - `entry` 最终规范化到了错误的 executable kind，无法作为 execution planner bootstrap 入口。
- `package authoring field 'export_targets' refers to '...' with wrong executable kind`
  - `exports[]` 的 `kind` 与实际解析到的 executable target 类型不一致。
- `unknown package authoring capability '...'`
  - capability 引用不存在，或 display name 无法唯一规范化到 canonical name。
- `workspace does not contain project named '...'`
  - workspace 中不存在传入的 `--project-name`。
- `descriptor field 'search_roots' must not escape the descriptor root`
  - project / workspace descriptor 中的相对路径越过 descriptor 根目录。

这些错误的正式回归样例位于 `tests/project/`、`tests/native/`、`tests/review/` 与 `tests/handoff/`。

## 最小验证建议

只验证 V0.5 package authoring 路径时，建议至少跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.5-package-authoring-model
ctest --preset test-dev --output-on-failure -L v0.5-package-authoring-validation
ctest --preset test-dev --output-on-failure -L v0.5-package-review
ctest --preset test-dev --output-on-failure -L v0.5-reference-consumer
```

## 对贡献者的含义

1. project/workspace descriptor 与 `ahfl.package.json` 是两条独立输入边界，不能互相偷带字段。
2. package authoring 的 display name 只是输入便利层；进入 handoff package 前必须规范化到 canonical name。
3. review/debug 与 direct consumer helper 都必须复用 `handoff::Package`，不能回退去扫 raw JSON 或 AST。
4. 改 project-aware 输入模式时，要同步更新：
   - `docs/design/project-descriptor-architecture-v0.3.zh.md`
   - `docs/reference/cli-commands-v0.5.zh.md`
   - `tests/cmake/ProjectTests.cmake`
   - `tests/project/`
5. 改 package authoring 或 handoff usage 时，要同步更新：
   - `docs/reference/native-handoff-usage-v0.5.zh.md`
   - `docs/reference/native-package-authoring-compatibility-v0.5.zh.md`
   - `docs/reference/contributor-guide-v0.5.zh.md`

## 当前状态

截至当前实现：

1. V0.5 project-aware 输入路径仍复用 V0.3 的 project / workspace descriptor 形态。
2. `emit-native-json --package` 与 `emit-package-review --package` 已形成正式 authoring 入口。
3. 仓库内已存在从 package authoring、review 到 reference consumer bootstrap 的完整最小路径。
