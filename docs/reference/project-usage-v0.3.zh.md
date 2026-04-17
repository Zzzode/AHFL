# AHFL Core V0.3 Project Usage

本文给出 AHFL Core V0.3 的 project-aware 用法参考，重点覆盖 `--search-root`、`--project`、`--workspace --project-name` 三种项目输入模式，以及 `dump-ast` / `emit-summary` 等 V0.3 新路径。

关联文档：

- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [source-graph-v0.2.zh.md](../design/source-graph-v0.2.zh.md)
- [module-loading-v0.2.zh.md](../design/module-loading-v0.2.zh.md)
- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)
- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)

## 术语

- `entry source`
  - 用户直接交给编译器的入口 `.ahfl` 源文件。
- `search root`
  - project-aware loader 用来解析 `module -> file` 的根目录集合。
- `project manifest`
  - `ahfl.project.json`，显式声明 entry source、search roots 和项目名称。
- `workspace descriptor`
  - `ahfl.workspace.json`，声明一组 project manifest，并通过 `--project-name` 选择其中一个 project。
- `source graph`
  - 由 entry source、递归装载到的 source、以及 import edge 组成的稳定前端输入模型。

## 三种项目输入模式

V0.3 当前支持三条等价但面向不同场景的项目输入路径：

1. `--search-root ... <entry.ahfl>`
   - V0.2 兼容入口。
   - 适合快速实验和最小复现。
2. `--project <ahfl.project.json>`
   - V0.3 正式 descriptor 入口。
   - 适合稳定项目、脚本和文档化用法。
3. `--workspace <ahfl.workspace.json> --project-name <name>`
   - V0.3 workspace 选择入口。
   - 适合 monorepo 或一组相关 project 共享同一工作区。

这三条路径最终都会下沉到同一条链路：

```text
manifest/workspace/search-root flags
  -> ProjectInput
  -> SourceGraph
  -> resolve/typecheck/validate
  -> IR/backends
```

## 最小目录与 descriptor 约定

最小 project 目录可以像这样组织：

```text
tests/project/check_ok/
  ahfl.project.json
  app/main.ahfl
  lib/types.ahfl
  lib/agents.ahfl

tests/project/
  ahfl.workspace.json
```

最小 `ahfl.project.json` 形态：

```json
{
  "format_version": "ahfl.project.v0.3",
  "name": "check-ok",
  "entry": "app/main.ahfl",
  "search_roots": ["."]
}
```

最小 `ahfl.workspace.json` 形态：

```json
{
  "format_version": "ahfl.workspace.v0.3",
  "name": "ahfl-tests",
  "projects": [
    {
      "name": "check-ok",
      "path": "check_ok/ahfl.project.json"
    }
  ]
}
```

descriptor 不会替换现有 module loading 规则；`module -> relative path` 规则仍保持不变。

## 常用命令

兼容 `--search-root` 入口：

```bash
./build/dev/src/cli/ahflc check \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

descriptor 驱动入口：

```bash
./build/dev/src/cli/ahflc dump-ast \
  --project tests/project/check_ok/ahfl.project.json
```

workspace 选择入口：

```bash
./build/dev/src/cli/ahflc emit-ir-json \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

查看 source graph：

```bash
./build/dev/src/cli/ahflc dump-project \
  --project tests/project/check_ok/ahfl.project.json
```

查看 capability-oriented backend summary：

```bash
./build/dev/src/cli/ahflc emit-summary \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

导出 Native handoff package：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

导出 restricted formal backend：

```bash
./build/dev/src/cli/ahflc emit-smv \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

## 支持矩阵

| 命令 | `--search-root` | `--project` | `--workspace --project-name` |
|------|-----------------|-------------|-------------------------------|
| `check` | 是 | 是 | 是 |
| `dump-ast` | 是 | 是 | 是 |
| `dump-project` | 是 | 是 | 是 |
| `dump-types` | 是 | 是 | 是 |
| `emit-ir` | 是 | 是 | 是 |
| `emit-ir-json` | 是 | 是 | 是 |
| `emit-native-json` | 是 | 是 | 是 |
| `emit-summary` | 是 | 是 | 是 |
| `emit-smv` | 是 | 是 | 是 |

## 常见失败场景

- `project descriptor requires non-empty 'search_roots'`
  - project manifest 缺失 `search_roots`，或字段为空。
- `workspace does not contain project named '...'`
  - workspace 中不存在传入的 `--project-name`。
- `workspace contains duplicate project name '...'`
  - workspace 中有多个 project manifest 使用同名 `name`，导致 `--project-name` 选择歧义。
- `descriptor field 'search_roots' must not escape the descriptor root`
  - manifest/workspace 中的相对路径越过了 descriptor 所在目录的规范化边界。
- module/file mismatch
  - 文件路径与 `module` 声明不一致。
- duplicate module owner
  - 多个文件声明了同一个 `module`。
- cross-file type mismatch
  - resolver 能解析目标声明，但 typecheck/validate 发现跨文件 schema 或 workflow 约束不匹配。

这些错误的正式回归样例位于 `tests/project/`，并进入了 `ctest --preset test-dev -L ahfl-v0.3`。

## 对下游与贡献者的含义

1. checker/backend 的稳定输入不再是假定“单文件程序”，而是 `SourceGraph`。
2. 如果工具链缓存 declaration 结果，缓存键至少应联合 canonical name 与 provenance。
3. handoff 导出也继续复用同一条 project-aware 输入链路，不会引入第二套独立 loader。
4. 修改 project descriptor、CLI 输入模式或 source graph 行为时，应同时更新：
   - `docs/design/project-descriptor-architecture-v0.3.zh.md`
   - `docs/reference/cli-commands-v0.3.zh.md`
   - `tests/cmake/ProjectTests.cmake`
   - `tests/project/`
5. 本地最小验证建议先跑：
   - `ctest --preset test-dev -L ahfl-v0.3`
