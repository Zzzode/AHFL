# AHFL-IR Format

本文说明如何导出、读取和校验 AHFL 的中间表示（IR）。用文本 IR 阅读程序结构，用 JSON IR 接入脚本和分析工具，用 Opt IR 检查优化结果。

- 命令和选项见 [CLI 参考](./cli-commands.zh.md)。
- 导出能力和限制见 [产物选择参考](./backend-capability-matrix.zh.md)。
- 多文件工程的输入配置见[包指南](../tutorials/packages.zh.md)。

## 分层投影边界

编译结果分为 AHFL-IR、Core-IR 和可执行 Wasm 三层。检查前两层时，按下表选择输出：

| 层 | artifact id | 格式标识 | 角色 |
|----|-------------|----------|------|
| AHFL-IR（verification / orchestration，`ir::Program`） | `emit ir-json` | `ahfl.ir` | 验证/编排层稳定投影；SMV、assurance 等下游消费该层 |
| AHFL-IR（文本） | `emit ir` | `ahfl.ir` | 同上，人类可读文本形式 |
| Core-IR（execution，`CoreProgram`） | `emit core-ir-json` | `ahfl.core` | 执行层投影 |

```bash
ahflc emit core-ir-json examples/refund/audit.ahfl
```

读取不同层时，请按 `format` 校验格式：

1. `ir` / `ir-json` 提供验证与编排结构，包括声明、表达式、语句、时序公式与 `formal_observations`。
2. `core-ir-json` 提供执行结构。JSON 顶层包含 `format`、`layer: "core"`、`types` / `value_types` / `capabilities` / `agents` / `flows` / `workflows` / `instances`。
3. Core-IR 的 `value_types` 是程序级类型表；region 与 statement 递归嵌套。该输出不包含目标平台的物理内存布局。详细 schema 见 [Core-IR JSON 格式](../design/core-ir-p9-layered-json.zh.md)。

`readonly` 不是源码语法，也不是 `TypeRef` / JSON IR 字段；读取类型时不要假定存在该标记。

## 两种输出形态

文本 IR：

```bash
ahflc emit ir examples/refund/audit.ahfl
```

JSON IR：

```bash
ahflc emit ir-json examples/refund/audit.ahfl
```

文本 IR 适合：

- 人工阅读
- 比较两次编译输出
- declaration ownership / summary 审查

JSON IR 适合：

- 脚本消费
- downstream tooling
- 对 declaration / observation / summary 做结构化遍历

Opt IR：

```bash
ahflc emit opt-ir examples/refund/audit.ahfl
ahflc emit opt-ir -O examples/refund/audit.ahfl
ahflc emit opt-ir-json examples/refund/audit.ahfl
ahflc emit opt-ir-json -O examples/refund/audit.ahfl
```

Opt IR 提供控制流图（CFG）和静态单赋值（SSA）形式的诊断信息，包含函数、局部变量、基本块、语句、终结指令、操作数、类型和源范围。无法作为纯表达式处理的时序原子记录在 `skipped_temporal` 中。

`-O` 会优化 AHFL-IR，因此普通导出产物也可能变化；导出 Opt IR 时还会优化其函数与基本块。Opt IR 是 artifact-only 诊断产物；用 `ir-json` 读取验证与编排结构，用 `core-ir-json` 读取执行结构。

Opt IR JSON 顶层结构：

```json
{
  "format": "AHFL_OPT_IR",
  "source_program_format": "ahfl.ir",
  "skipped_temporal": [],
  "functions": []
}
```

`skipped_temporal` 表示 `called`、`running`、`completed`、`in_state` 等事件或状态时序原子未被转换为纯表达式函数。读取 scope、kind、reason 和 source range 可定位无法优化的条款：

```text
skipped_temporal scope=workflow::formal::flow_workflow::ReviewWorkflow::safety::0 kind=running reason="temporal atom is not a pure expression fragment" source=[1040,1055)
```

## 顶层结构

IR program 当前包含三个稳定顶层字段：

- `format`
- `formal_observations`
- `declarations`

