# AHFL IR Format

本文是 `docs/reference` 中 IR 格式参考的合并入口，统一覆盖原 `ir-format-v0.2` 与 `ir-format-v0.3` 文档。当前维护口径以 V0.3 为准；V0.2 的文本 / JSON IR 基础结构已合并到本文，不再保留独立入口。

关联文档：

- [compiler-phase-boundaries.zh.md](../design/compiler-phase-boundaries.zh.md)
- [project-usage.zh.md](./project-usage.zh.md)
- [cli-commands.zh.md](./cli-commands.zh.md)
- [backend-capability-matrix.zh.md](./backend-capability-matrix.zh.md)

## 合并范围

| 历史版本 | 合并后保留的信息 |
|----------|------------------|
| V0.2 | 文本 IR / JSON IR 双输出形态、顶层 program 结构、declaration provenance 与 declaration serialization。 |
| V0.3 | project-aware provenance、`TypeRef` / `SymbolRef`、flow handler summary、workflow value summary、`formal_observations`。 |

## 当前口径摘要

1. 机器消费应优先使用 JSON IR，而不是解析文本 IR 展示字符串。
2. `format_version = "ahfl.ir.v1"` 是当前 IR 格式标识。
3. 旧字符串字段保留为展示/兼容入口；稳定身份应优先读 `*_ref.canonical_name`。
4. flow / workflow summary 是下游 consumer 的受限摘要，不替代完整 expression / statement tree。
5. `formal_observations` 是 IR 与 formal backend 的共享 observation registry，不属于某个 backend 私有格式。
6. Opt IR 是独立的优化诊断 artifact，通过 `ahflc emit opt-ir` 输出文本、通过 `ahflc emit opt-ir-json` 输出 `AHFL_OPT_IR_V1` JSON；它不是 Semantic JSON IR 的替代格式，也不是普通 backend contract。

## 定位

稳定 IR 是 backend 的公共输入边界：

1. 它只消费 validate 之后的语义模型。
2. 它不是 parse tree 的序列化。
3. 它保留 declaration、expression、statement、temporal formula 与 `formal_observations` 的结构化信息。
4. 在 V0.3 中，它还稳定暴露 declaration provenance、结构化类型/符号引用、flow summary 和 workflow value summary。

当前版本标识：

- `format_version = "ahfl.ir.v1"`

readonly 当前不是 AHFL Core V0.1 的源码语法，也不是 Semantic IR `TypeRef` / JSON IR 的稳定字段。容器 variance 由类型关系层内部处理；若未来引入 readonly 语言能力，必须单独扩展 spec、grammar、Typed HIR、Semantic IR 与 JSON IR。

## 两种输出形态

文本 IR：

```bash
./build/dev/src/tooling/cli/ahflc emit-ir --project tests/project/check_ok/ahfl.project.json
```

JSON IR：

```bash
./build/dev/src/tooling/cli/ahflc emit-ir-json --project tests/project/check_ok/ahfl.project.json
```

文本 IR 适合：

- 人工阅读
- golden diff
- declaration ownership / summary 审查

JSON IR 适合：

- 脚本消费
- downstream tooling
- 对 declaration / observation / summary 做结构化遍历

Optimization IR：

```bash
./build/dev/src/tooling/cli/ahflc emit opt-ir tests/golden/ir/ok_expr_temporal.ahfl
./build/dev/src/tooling/cli/ahflc emit opt-ir -O tests/golden/ir/ok_expr_temporal.ahfl
./build/dev/src/tooling/cli/ahflc emit opt-ir-json tests/golden/ir/ok_expr_temporal.ahfl
./build/dev/src/tooling/cli/ahflc emit opt-ir-json -O tests/golden/ir/ok_expr_temporal.ahfl
```

Opt IR 是 `ir::Program` 下方的 CFG/SSA 风格诊断表示。文本 dump 输出 function、local、basic block、statement、terminator、operand、type、source range，以及未降成 pure expression fragment 的 `skipped_temporal` 记录；`AHFL_OPT_IR_V1` JSON 输出同一模型的结构化机器可读 artifact。普通 backend、Semantic JSON IR、SMV、native / execution / assurance 输出仍消费 Semantic IR (`ir::Program`)；`-O` 在 `emit opt-ir` / `emit opt-ir-json` 中会额外运行 Opt IR passes 并打印优化后的 Opt IR。

