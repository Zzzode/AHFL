# AHFL Core V0.2 Formal Backend Boundary

本文冻结 AHFL Core 在 formal backend 方向的当前边界，作为 `emit-smv`、后续 model-check-oriented backend，以及 `tests/formal/*` golden 的统一约束。

适用范围：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `include/ahfl/backends/smv.hpp`
- `src/backends/smv.cpp`
- `src/cli/ahflc.cpp`
- `tests/formal/*`
- `docs/plan/roadmap-v0.1.zh.md`
- `docs/plan/roadmap-v0.2.zh.md`
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`

## 目标

1. 明确当前哪些 AHFL 语义会被 lower 到 formal backend
2. 明确当前哪些语义会被抽象成 observation
3. 明确当前 formal backend 明确不承诺的语义
4. 给出 Issue 16-20 的实现顺序，避免在 `emit-smv` 上继续 ad-hoc 扩张

## 当前 backend 边界

当前仓库中的 formal backend 入口是：

- `ahflc emit-smv <file>`

其处理链路固定为：

1. `parse`
2. `ast`
3. `resolve`
4. `typecheck`
5. `validate`
6. `emit-smv`

`emit-smv` 不直接消费 parse tree，也不直接从源码字符串做二次解析；它只基于 validate 通过后的语义模型和稳定 IR 做 lowering。

当前输出目标固定为：

- 单个 `MODULE main`
- `VAR`
- `IVAR`
- `DEFINE`
- `ASSIGN`
- `LTLSPEC`

当前逻辑边界固定为：

- 只处理 LTL 方向的公式 lowering
- 保留 validate 后的状态机、flow 终结控制流与 workflow 启动/完成结构
- 不尝试把 AHFL 解释成完整 runtime

## V0.2 增量冻结

V0.2 在 formal backend 方向只正式新增一类能力：

- contract clause export 的受限扩张

这次扩张不改变 backend 的总体定位。当前 `emit-smv` 仍然是：

- project-aware Core 的 restricted formal exporter

而不是：

- 完整 runtime / statement / data semantics 的 model checker backend

V0.2 当前正式保留的 contract/export 语义：

1. `requires`
   - 保留为 contract precondition 边界。
   - 通过 observation-backed `LTLSPEC` 导出。
2. `ensures`
   - 保留为 final-state obligation 边界。
   - 通过 observation-backed、final-state-guarded `LTLSPEC` 导出。
3. `invariant`
   - 保留为现有 temporal exporter 可表达的 agent contract 公式。
4. `forbid`
   - 保留为现有 temporal exporter 可表达的禁止型 temporal 公式。
5. workflow `safety` / `liveness`
   - 继续保留为 workflow lifecycle 上的 LTL 公式。

V0.2 当前继续抽象的 contract/export 语义：

1. `requires` / `ensures` 内部的值计算与数据依赖
2. output 值如何由 flow statement 逐步构造
3. capability 参数、返回值、调用次数与调用顺序
4. flow 中 `let` / 赋值 / `assert` 的 transition-relation 编码
5. retry / timeout / quota / exception / richer scheduling 语义

因此，V0.2 的 contract/export 边界必须按下面的二分法理解：

- 保留语义
  - clause 的约束边界与何时应成立。
- 抽象语义
  - clause 内部的具体数据求值和 runtime 机制。

## V0.3 Freeze: Flow Summary Boundary

V0.3 在不把 `emit-smv` 扩张成完整 statement executor 的前提下，正式新增一层 flow statement 摘要边界：

- `flow.state_handlers[].summary.goto_targets`
- `flow.state_handlers[].summary.may_return`
- `flow.state_handlers[].summary.may_fallthrough`
- `flow.state_handlers[].summary.assigned_paths`
- `flow.state_handlers[].summary.called_targets`
- `flow.state_handlers[].summary.assert_count`

这层边界的定位不是“把每条语句直接 lower 成 transition relation”，而是把 backend 之前在内部临时推导的 flow 控制/副作用摘要提升成稳定 IR 契约。

### V0.3 当前保留的 flow 语义

1. handler 可能跳转到哪些 `goto` 目标
2. handler 是否可能 `return`
3. handler 是否可能 fallthrough 到后续语句
4. handler 内显式写入了哪些 path
5. handler 内出现了哪些 call target
6. handler 内出现了多少个 `assert`

### V0.3 当前继续抽象的 flow 语义

1. `let` / assignment / `assert` 的逐步求值顺序
2. path 写入之间的先后关系与数据依赖
3. call target 的执行时点、次数、参数值、返回值与因果关系
4. `assert` 条件本身对 transition relation 的直接约束
5. 多分支路径上的精确 path-sensitive 数据语义

### backend 当前如何消费

当前 `emit-smv` 只正式消费 flow summary 里的控制摘要：

1. `goto_targets`
2. `may_return`
3. `may_fallthrough`

`assigned_paths`、`called_targets`、`assert_count` 当前作为稳定导出信息保留在 IR / JSON IR 中，供后续 backend 或 tooling 消费，但 `emit-smv` 还不会把它们直接编码成状态迁移或值语义。

## V0.3 Freeze: Workflow Value Summary Boundary

V0.3 同时把 workflow 值流里最关键的读取来源提升成稳定 IR 字段：

- `workflow.nodes[].input_summary.reads`
- `workflow.return_summary.reads`

其语义只覆盖：

1. 读取 `workflow_input`
2. 读取 `workflow_node_output`

当前 `emit-smv` 仍然不会把这些 value summary 直接 lower 成值语义或数据通道语义；它们的作用是为后续 formal/backend/tooling 保留统一消费入口，而不是提前承诺完整 workflow dataflow execution semantics。

## 当前保留的语义

### 1. Agent 状态机

对每个 agent，当前 formal backend 会生成一个枚举状态变量：

- 变量名形如 `agent__<agent>__state`
- 枚举成员来自 agent 的 `states`
- `init(...)` 来自 agent 的 `initial_state`
- `next(...)` 来自 agent 的显式 `transition`

当前 lowering 规则：

1. 默认后继集合来自 agent declaration 里的显式 `transition`
2. 若存在 `flow for <agent>`，则对每个 handler 状态，formal backend 会收集所有可达控制路径上的 `goto` 目标，并用这些目标覆盖 declaration-level 后继集合
3. final-state handler 的 `return` 会把该状态视为控制流终结点，因此对应状态在 SMV 中保持自循环
4. 若某状态没有可达后继，则该状态自循环

### 2. Workflow 节点状态

对每个 workflow node，当前 formal backend 会生成一个枚举状态变量，以及两个布尔状态位：

- 变量名形如 `workflow__<workflow>__node__<node>__state`
- `workflow__<workflow>__node__<node>__started`
- `workflow__<workflow>__node__<node>__completed`
- 节点状态集合直接复用目标 agent 的 `states`
- 节点初始状态复用目标 agent 的 `initial_state`
- 节点状态转移图复用目标 agent 的 flow-aware 状态机 lowering

若节点带有 `after` 依赖，则当前 lowering 会编码显式启动门控：

1. 无依赖节点的 `started` 初始即为 `TRUE`
2. 有依赖节点的 `started` 初始为 `FALSE`，直到所有 `after` 依赖节点都 `completed`
3. 在 `started = FALSE` 且依赖未满足时，节点停留在当前状态，不沿目标 agent 的状态机前进
4. `completed` 是一个 latch：节点一旦在已启动状态下进入目标 agent 的任一 final state，下一步起 `completed = TRUE`
5. 当前不会把 workflow 调度细化成更丰富的调度协议或消息传递语义

### 3. Temporal 原子映射

当前 formal backend 对受支持的 temporal atom 采用如下映射：

| AHFL 原子 | 当前 SMV 映射 |
|------|------|
| `called(k)` | `agent__<agent>__called__<capability>` 布尔 `IVAR` |
| `in_state(S)` | `(agent__<agent>__state = S)` |
| `running(n)` | `workflow__<workflow>__node__<node>__running` |
| `completed(n)` | `workflow__<workflow>__node__<node>__completed` |
| `completed(n, S)` | `(workflow__<workflow>__node__<node>__completed & (workflow__<workflow>__node__<node>__state = S))` |

其中：

1. `completed(n)` 当前是显式 workflow node 状态位，而不是单纯的 state-membership `DEFINE`
2. `running(n)` 当前通过 `DEFINE` 展开为 `started(n) & !completed(n)`
3. `called(k)` 当前只是 observation input，不带执行来源、次数、顺序或因果关系

### 4. Temporal 连接词映射

当前支持以下时序连接词映射：

| AHFL | SMV |
|------|-----|
| `always` | `G` |
| `eventually` | `F` |
| `next` | `X` |
| `not` / `!` | `!` |
| `and` | `&` |
| `or` | `|` |
| `implies` | `->` |
| `until` | `U` |

当前 `emit-smv` 只输出 `LTLSPEC`，不输出 CTL 或公平性约束。

### 5. Contract `requires` / `ensures` 导出

当前 formal backend 对 expr-shaped contract clause 采用受限导出：

- `requires: <pure-bool-expr>`
- `ensures: <pure-bool-expr>`

其规则固定为：

1. `requires` / `ensures` 不直接 lower 成可求值的数据表达式树，而是先进入共享 `formal_observations`
2. `requires` 当前被视为初始态前置条件，导出为 `LTLSPEC <contract_observation>`
3. `ensures` 当前只在 agent 进入任一 final state 时受检，导出为 `LTLSPEC G((final_state(agent)) -> <contract_observation>)`
4. 该导出保留的是 contract 边界，不是 output/dataflow 的完整执行语义
5. `invariant` / `forbid` 的 temporal lowering 规则保持不变，仍按现有 temporal exporter 路径处理

## Observation Abstraction

当前最重要的抽象边界是：

- contract 语境中的纯布尔 `Expr`
- temporal formula 中内嵌的纯布尔 `Expr`

例如：

- `always (not called(Decide) or ready(input.id))`

其中 `ready(input.id)` 当前不会被 lower 成可求值的符号表达式树，而会被抽象为新的布尔 observation 变量。

当前规则固定为：

1. 每个 embedded pure boolean expr 都会先进入共享的 `formal_observations` 模型，再由 backend-specific emitter 决定如何消费
2. observation scope 当前显式区分 `contract_clause`、`workflow_safety_clause`、`workflow_liveness_clause`
3. observation 名字由 scope 所属 owner、clause 索引和遍历顺序稳定决定
4. 当前不会跨 clause、跨 contract、跨 workflow 做语义去重；同一 scope 内的同一 atom 位置只生成一个 observation symbol
5. 当前 observation 仅表示“环境可观察的布尔事实”，并不自动附带语义约束

当前命名来源：

- agent capability / `called(k)` observation
- contract `requires` / `ensures` expr
- contract temporal clause 内的 embedded expr
- workflow safety/liveness clause 内的 embedded expr

当前仓库里，这套 observation 模型已经被：

1. `emit-ir-json` 作为 `formal_observations` 顶层字段输出
2. `emit-smv` 用来生成布尔 `IVAR` 和稳定 symbol 名字

在 V0.2 contract/export 冻结下，shared observation model 的职责也已经固定：

1. 承载 contract `requires` / `ensures`
2. 承载 contract temporal clause 内嵌纯布尔 expr
3. 承载 workflow safety/liveness 内嵌纯布尔 expr
4. 为 backend emitter 提供统一的 clause ownership 和稳定命名

## 当前不承诺的语义

以下能力当前明确不属于 `emit-smv` 的语义承诺范围：

1. `requires` / `ensures` expr 的直接数据求值或 output/value semantics 编码
2. `let`、赋值、`assert`、表达式求值对 transition relation 的直接编码
3. `goto` / final-state `return` 之外的更细粒度 statement 执行语义
4. capability 调用发生的时间点、次数、参数值和顺序
5. workflow 输入表达式、返回值表达式、数据依赖与值流
6. lifecycle、retry、timeout、quota、exception 语义
7. LLM/tool/native body 语义
8. CTL、公平性、部分顺序并发或分布式事务语义

换句话说，当前 `emit-smv` 是：

- 一个受限的 state-machine / flow-termination / workflow-lifecycle / temporal exporter

而不是：

- AHFL 完整执行语义的 model checker backend

## 对后续实现的约束

Issue 16 之后，formal backend 相关实现必须遵守以下约束：

1. 新增 lowering 能力时，必须明确写出“保留语义”还是“抽象语义”
2. 若采用 observation abstraction，必须说明 observation 的作用域、命名和是否去重
3. 任何 formal backend 都不得重新读取 parse tree 或 `raw_text`
4. backend-specific emitter 必须消费 validate 后的语义模型或稳定 IR
5. 若 `emit-smv` 继续扩张，必须先更新本文档，再新增 golden

## 当前状态

截至 V0.2 当前实现：

1. `emit-smv` 已覆盖受限的 state-machine / workflow-lifecycle / temporal / contract export 子集
2. contract/export 的“保留语义”和“抽象语义”边界已文档化
3. `emit-ir` / `emit-ir-json` / `emit-smv` 已通过统一 backend driver 分发
