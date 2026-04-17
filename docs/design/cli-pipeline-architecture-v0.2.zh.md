# AHFL Core V0.2 CLI Pipeline Architecture

本文说明 `ahflc` 在 V0.2 阶段的命令解析、模式分叉和流水线驱动方式，面向需要新增 CLI 子命令、扩展 project-aware 入口或调整编译阶段编排的工程实现者。

关联文档：

- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [frontend-lowering-architecture-v0.2.zh.md](./frontend-lowering-architecture-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)
- [cli-commands-v0.3.zh.md](../reference/cli-commands-v0.3.zh.md)

## 目标

本文主要回答四个问题：

1. `ahflc` 当前如何从命令行参数分发到不同编译流水线。
2. 单文件模式和 project-aware 模式在 CLI 上如何分叉。
3. 各命令为什么共用同一条语义主链，而不是各自实现一套。
4. 如果要新增命令或选项，应该改哪里，哪些边界不能打破。

## 总体定位

当前 CLI 实现主要位于：

- `src/cli/ahflc.cpp`
- `include/ahfl/backend.hpp`

其中：

1. `src/cli/ahflc.cpp`
   - 命令解析与流水线编排
2. `include/ahfl/backend.hpp`
   - 对 backend driver 的兼容转发头

CLI 的职责是：

- 解析命令行
- 选择运行模式
- 驱动 frontend / semantics / backend
- 将 diagnostics 与输出写到标准流

CLI 不应负责：

- 文件系统模块解析细节
- resolver/typecheck/validate 内部规则
- backend-specific lowering 逻辑

## 命令解析模型

当前 `CommandLineOptions` 只承载少量显式状态：

- `dump_ast`
- `dump_types`
- `dump_project`
- `emit_ir`
- `emit_ir_json`
- `emit_smv`
- `search_roots`
- `positional`

这说明当前 CLI 采用的是：

- 一个轻量 action flag 模型，而不是层层子命令对象体系

### parse_command_line 的职责

`parse_command_line(...)` 当前只做三类事：

1. 识别帮助和已知选项
2. 识别显式子命令
3. 收集 positional 参数

它不做：

- 编译阶段执行
- diagnostics render
- 文件加载

这保证了参数解析和编译执行是两层不同职责。

### 动作冲突规则

CLI 当前通过 `action_count` 强制：

- 最多选择一个输出/动作模式

也就是说下面这些是冲突的：

- `dump-ast`
- `dump-types`
- `dump-project`
- `emit-ir`
- `emit-ir-json`
- `emit-smv`

而默认无显式动作时，CLI 落回：

- `check`

这条设计让 `check` 成为默认主链，而各种 dump/emission 是它的分支观察窗口。

## 模式分叉：单文件 vs project-aware

当前最重要的运行时分叉条件是：

```text
project_mode = !search_roots.empty()
```

这意味着 CLI 当前并没有一个单独的 `project` 子命令；project-aware 模式是由：

- `--search-root`

显式开启的。

### project-aware 约束

当前 CLI 约束是：

1. `dump-ast` 在 project-aware 模式下仍停在 frontend/lowering 边界。
2. `dump-ast` 的 project-aware 输出必须按 `source -> module -> program` 分组，而不是伪装成单一合并 AST。
3. `dump-project` 仍只负责 source graph 视图，不与 AST 视图重叠。

## project-aware 主链

当 `dump_project` 为真或 `project_mode` 为真时，CLI 当前采用如下链路：

```text
ProjectInput
  -> Frontend::parse_project
  -> Resolver::resolve(graph)
  -> TypeChecker::check(graph, ...)
  -> Validator::validate(graph, ...)
  -> emit_backend(...) / summary
```

这条主链有几个刻意设计：

1. `dump-project` 在 parse_project 之后立即返回。
2. 其余命令共享同一条 resolve/typecheck/validate 主链。
3. backend emission 全都建立在 validate 成功之后。

### diagnostics 的推进顺序

project-aware 模式当前按阶段顺序 render：

1. `project_result.diagnostics`
2. `resolve_result.diagnostics`
3. `type_check_result.diagnostics`
4. `validation_result.diagnostics`

一旦某阶段 `has_errors()` 为真，就在适当位置提前返回。

这意味着：

1. CLI 不会在 resolve 失败后继续硬跑 typecheck。
2. 也不会在 validate 失败后继续 backend emit。

## 单文件主链

单文件模式当前采用如下链路：

```text
parse_file
  -> optional dump-ast
  -> resolve(program)
  -> typecheck(program, ...)
  -> validate(program, ...)
  -> emit_backend(...) / summary
```

