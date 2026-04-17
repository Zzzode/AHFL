# AHFL Core V0.3 IR Format

本文给出 AHFL 稳定 IR / JSON IR 的消费边界参考，重点覆盖 V0.3 的 project-aware provenance、flow handler summary 与 workflow value summary。

关联文档：

- [compiler-phase-boundaries-v0.2.zh.md](../design/compiler-phase-boundaries-v0.2.zh.md)
- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)
- [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)

## 定位

稳定 IR 是 backend 的公共输入边界：

1. 它只消费 validate 之后的语义模型。
2. 它不是 parse tree 的序列化。
3. 它保留 declaration、expression、statement、temporal formula 与 `formal_observations` 的结构化信息。
4. 在 V0.3 中，它还稳定暴露 declaration provenance、flow summary 和 workflow value summary。

当前版本标识：

- `format_version = "ahfl.ir.v1"`

更完整的版本化与 breaking-change 规则见：

- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)

## 两种输出形态

文本 IR：

```bash
./build/dev/src/cli/ahflc emit-ir --project tests/project/check_ok/ahfl.project.json
```

JSON IR：

```bash
./build/dev/src/cli/ahflc emit-ir-json --project tests/project/check_ok/ahfl.project.json
```

文本 IR 适合：

- 人工阅读
- golden diff
- declaration ownership / summary 审查

JSON IR 适合：

- 脚本消费
- downstream tooling
- 对 declaration / observation / summary 做结构化遍历

## 顶层结构

IR program 当前包含三个稳定顶层字段：

- `format_version`
- `formal_observations`
- `declarations`

```json
{
  "format_version": "ahfl.ir.v1",
  "formal_observations": [],
  "declarations": []
}
```

## Declaration Provenance

project-aware 模式下，每个 declaration 都会携带 provenance。

文本 IR 形态：

```text
@provenance(module=app::main, source=app/main.ahfl)
workflow app::main::MainWorkflow { ... }
```

JSON IR 形态：

```json
{
  "kind": "workflow",
  "provenance": {
    "module_name": "app::main",
    "source_path": "app/main.ahfl"
  }
}
```

消费建议：

1. 多文件模式下，不要只用 local name 作为 declaration 键。
2. 若要追踪声明来源，应至少联合使用 canonical name 与 provenance。
3. 机器消费应优先读取 JSON IR 的 `provenance` 对象，而不是解析文本 IR。

## Flow Handler Summary

V0.3 在 `flow.state_handlers[]` 上新增稳定摘要字段：

- `summary.goto_targets`
- `summary.may_return`
- `summary.may_fallthrough`
- `summary.assigned_paths`
- `summary.called_targets`
- `summary.assert_count`

它的作用不是替代 statement tree，而是显式暴露 backend/tooling 需要的受限控制与副作用摘要。

```json
{
  "state_name": "Init",
  "summary": {
    "goto_targets": ["Done"],
    "may_return": false,
    "may_fallthrough": false,
    "assigned_paths": [],
    "called_targets": ["demo::Echo"],
    "assert_count": 0
  }
}
```

## Workflow Value Summary

V0.3 在 workflow 上新增两类稳定摘要：

- `nodes[].input_summary`
- `return_summary`

其目标是把 workflow 值流里最关键的“读取来源”冻结下来，而不是让 downstream tooling 重新遍历表达式树猜测来源。

读取来源当前仅有两类：

- `workflow_input`
- `workflow_node_output`

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
  }
}
```

边界说明：

1. 这层 summary 只冻结“值从哪里读”。
2. 它不冻结执行顺序、缓存、调度或 path-sensitive dataflow。
3. 需要完整表达式结构时，仍应读取 `input` / `return_value` 原始 IR 节点。

## `formal_observations`

`formal_observations` 是 IR 与 formal backend 共享的 observation registry。

当前 observation 来源包括：

- `called(<capability>)`
- contract `requires` / `ensures` expr
- contract temporal clause 内嵌的 pure bool expr
- workflow safety / liveness 内嵌的 pure bool expr

它与 summary 一样，都是给下游 consumer 的稳定公共边界，而不是某个 backend 私有结构。

## 稳定边界与非目标

当前可视为稳定的边界：

1. 顶层 program 结构
2. declaration kind 集合
3. `provenance` 字段名与语义
4. flow summary 与 workflow value summary 的结构化含义
5. `formal_observations` 的公共角色

当前不应被视为稳定 ABI 的内容：

1. 文本 IR 的纯排版细节
2. 供人读的注释行文字
3. backend-specific lower 之后的中间辅助命名

换句话说：

- 机器消费优先依赖 JSON IR。
- 人工审查和 golden diff 优先使用文本 IR。
