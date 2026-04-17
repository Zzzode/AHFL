# AHFL Core V0.2 IR Format

本文给出 AHFL 稳定 IR / JSON IR 的消费边界参考，重点说明 V0.2 新增的 project-aware provenance 与 shared `formal_observations`。

关联文档：

- [compiler-phase-boundaries-v0.2.zh.md](../design/compiler-phase-boundaries-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)
- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)
- [project-usage-v0.2.zh.md](./project-usage-v0.2.zh.md)
- [cli-commands-v0.2.zh.md](./cli-commands-v0.2.zh.md)

## 定位

稳定 IR 是 backend 的公共输入边界：

1. 它只消费 validate 之后的语义模型。
2. 它不是 parse tree 的序列化。
3. 它保留 declaration、expression、statement、temporal formula 与 formal observation 的结构化信息。
4. 在 V0.2 中，它还必须显式表达 declaration provenance，不能再假定“声明来自当前单文件输入”。

当前版本标识：

- `format_version = "ahfl.ir.v1"`

更完整的版本化与 breaking-change 规则见：

- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)

## 两种输出形态

### 文本 IR

通过：

```bash
./build/dev/src/cli/ahflc emit-ir <input.ahfl>
```

或：

```bash
./build/dev/src/cli/ahflc emit-ir --search-root <dir>... <entry.ahfl>
```

文本 IR 适合：

- 人工阅读
- golden diff
- 审查 declaration ownership

### JSON IR

通过：

```bash
./build/dev/src/cli/ahflc emit-ir-json <input.ahfl>
```

或：

```bash
./build/dev/src/cli/ahflc emit-ir-json --search-root <dir>... <entry.ahfl>
```

JSON IR 适合：

- 脚本消费
- downstream tooling
- 对 declaration / observation 做结构化遍历

## 顶层结构

IR program 当前包含三个稳定顶层字段：

- `format_version`
- `formal_observations`
- `declarations`

JSON 示例：

```json
{
  "format_version": "ahfl.ir.v1",
  "formal_observations": [],
  "declarations": []
}
```

## Declaration Provenance

V0.2 新增 declaration provenance，用来表达每个声明属于哪个 module / source。

### 文本 IR

project-aware 模式下，每个 declaration 前会出现：

```text
@provenance(module=app::main, source=app/main.ahfl)
workflow app::main::MainWorkflow { ... }
```

### JSON IR

project-aware 模式下，每个 declaration 会携带：

```json
{
  "kind": "workflow",
  "provenance": {
    "module_name": "app::main",
    "source_path": "app/main.ahfl"
  }
}
```

### 字段说明

- `module_name`
  - 声明所属 module。
- `source_path`
  - 相对 project module path 规则推导出的稳定 source 路径。

### 消费建议

1. 多文件模式下，不要只用 local name 作为 declaration 键。
2. 若要追踪声明来源，应至少联合使用 canonical name 与 provenance。
3. 文本 IR 的 `@provenance(...)` 是人类可读表示；机器消费应优先使用 JSON IR 的 `provenance` 对象。

## Declarations

当前稳定 declaration 种类包括：

- `module`
- `import`
- `const`
- `type_alias`
- `struct`
- `enum`
- `capability`
- `predicate`
- `agent`
- `contract`
- `flow`
- `workflow`

这些 declaration 在文本 IR 与 JSON IR 中保持一一对应，只是展示方式不同。

## Expressions / Statements / Temporal Formulas

IR 里会继续保留结构化节点，而不是退化回源码字符串。当前主要包括：

- value expr
  - literal、path、call、struct literal、collection literal、member/index access、unary/binary 等。
- flow statement
  - `let`、`assign`、`if`、`goto`、`return`、`assert`、expr statement。
- temporal expr
  - `called`、`in_state`、`running`、`completed`、temporal unary/binary、embedded expr。

消费方不应依赖 AST trivia 或 token 文本来重建这些结构。

## Flow Handler Summary

V0.3 在现有 `flow.state_handlers[].body` 之外，新增稳定摘要字段：

