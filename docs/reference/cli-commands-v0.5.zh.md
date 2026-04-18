# AHFL CLI Commands V0.5

本文给出 `ahflc` 在 V0.5 阶段的命令参考，重点覆盖 package authoring 输入、`emit-package-review` 调试面，以及当前 CLI 边界。

关联文档：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)

## 总览

项目输入模式：

```text
<input-mode> ::=
    <input.ahfl>
  | [--search-root <dir>]... <input.ahfl>
  | --project <ahfl.project.json>
  | --workspace <ahfl.workspace.json> --project-name <name>
```

命令：

```text
ahflc check <input-mode>
ahflc dump-ast <input-mode>
ahflc dump-types <input-mode>
ahflc dump-project <input-mode>
ahflc emit-ir <input-mode>
ahflc emit-ir-json <input-mode>
ahflc emit-native-json [--package <ahfl.package.json>] <input-mode>
ahflc emit-package-review [--package <ahfl.package.json>] <input-mode>
ahflc emit-summary <input-mode>
ahflc emit-smv <input-mode>
```

## 选项

- `--search-root <dir>`
  - 开启 project-aware loader 路径。
- `--project <ahfl.project.json>`
  - 使用单个 project descriptor 作为输入。
- `--workspace <ahfl.workspace.json>`
  - 使用 workspace descriptor 作为输入。
- `--project-name <name>`
  - 与 `--workspace` 配合使用，选择目标 project。
- `--package <ahfl.package.json>`
  - 提供 package authoring descriptor。
  - 当前只允许与 `emit-native-json` 或 `emit-package-review` 组合。

## 重点命令

### `emit-native-json`

用途：

1. 输出 runtime-facing Native handoff package JSON。
2. 若提供 `--package`，会先做 package authoring descriptor 解析与 semantic normalization。
3. semantic normalization 会把 display name 规范化为 canonical name，并拒绝 wrong kind、unknown target / capability 与规范化后的重复项。

示例：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
```

### `emit-package-review`

用途：

1. 输出 package-aware review/debug 摘要。
2. 同时覆盖 package reader 视角与 workflow execution planner bootstrap 视角。
3. 显式呈现 package identity、entry/export target、capability binding、policy / observation 摘要与 workflow execution graph。

当前边界：

1. 输出基于 `handoff::Package` 的稳定 review 视图，而不是原始 JSON diff。
2. 若提供 `--package`，会复用 `emit-native-json` 相同的 package authoring 校验与规范化流程。
3. 输出用于 authoring review、consumer smoke test 与 planner bootstrap 预检，不承诺 runtime schedule、connector 或 deployment 语义。

示例：

```bash
./build/dev/src/cli/ahflc emit-package-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.display.package.json
```

## project-aware 支持矩阵

| 命令 | `--search-root` | `--project` | `--workspace --project-name` |
|------|-----------------|-------------|-------------------------------|
| `check` | 是 | 是 | 是 |
| `dump-ast` | 是 | 是 | 是 |
| `dump-types` | 是 | 是 | 是 |
| `dump-project` | 是 | 是 | 是 |
| `emit-ir` | 是 | 是 | 是 |
| `emit-ir-json` | 是 | 是 | 是 |
| `emit-native-json` | 是 | 是 | 是 |
| `emit-package-review` | 是 | 是 | 是 |
| `emit-summary` | 是 | 是 | 是 |
| `emit-smv` | 是 | 是 | 是 |

## 退出码

- `0`
  - 成功，或按预期输出 dump / emission 结果。
- `1`
  - 进入编译主链路后出现 parse / resolve / typecheck / validate / package metadata validation 错误。
- `2`
  - 命令行参数非法，例如未知选项、缺少参数、动作冲突，或 `--package` 与不支持的子命令组合。