和 project-aware 模式相比，差别主要在：

1. frontend 输入是单个文件
2. diagnostics render 可使用 `parse_result.source` 作为 fallback source
3. 成功 summary 按 top-level declaration 统计，而不是按 source 数统计

### dump-ast 的插入位置

`dump-ast` 当前位于 parse 成功之后、resolve 之前。

这说明它的定位始终是：

- parser + AST lowering 的调试观察点

而不是：

- 已通过语义检查的某种高级视图

## emit_backend 的位置

CLI 当前所有 backend 导出都统一经由：

- `emit_backend(BackendKind, ...)`

在单文件和 project-aware 模式下都一致。

这条边界非常重要，因为它保证：

1. CLI 不关心具体是文本 IR、JSON IR 还是 SMV。
2. lowering 到 IR 的逻辑集中在 IR/backend 层，而不是 CLI。
3. 新 backend 加入后，只需在 driver 和 CLI action 映射处补一层，而不是重写主链。

## summary 输出的设计

当前 CLI 在“没有选择 dump/emission 动作且 validate 通过”时输出 summary。

单文件模式输出：

- top-level declaration 数
- symbol 数
- reference 数
- named type 数

project-aware 模式输出：

- source 数
- symbol 数
- reference 数
- named type 数

这说明 summary 的定位是：

- 对当前输入模型的最小成功确认

而不是稳定机器接口。

因此下游 automation 不应依赖 summary 文本作为结构化 API；若需要稳定机器接口，应使用 `emit-ir-json`。

## CLI 该做什么

应该做：

- 参数解析
- 运行模式选择
- 阶段顺序编排
- diagnostics 输出
- backend 动作分发

不该做：

- 在 CLI 内手写 module loader
- 在 CLI 内引入新的语义判定
- 在 CLI 内拼 backend-specific 中间表示

## 新增命令时的落点指南

### 新增只读观察命令

如果命令只是暴露已有阶段结果，例如新的 dump：

1. 先确定它观察的是哪一层边界。
2. 尽量复用现有主链，在合适阶段后返回。
3. 不要因为增加一个 dump 命令而复制整条 pipeline。

### 新增 backend 命令

建议顺序：

1. 先在 `BackendKind` 和 driver 中加入新 backend。
2. 再在 CLI 中加入 action flag 和 usage 文本。
3. 让 CLI 仍通过统一 `emit_backend(...)` 入口调用。

### 新增 project-aware 选项

先判断：

1. 这是 frontend 输入模型的一部分，还是 CLI 仅用于显示的参数。
2. 若属于装载规则，应优先进入 `ProjectInput`，而不是停留在 CLI 局部变量。

## 当前架构的限制

当前 CLI 仍有几个明确限制：

1. action 选择仍是 flag 聚合，不是可组合的多动作模型。
2. `dump-ast` 虽已支持 project-aware，但仍不是跨阶段复合调试报告。
3. project-aware 模式由 `--search-root` 隐式触发，而不是单独 mode 对象。
4. diagnostics 主要按阶段即时 render，尚未引入统一的跨阶段报告对象。

这些限制本身不是 bug，但意味着后续扩展时应谨慎：

1. 若要支持组合动作，需要先重新定义 CLI 输出契约。
2. 若要支持 project-wide AST 观察，需要先定义新的稳定输出边界。

## 推荐阅读顺序

建议按下面顺序读：

1. `src/cli/ahflc.cpp`
2. `include/ahfl/frontend/frontend.hpp`
3. `include/ahfl/semantics/resolver.hpp`
4. `include/ahfl/semantics/typecheck.hpp`
5. `include/ahfl/semantics/validate.hpp`
6. `include/ahfl/backends/driver.hpp`

阅读重点：

1. 先看 `CommandLineOptions` 和 `parse_command_line(...)`。
2. 再看 `run_cli(...)` 中 project-aware 与单文件分支。
3. 最后看 backend 发射和 summary 输出点。

## 对后续实现的约束

后续继续扩展 CLI 时，应保持以下原则：

1. CLI 继续作为 orchestration 层，而不是语义实现层。
2. 单文件与 project-aware 共用同一条语义主链，只在 frontend 输入模型处显式分叉。
3. backend 统一经 driver 分发，不在 CLI 中复制 emission 逻辑。
4. `dump-ast`、`dump-project`、`emit-*` 各自对应明确边界，不要让它们语义重叠。
5. 若某个新功能需要 CLI 直接理解 AST/IR 细节，先反查是否缺少一个更合适的公共边界。
