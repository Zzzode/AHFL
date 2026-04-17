# AHFL Core V0.3 CLI Commands

本文给出 `ahflc` 在 V0.3 阶段的命令参考，重点覆盖 descriptor 驱动 project-aware 入口、`emit-summary` 参考 backend，以及当前 CLI 边界。

关联文档：

- [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)

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
ahflc emit-native-json <input-mode>
ahflc emit-summary <input-mode>
ahflc emit-smv <input-mode>
```

## 选项

- `--search-root <dir>`
  - 开启 V0.2 兼容的 project-aware loader 路径。
  - 可重复传入多个 root。
- `--project <ahfl.project.json>`
  - 使用单个 project manifest 作为输入。
- `--workspace <ahfl.workspace.json>`
  - 使用 workspace descriptor 作为输入。
- `--project-name <name>`
  - 与 `--workspace` 配合使用，选择目标 project。
- `--dump-ast`
  - `check` 的兼容开关；建议新用法直接使用 `dump-ast` 子命令。

## 命令说明

### `check`

用途：

- 跑 parse + AST lowering + resolve + typecheck + validate 全链路。

示例：

```bash
./build/dev/src/cli/ahflc check --project tests/project/check_ok/ahfl.project.json
```

### `dump-ast`

用途：

- 输出 hand-written AST。
- 在 project-aware 模式下输出 `project_ast -> source -> module -> program` 视图。

示例：

```bash
./build/dev/src/cli/ahflc dump-ast \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

### `dump-types`

用途：

- 输出合并后的语义环境和签名。

示例：

```bash
./build/dev/src/cli/ahflc dump-types \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

### `dump-project`

用途：

- 只运行 project-aware 装载，输出 source graph 概要。
- 不进入 resolve / typecheck / validate / backend。

示例：

```bash
./build/dev/src/cli/ahflc dump-project \
  --project tests/project/check_ok/ahfl.project.json
```

### `emit-ir`

用途：

- 输出稳定文本 IR。
- project-aware 模式下，为每个 declaration 输出 provenance。

示例：

```bash
./build/dev/src/cli/ahflc emit-ir \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

### `emit-ir-json`

用途：

- 输出稳定 JSON IR。
- 机器消费应优先使用该命令。

示例：

```bash
./build/dev/src/cli/ahflc emit-ir-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

### `emit-native-json`

用途：

- 输出 V0.4 Native handoff package JSON。
- 适合 runtime-facing consumer、golden diff 和 handoff boundary 审查。

当前边界：

1. 基于 validate 后的统一 backend 路径
2. 支持单文件、`--search-root`、`--project`、`--workspace --project-name`
3. 当前会显式导出 `execution_graph` 与每个 workflow node 的最小 `lifecycle` 摘要
4. 当前会显式导出 package 级 `policy_obligations`，并保留 capability dependency 与 workflow value summary
5. 当前不会猜 package identity / entry / export 默认值；若未提供 metadata 输入来源，会输出空 metadata 区段

示例：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

更多 V0.4 handoff 用法与验证建议见：

- [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)

### `emit-summary`

用途：

- 输出 capability-oriented summary backend。
- 适合快速查看 program 是否进入 provenance、flow summary、workflow value summary 和 `formal_observations` 边界。

示例：

```bash
./build/dev/src/cli/ahflc emit-summary tests/ir/ok_workflow_value_flow.ahfl
```

### `emit-smv`

用途：

- 输出 restricted formal backend。
- 当前只消费受限控制与 observation 语义，不代表完整执行语义。

示例：

```bash
./build/dev/src/cli/ahflc emit-smv \
  --project tests/project/check_ok/ahfl.project.json
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
| `emit-summary` | 是 | 是 | 是 |
| `emit-smv` | 是 | 是 | 是 |

## 退出码

- `0`
  - 成功，或按预期输出 dump/emission 结果。
- `1`
  - 进入编译主链路后出现 parse / resolve / typecheck / validate 错误。
- `2`
  - 命令行参数非法，例如未知选项、缺少参数、动作冲突、workspace 缺少 `--project-name`。

## 约束与建议

1. descriptor 只是 project input 的声明式入口，不会绕过现有 module loading 规则。
2. backend 命令都会先经过 `validate`；不会直接序列化 parse tree。
3. 若 automation 需要完整 compiler-facing 机器消费，优先使用 `emit-ir-json`。
4. 若 automation 需要 runtime-facing handoff package，优先使用 `emit-native-json`。
5. 若只想确认 loader 结果，不要直接跑 `check`，优先使用 `dump-project`。
6. 本地验证 handoff 导出时，优先跑：
   - `ctest --test-dir build/dev --output-on-failure -L ahfl-v0.4`
7. 本地验证 V0.3 基线时，优先跑：
   - `ctest --preset test-dev -L ahfl-v0.3`