- `summary.goto_targets`
- `summary.may_return`
- `summary.may_fallthrough`
- `summary.assigned_paths`
- `summary.called_targets`
- `summary.assert_count`

其目的不是替代原始 statement tree，而是显式暴露 backend/tooling 之前需要自行推导的受限 flow 语义。

### JSON 形态

```json
{
  "state_name": "Init",
  "summary": {
    "goto_targets": ["Done"],
    "may_return": false,
    "may_fallthrough": false,
    "assigned_paths": [
      {
        "root_kind": "identifier",
        "root_name": "ctx",
        "members": ["value"]
      }
    ],
    "called_targets": ["ir::alias_const::Echo"],
    "assert_count": 0
  }
}
```

### 消费建议

1. 若你只关心 flow 控制摘要，优先读 `summary`，不要在 backend 里重复遍历 statement tree。
2. 若你需要更细粒度语句结构，继续读取 `body.statements`；`summary` 不是替代物。
3. `assigned_paths` / `called_targets` / `assert_count` 当前表示“存在性摘要”，不是完整执行语义或 path-sensitive dataflow。

## Workflow Value Summary

V0.3 在 workflow IR 中新增两类稳定摘要：

- `nodes[].input_summary`
- `return_summary`

其用途是把 workflow 值流里最关键的“读取来源”显式冻结下来，而不是让下游 backend/tooling 重新遍历表达式树推断。

### 读取来源种类

- `workflow_input`
  - 表示读取 workflow 自身输入，例如 `input`、`input.id`
- `workflow_node_output`
  - 表示读取某个 workflow node 的输出，例如 `first`、`first.value`

### JSON 形态

```json
{
  "input_summary": {
    "reads": [
      {
        "kind": "workflow_node_output",
        "root_name": "first",
        "members": ["value"]
      }
    ]
  },
  "return_summary": {
    "reads": [
      {
        "kind": "workflow_node_output",
        "root_name": "second",
        "members": ["value"]
      }
    ]
  }
}
```

### 边界说明

1. 这层 summary 只冻结“值从哪里读”，不冻结执行顺序、缓存、调度或 path-sensitive 数据语义。
2. `after` 仍表示显式执行依赖；`input_summary` / `return_summary` 表示值依赖，二者不应混为一层。
3. 若 consumer 需要完整表达式结构，仍应读取 `input` / `return_value`；summary 只提供稳定快捷入口。

## `formal_observations`

`formal_observations` 是 IR 与 formal backend 共享的 observation registry。

当前 observation 来源包括：

- `called(<capability>)`
- contract `requires` / `ensures` expr
- contract temporal clause 内嵌的 pure bool expr
- workflow safety/liveness 内嵌的 pure bool expr

### 结构

当前 observation 节点种类包括：

- `called_capability`
- `embedded_bool_expr`

其中 `embedded_bool_expr` 会带 scope：

- `contract_clause`
- `workflow_safety_clause`
- `workflow_liveness_clause`

## 与 formal backend 的关系

IR 并不等于 SMV，但 formal backend 直接消费这两个稳定部分：

1. declaration 结构
2. `formal_observations`

当前 `emit-smv` 对 contract 的受限导出规则是：

- `requires`
  - 作为 observation-backed 初始态前置条件导出。
- `ensures`
  - 作为 observation-backed、final-state-guarded obligation 导出。

这仍然不是 statement/dataflow 的完整执行语义。

## 稳定边界与非目标

当前可视为稳定的边界：

1. 顶层 program 结构
2. declaration kind 集合
3. JSON provenance 字段名：`module_name`、`source_path`
4. `formal_observations` 的共享角色

当前不应被视为稳定 ABI 的内容：

1. 文本 IR 的纯排版细节
2. 供人读的注释行文字
3. backend-specific 进一步 lower 之后的中间辅助命名

换句话说：

- 若你要做脚本消费，优先依赖 JSON IR 的字段结构。
- 若你要做回归审查，文本 IR 更适合 diff。