Opt IR 当前生产路径是 artifact-only：它不会被 `--optimize` 回降到 Semantic IR，也不会让普通 backend 隐式直连 Opt IR。

Opt IR JSON 顶层结构：

```json
{
  "format": "AHFL_OPT_IR_V1",
  "source_program_version": "ahfl.ir.v1",
  "skipped_temporal": [],
  "functions": []
}
```

`skipped_temporal` 表示 `called`、`running`、`completed`、`in_state` 等 event/state temporal atom 被有意排除在 expression-function lowering 之外。它们不是静默丢失，而是以 scope、kind、reason 和 source range 进入 artifact：

```text
skipped_temporal scope=workflow::formal::flow_workflow::ReviewWorkflow::safety::0 kind=running reason="temporal atom is not a pure expression fragment" source=[1040,1055)
```

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
    "source_path": "app/main.ahfl",
    "source_range": {
      "begin_offset": 0,
      "end_offset": 128
    }
  }
}
```

`source_range` 使用源文件内的半开字节偏移区间 `[begin_offset, end_offset)`。除 declaration provenance
外，JSON IR 也会在表达式、时序表达式、语句、block、参数、结构体字段、contract clause、
flow handler、workflow node 等后端可能需要重新定位诊断的位置上携带 `source_range`。

消费建议：

1. 多文件模式下，不要只用 local name 作为 declaration 键。
2. 若要追踪声明来源，应至少联合使用 canonical name 与 provenance。
3. 机器消费应优先读取 JSON IR 的 `provenance` 对象，而不是解析文本 IR。
4. 需要生成诊断或 sourcemap 时，优先使用 `source_range`，不要反向解析展示字符串。

## TypeRef / SymbolRef

JSON IR 中，旧字符串字段仍然保留：

- `type`
- `input_type`
- `output_type`
- `return_type`
- `target`
- `capabilities`

这些字段继续作为兼容展示值和旧 consumer 入口。新增的结构化伴随字段用于机器消费：

- `type_ref`
- `aliased_type_ref`
- `input_type_ref`
- `context_type_ref`
- `output_type_ref`
- `return_type_ref`
- `symbol_ref`
- `target_ref`
- `capability_refs`

`TypeRef` 的核心字段：

```json
{
  "kind": "struct",
  "display_name": "demo::Request",
  "canonical_name": "demo::Request"
}
```

复合类型会继续结构化展开：

```json
{
  "kind": "map",
  "display_name": "Map<String, Int>",
  "key_type": {
    "kind": "string",
    "display_name": "String"
  },
  "value_type": {
    "kind": "int",
    "display_name": "Int"
  }
}
```

`SymbolRef` 的核心字段：

```json
{
  "kind": "agent",
  "canonical_name": "demo::Agent",
  "local_name": "Agent",
  "module_name": "demo"
}
```

消费建议：

1. 需要稳定身份时，优先读 `*_ref.canonical_name`，不要解析展示字符串。
2. 需要保持旧版本兼容时，仍可读取原有字符串字段。
3. 对未知 `*_ref` 子字段保持 forward-compatible 忽略能力。

## ExprRef / Source Range / ID

当前 Semantic IR 的普通 expression 由 `Program::expr_arena` 统一拥有，节点之间通过 `ExprRef` 引用，而不是嵌套 owning pointer tree。JSON IR 仍导出结构化 expression tree，消费方不需要直接理解 arena 内存布局，但应按下面规则处理稳定身份和诊断位置：

1. `Expr::id` 是 lowering 时分配的 program-local stable numeric ID，可用于同一次 IR 输出内的诊断和测试关联。
2. `ExprRef` 是实现层 handle，不是当前 JSON IR 的跨版本 ABI；外部机器消费仍应读取 JSON 节点结构。
3. expression、statement、declaration provenance、type ref 等位置携带的 `source_range` 使用 `[begin_offset, end_offset)` 半开区间。
4. 表达式类型应读取结构化 `type_ref` / `resolved_type` 相关字段，不要从展示字符串反推类型。

statement 与 temporal expression 当前不是 arena/ref ABI。它们在 JSON IR 中继续作为 owning tree 输出：statement 由 block 顺序拥有，temporal expression 由 contract clause 或 workflow safety/liveness formula 递归拥有。`Statement::id` 可用于同一次 IR 输出内的诊断和测试关联；temporal expression 没有跨 owner 稳定 handle。外部 consumer 需要遍历 statement / temporal 时，应遍历 JSON tree 本身，而不是期待 `StatementRef` / `TemporalRef`。

## Path Root Kind

JSON IR 中的 path expression 会输出 `root_kind`，用于冻结路径根的语义来源。当前合法取值为：

| `root_kind` | 含义 |
|-------------|------|
| `input` | flow handler input、workflow input 或 contract input |
| `context` | `ctx.*` execution context 路径 |
| `output` | flow handler output 路径 |
| `state` | 显式 state 根路径 |
| `local` | flow 内 let/local binding 路径 |
| `identifier` | workflow node output 或其他普通 identifier 根 |

Typed HIR serialization、Semantic IR lowering、JSON IR、文本 IR、runtime assignment executor 与 Opt IR lowering 都消费同一组 root kind。下游 consumer 不应再通过 `root_name == "ctx"` 这类字符串约定推断语义；需要判断执行上下文、local binding 或 workflow node output 时，应优先读取 `root_kind`。

当前 DSL golden 覆盖 `input/context/local/output/identifier` 的实际源码路径；`state` 作为 IR 结构能力由 IR printer / JSON 单元测试覆盖。

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

Opt IR 对 temporal 的处理与 `formal_observations` 不同：embedded pure bool expr 会降成 OptFunction；event/state atom 会保留为 `skipped_temporal` artifact 记录。二者共同保证 temporal clause 的优化覆盖和不可优化原因都可审查。

它与 summary 一样，都是给下游 consumer 的稳定公共边界，而不是某个 backend 私有结构。

## 稳定边界与非目标

当前可视为稳定的边界：

1. 顶层 program 结构
2. declaration kind 集合
3. `provenance` 字段名与语义
4. `TypeRef` / `SymbolRef` 作为旧字符串字段的结构化机器可读伴随字段
5. flow summary 与 workflow value summary 的结构化含义
6. `formal_observations` 的公共角色

内部编译器会用 `Program::analysis_revision` 与 `AnalysisBundle::source_program_revision` 保证 derived analyses freshness；这是 pass / backend emission 的实现合同，不是当前 JSON IR 的外部 ABI 字段。外部 consumer 只应消费已经输出的 summary / observation 内容。

当前不应被视为稳定 ABI 的内容：

1. 文本 IR 的纯排版细节
2. 供人读的注释行文字
3. backend-specific lower 之后的中间辅助命名

换句话说：

- 机器消费优先依赖 JSON IR。
- 人工审查和 golden diff 优先使用文本 IR。

## 版本与变更规则

Semantic IR 当前稳定版本标识为：

- `format_version = "ahfl.ir.v1"`

该版本号同时出现在文本 IR 首行和 JSON IR 顶层字段。`include/ahfl/compiler/ir/ir.hpp` 中的 `ahfl::ir::kFormatVersion` 是实现侧事实来源。

在不改变 `format_version` 的前提下，允许的普通扩展包括：

1. 新增可忽略的 JSON 字段。
2. 新增 text IR 展示行，但不改变既有字段语义。
3. 新增 backend-private artifact，只要不改变 Semantic IR contract。
4. 新增 summary / observation 时同时保留原字段含义。

必须视为 format-version / golden / docs 同步事件的变化包括：

1. 删除、重命名或改变 JSON IR machine-facing 字段语义。
2. 改变 declaration、expression、statement、temporal formula 的结构含义。
3. 改变 `TypeRef` / `SymbolRef` 的 identity 语义。
4. 改变 `formal_observations` 的收集边界。
5. 让普通 backend 隐式消费 Opt IR，或让 `--optimize` 绕过 Semantic IR contract。

Consumer 建议：

1. 机器消费优先读取 JSON IR，并显式校验 `format_version`。
2. 人工审查和 golden diff 可以使用文本 IR，但不应把纯排版当作稳定 ABI。
3. 需要优化诊断时读取 `AHFL_OPT_IR_V1`，不要把 Opt IR 当作 Semantic JSON IR 的替代格式。
