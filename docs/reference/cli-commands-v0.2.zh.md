# AHFL Core V0.2 CLI Commands

本文给出 `ahflc` 在 V0.2 阶段的命令参考，重点覆盖 project-aware 用法和当前限制。

关联文档：

- [project-usage-v0.2.zh.md](./project-usage-v0.2.zh.md)
- [ir-format-v0.2.zh.md](./ir-format-v0.2.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)

## 总览

```text
ahflc check [--search-root <dir>]... [--dump-ast] <input.ahfl>
ahflc dump-ast [--search-root <dir>]... <input.ahfl>
ahflc dump-types [--search-root <dir>]... <input.ahfl>
ahflc dump-project [--search-root <dir>]... <entry.ahfl>
ahflc emit-ir [--search-root <dir>]... <input.ahfl>
ahflc emit-ir-json [--search-root <dir>]... <input.ahfl>
ahflc emit-summary [--search-root <dir>]... <input.ahfl>
ahflc emit-smv [--search-root <dir>]... <input.ahfl>
```

## 选项

- `--search-root <dir>`
  - 开启 project-aware 模式，并把 `<dir>` 加入 module 解析根目录。
  - 可重复传入多个 root。
- `--dump-ast`
  - 输出 hand-written AST 树。
  - 在 project-aware 模式下会输出按 source/module 分组的 AST 视图。

## 命令说明

### `check`

用途：

- 跑 parse + AST lowering + resolve + typecheck + validate 全链路。

单文件示例：

```bash
./build/dev/src/cli/ahflc check examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc check \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

成功时会输出 summary，包括 source / symbol / reference / named type 统计。

### `dump-ast`

用途：

- 输出完整 AST 树，便于调试 parser/lowering。

示例：

```bash
./build/dev/src/cli/ahflc dump-ast examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc dump-ast \
  --project tests/project/check_ok/ahfl.project.json
```

输出边界：

1. 单文件模式输出单个 `program` AST。
2. project-aware 模式输出 `project_ast -> source -> module -> program` 视图。
3. `dump-ast` 仍停在 frontend/lowering 边界，不进入 resolve / typecheck / validate。

### `dump-types`

用途：

- 输出 type environment 和语义签名。

单文件示例：

```bash
./build/dev/src/cli/ahflc dump-types examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

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
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

### `emit-ir`

用途：

- 输出稳定文本 IR。

单文件示例：

```bash
./build/dev/src/cli/ahflc emit-ir examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc emit-ir \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

project-aware 模式下，文本 IR 会为每个 declaration 输出：

- `@provenance(module=..., source=...)`

### `emit-ir-json`

用途：

- 输出稳定 JSON IR。

单文件示例：

```bash
./build/dev/src/cli/ahflc emit-ir-json examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc emit-ir-json \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

project-aware 模式下，JSON IR 的每个 declaration 会携带：

```json
{
  "provenance": {
    "module_name": "app::main",
    "source_path": "app/main.ahfl"
  }
}
```

### `emit-summary`

用途：

- 输出一个 capability-oriented summary backend。
- 适合快速查看某个 program 是否进入了 provenance、formal observations、flow summary、workflow summary 等边界。

单文件示例：

```bash
./build/dev/src/cli/ahflc emit-summary tests/ir/ok_workflow_value_flow.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc emit-summary \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

输出边界：

1. 它不是新的 IR family，仍共享 `ahfl.ir.v1`。
2. 它不输出完整 declaration/expr tree，而是输出稳定汇总。
3. 它当前作为参考 backend，用来验证 backend extension path。

### `emit-smv`

用途：

- 输出受限 formal backend 的 SMV。

单文件示例：

```bash
./build/dev/src/cli/ahflc emit-smv examples/refund_audit_core_v0_1.ahfl
```

project-aware 示例：

```bash
./build/dev/src/cli/ahflc emit-smv \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

当前 formal backend 仍然是 restricted subset，详细边界见 [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)。

## project-aware 支持矩阵

| 命令 | 是否支持 `--search-root` | 说明 |
|------|-------------------------|------|
| `check` | 是 | 全链路 project-aware 检查 |
| `dump-ast` | 是 | 输出 project-aware AST 调试视图 |
| `dump-types` | 是 | 输出合并后的语义环境 |
| `dump-project` | 是 | 只输出 source graph |
| `emit-ir` | 是 | 输出 project-aware 文本 IR |
| `emit-ir-json` | 是 | 输出 project-aware JSON IR |
| `emit-summary` | 是 | 输出 capability-oriented backend summary |
| `emit-smv` | 是 | 输出 project-aware restricted SMV |

## 退出码

- `0`
  - 成功，或按预期输出 dump/emission 结果。
- `1`
  - 进入编译主链路后出现 parse / resolve / typecheck / validate 错误。
- `2`
  - 命令行参数非法，例如未知选项、缺少参数、动作冲突。

## 约束与建议

1. `--search-root` 只影响 project-aware loader，不会改变语义阶段的 canonical naming 规则。
2. backend 命令都会先经过 `validate`；不会直接序列化 parse tree。
3. 若你在 automation 中消费 CLI，建议优先用 `emit-ir-json`，因为文本 IR 更适合人读和 golden diff。
4. 若你只想确认 loader 结果，不要直接跑 `check`，优先用 `dump-project`。