```json
{
  "format": "ahfl.ir",
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

JSON IR 中，以下字符串字段用于展示：

- `type`
- `input_type`
- `output_type`
- `return_type`
- `target`
- `capabilities`

机器读取类型和符号时，请使用对应的结构化字段：

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

泛型类型使用声明的名义类型和 `type_args` 表示参数。例如，`Map<String, Int>` 的类型引用包含以下字段（`nominal_ref.id` 为本产物中的声明编号）：

```json
{
  "kind": "struct",
  "display_name": "std::collections::Map<String, Int>",
  "canonical_name": "std::collections::Map",
  "nominal_ref": {
    "kind": "type",
    "canonical_name": "std::collections::Map",
    "id": 2
  },
  "type_args": [
    { "kind": "string", "display_name": "String" },
    { "kind": "int", "display_name": "Int" }
  ]
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
2. 展示名称时可以读取字符串字段；不要从其排版反推类型结构。
3. 严格校验当前 `*_ref` 子字段；未知字段或缺少必填字段拒绝读取。

## ExprRef / Source Range / Type / Effect / ID

JSON IR 中的 expression、statement 和 temporal expression 均按树结构遍历。请按以下规则读取：

1. expression 和 statement 的 `id` 是本次程序输出内的数值身份，可用于关联诊断；不要跨编译会话复用。
2. `ExprRef` 是编译器内部句柄，不是 JSON 格式字段；外部工具应读取 JSON 节点。
3. `source_range` 使用 `[begin_offset, end_offset)` 半开字节偏移区间。
4. 表达式类型读取结构化 `type_ref` / `resolved_type` 字段，不从展示字符串反推。
5. `effect` 描述表达式效果，取值为 `pure`、`const_only`、`predicate_call`、`nondet`、`capability_call`、`external_effect` 或 `unknown`。读取保证案例或形式化结果时，不能把缺失或未知效果当作 `pure`；能力身份与推导效果矛盾时应拒绝该输入。
6. temporal expression 没有跨 owner 的稳定句柄；请从所属 contract clause 或 workflow safety/liveness 列表遍历。

## Path Root Kind

JSON IR 中的 path expression 会输出 `root_kind`，用于区分路径根的语义来源。当前合法取值为：

| `root_kind` | 含义 |
|-------------|------|
| `input` | flow handler input、workflow input 或 contract input |
| `context` | `ctx.*` execution context 路径 |
| `output` | flow handler output 路径 |
| `state` | 显式 state 根路径 |
| `local` | flow 内 let/local binding 路径 |
| `identifier` | workflow node output 或其他普通 identifier 根 |

判断路径是上下文、局部变量还是 workflow 节点输出时，读取 `root_kind`，不要通过 `root_name == "ctx"` 等名称约定推断。

## Flow Handler Summary

`flow.state_handlers[]` 的摘要字段：

- `summary.goto_targets`
- `summary.may_return`
- `summary.may_fallthrough`
- `summary.assigned_paths`
- `summary.called_targets`
- `summary.inferred_effect`
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
    "inferred_effect": "capability_call",
    "assert_count": 0
  }
}
```

## Workflow Value Summary

workflow 提供两类摘要：

- `nodes[].input_summary`
- `return_summary`

用这些字段判断节点输入或返回值读取了哪些来源。

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

1. summary 只说明值的读取来源。
2. 它不描述执行顺序、缓存、调度或各控制路径上的数据流。
3. 需要完整表达式结构时，仍应读取 `input` / `return_value` 原始 IR 节点。

## `formal_observations`

`formal_observations` 是 IR 与 formal backend 共享的 observation registry。

对于 `ContractClause` scope，`clause_index` 是同一 resolved Agent 的所有合同
按当前 IR 声明顺序展开后的累计条款编号，从 0 开始；计数包括 `decreases`
等全部条款，空合同不占编号。不同 Agent 分别计数，`atom_index` 仍是单条款
内的 atom 编号。workflow safety/liveness 的 `clause_index` 保持各列表内索引。
反查条款时须找到实际所属合同，以其 `provenance` 获取源文件与范围。

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
4. `TypeRef` / `SymbolRef` 的结构化类型与符号信息
5. flow summary 与 workflow value summary 的结构化含义
6. `formal_observations` 的公共角色

`Program::analysis_revision` 是内部分析版本，不在 JSON IR 中输出。外部工具请读取导出的 summary / observation，不依赖编译器内存结构或分析缓存。

当前不应被视为稳定 ABI 的内容：

1. 文本 IR 的纯排版细节
2. 供人读的注释行文字
3. backend-specific lower 之后的中间辅助命名

换句话说：

- 机器消费优先依赖 JSON IR。
- 人工审查和输出比较优先使用文本 IR。

## 当前格式与变更规则

AHFL-IR 当前格式标识为 `ahfl.ir`。

该格式标识同时出现在文本 IR 首行和 JSON IR 顶层字段。

读取 `ahfl.ir` 时，严格校验当前字段及 `format`；缺少必填字段或未知字段拒绝。文本排版和 backend 内部辅助名称不属于稳定机器接口。

expression 的 `effect` 和 flow summary 的 `inferred_effect` 是必填的语义信息。字段缺失时拒绝输入，不得把缺失效果静默当作 `pure`。

Consumer 建议：

1. 机器消费优先读取 JSON IR，并显式校验 `format`。
2. 人工审查和输出比较可以使用文本 IR，但不应把纯排版当作稳定 ABI。
3. 需要优化诊断时读取 `AHFL_OPT_IR`，不要把 Opt IR 当作 Semantic JSON IR 的替代格式。
