# AHFL 调试指南

本文说明如何使用 `ahfl-dap` 调试 AHFL workflow。AHFL 的调试模型面向 workflow /
state-machine 语义：断点和步进发生在状态迁移、capability 调用和 workflow 节点边界，
而不是传统命令式语言的逐语句执行。这符合 AHFL 的心智模型——用户关心的是"agent 在
哪个状态"、"capability 返回了什么"，而不是表达式内部的求值顺序。

调试适配器通过 [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
（DAP）与 IDE 通信，任何支持 DAP 的客户端（VS Code、Neovim、Emacs 等）都可以接入。
设计与实现细节见 [RFC 0015](../rfcs/0015-dap-runtime-integration.zh.md)。

## 能力总览

| 能力 | 状态 | 说明 |
|------|------|------|
| Line breakpoint | 支持 | 在 state handler、agent 声明、workflow 节点所在行设置断点；`verified` 反映真实可断性 |
| 状态迁移暂停 | 支持 | 在 state handler 行设置断点即等价于"进入该状态时暂停" |
| Capability 调用暂停 | 基础设施已就位 | 调试钩子已接入 capability 调用路径，但 DAP 协议层面暂无设置入口（见[限制](#当前限制)） |
| 步进（next / stepIn / stepOut / continue） | 支持 | 以状态迁移和 capability 调用为粒度 |
| 调用栈 | 支持 | 返回 workflow / node / state / capability 帧栈，含源码位置 |
| 变量查看 | 部分支持 | Agent State、Input/Output、Workflow 作用域有真实数据；Context 作用域为空（见[限制](#当前限制)） |
| 结构化变量展开 | 支持 | struct 字段、enum payload、list、map 可递归展开 |
| Evaluate（hover / watch） | 部分支持 | 仅支持标识符 + `.field` 访问链（见[限制](#当前限制)） |
| Output 事件 | 支持 | capability 成功结果走 stdout，失败和编译错误走 stderr |
| 条件断点 / logpoint | 不支持 | `supportsConditionalBreakpoints: false` |
| 函数断点 | 不支持 | `supportsFunctionBreakpoints: false` |
| setVariable | 不支持 | 运行时变量只读 |
| 远程 attach | 不支持 | 仅支持 launch 模式 |

## 前置条件

### 构建 `ahfl-dap`

`ahfl-dap` 是 AHFL 工具链的一部分，随主构建一起编译。调试适配器依赖 WASM backend
（`AHFL_ENABLE_BACKEND_WASM`，默认 ON）；如果 WASM backend 被编译关闭，`launch`
请求会返回 `debug adapter requires WASM backend support` 错误。

```bash
cmake --preset dev
cmake --build --preset build-dev -j$(nproc) --target ahfl-dap
```

构建产物位于：

```text
build/dev/src/tooling/dap/ahfl-dap
```

### 安装

启用 `AHFL_INSTALL` 后，`ahfl-dap` 随工具链一起安装到 `CMAKE_INSTALL_PREFIX`：

```bash
cmake --preset dev -DAHFL_INSTALL=ON
cmake --build --preset build-dev -j$(nproc)
cmake --install build/dev
```

安装后 `ahfl-dap` 位于 `${CMAKE_INSTALL_PREFIX}/bin/`，可直接通过 PATH 访问。

### VS Code 扩展

VS Code 扩展（`tools/vscode`）已注册 `ahfl` debug type，提供 launch schema、
断点贡献和 debug adapter factory。扩展按以下优先级解析 `ahfl-dap` 路径：

1. `ahfl.debugAdapterPath` 设置项（显式指定）
2. 扩展内置 `server/ahfl-dap`（release VSIX 打包时附带）
3. PATH 中的 `ahfl-dap`

本地开发时，在 VS Code 设置中指向开发构建：

```json
{
  "ahfl.debugAdapterPath": "${workspaceFolder}/build/dev/src/tooling/dap/ahfl-dap"
}
```

## 快速开始

### 手动启动调试适配器

`ahfl-dap` 通过 stdin/stdout 读写 `Content-Length` frame 的 DAP 消息。直接用管道
发送手写 frame 容易出错（Content-Length 必须与 body 字节数精确匹配），仓库自带的
smoke test（`tests/scripts/dap_smoke.py`）覆盖了 initialize → disconnect 的基本
生命周期，是验证适配器可用性的最简方式：

```bash
python3 tests/scripts/dap_smoke.py ./build/dev/src/tooling/dap/ahfl-dap
```

该脚本构造 `Content-Length` frame、发送 `initialize` + `disconnect` 请求，并验证
响应中的 `supportsConfigurationDoneRequest` 能力。

### 在 VS Code 中调试

1. 打开一个 `.ahfl` 文件。
2. 在 state handler 行（`state Init {` 所在行）点击行号左侧设置断点。
3. 按 F5（或 Run → Start Debugging），选择 "AHFL: Debug workflow"。
4. 适配器编译源文件、启动 workflow，在断点命中时暂停。
5. 使用调试工具栏继续（Continue）、单步（Step Over / Step Into / Step Out）。
6. 在 VARIABLES 面板查看 agent 状态、输入输出和 workflow 变量。
7. 在编辑器中悬停变量名或在 WATCH 面板添加表达式进行 evaluate。

## Launch 配置

`launch` 请求接受以下字段：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `program` | string | 是 | `.ahfl` 源文件的绝对路径 |
| `workflow` | string | 否 | 要调试的 workflow 名；省略时使用源文件中第一个 workflow 声明 |
| `input` | JSON | 否 | workflow 输入值，使用 AHFL runtime JSON 格式（struct 需 `_type`，enum 需 `_enum` + `_variant`）；省略时为 `None` |

### VS Code launch.json

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "ahfl",
      "request": "launch",
      "name": "AHFL: Debug workflow",
      "program": "${file}",
      "stopOnEntry": false
    },
    {
      "type": "ahfl",
      "request": "launch",
      "name": "AHFL: Debug named workflow",
      "program": "${workspaceFolder}/examples/execution-demo/src/main.ahfl",
      "workflow": "IncidentWorkflow",
      "input": {
        "_type": "execution_demo::types::IncidentRequest",
        "ticket_id": "INC-001",
        "service": "billing",
        "severity": {"_enum": "execution_demo::types::Severity", "_variant": "High"},
        "customer_impact": true
      },
      "stopOnEntry": false
    }
  ]
}
```

> **注意**：`stopOnEntry` 字段在 VS Code 扩展的 launch schema 中存在，但当前
> 调试适配器不处理该字段——设置为 `true` 不会在入口处暂停。请使用断点代替。

### 直接发送 DAP launch 请求

如果不通过 IDE，可以直接构造 DAP 消息。以下是一个完整的 launch 请求体示例：

```json
{
  "program": "/path/to/workflow.ahfl",
  "workflow": "GreetingWorkflow",
  "input": {
    "_type": "dap::simple_workflow::Payload",
    "tag": "hello"
  }
}
```

## 断点

### 可断行

调试适配器在 launch 时从编译后的 IR Program 构建"可断行"集合。只有以下声明所在的
源码行可以设置断点：

- **state handler**（`state Init { ... }`）——最常用的断点位置，在 agent 进入该状态时暂停
- **agent 声明**（`agent GreeterAgent { ... }`）
- **workflow 节点**（`node greet: GreeterAgent(input);`）

`setBreakpoints` 响应中每个断点的 `verified` 字段反映该行是否可断：

```json
{
  "breakpoints": [
    {"id": 1, "verified": true, "line": 22},
    {"id": 2, "verified": false, "line": 25}
  ]
}
```

`verified: false` 表示该行没有可映射的 IR 声明（例如空行、注释、import 语句）。
在 launch 之后设置的断点会通过 WASM 执行描述符进行延迟分类，并通过非请求的
`breakpoint` 事件通知客户端更新验证状态。

### 断点命中

当 workflow 执行到断点所在的 state handler 时，调试适配器发射 `stopped` 事件：

```json
{
  "type": "event",
  "event": "stopped",
  "body": {
    "reason": "breakpoint",
    "threadId": 1,
    "allThreadsStopped": true
  }
}
```

AHFL runtime 是单线程事件循环，`allThreadsStopped: true` 表示一个 agent 暂停时
整个 runtime 暂停。

### 断点与步进的优先级

断点优先于步进：如果断点和 arm 的步进在同一位置完成，`stopped` 事件的 reason 是
`breakpoint` 而不是 `step`。

## 步进

调试适配器支持四种步进命令，粒度为状态迁移和 capability 调用：

| 命令 | DAP 请求 | 语义 |
|------|----------|------|
| Continue | `continue` | 取消所有待处理步进，恢复执行直到下一个断点或 workflow 结束 |
| Step Over | `next` | 从当前 `(agent, state)` 恢复，在下一个状态迁移或 capability 调用处暂停（不深入子 workflow） |
| Step Into | `stepIn` | 当前与 Step Over 行为相同；capability 是外部调用（HTTP / LLM），无法 step into，待 capability step-into 落地后在返回处暂停 |
| Step Out | `stepOut` | 记录当前 workflow 嵌套深度，在深度减 1 时暂停；子 workflow 调试支持落地前深度始终为 0 |

步进完成时发射 `stopped` 事件，reason 为 `step`。

## 调用栈

`stackTrace` 返回当前暂停位置的帧栈，栈顶在前。帧类型：

| 帧类型 | 显示名 | 源码位置 |
|--------|--------|----------|
| State | `agent::state`（如 `GreeterAgent::Init`） | state handler 所在行 |
| Node | workflow 节点名 | 节点声明所在行 |
| Workflow | workflow 名 | workflow 声明所在行 |
| Capability | capability 名 | 无（runtime 钩子不暴露调用点源码位置） |

帧栈使用替换语义而非 push/pop：同一 agent 的状态迁移替换栈顶 state 帧；新节点的
状态进入弹出前一节点的帧；capability 调用替换栈顶 capability 帧。这忠实地反映了
workflow runtime 的逐节点顺序执行模型。

示例响应：

```json
{
  "stackFrames": [
    {"id": 1, "name": "IntakeAgent::Normalize", "source": {"path": "/path/to/intake.ahfl"}, "line": 23, "column": 1},
    {"id": 2, "name": "intake", "source": {"path": "/path/to/main.ahfl"}, "line": 14, "column": 1},
    {"id": 3, "name": "IncidentWorkflow", "source": {"path": "/path/to/main.ahfl"}, "line": 10, "column": 1}
  ],
  "totalFrames": 3
}
```

## 变量

`scopes` 请求返回当前帧的作用域，`variables` 请求展开作用域或结构化值。

### 作用域

| 作用域 | variablesReference | 内容 |
|--------|-------------------|------|
| Agent State | `100 + agent_id` | 当前状态名（`state` 字段）；state handler 局部变量不可见 |
| Context | `200 + agent_id` | agent context 字段；**当前为空**——context 字段在 state handler 内部赋值，runtime 钩子不暴露 |
| Input/Output | `300 + agent_id` | agent 的 live input（从 workflow 节点参数表达式推断）和 output（通过 `node_completed_hook` 观测） |
| Workflow | `400` | workflow input、已完成节点的结果、workflow output |

### 结构化值展开

struct 字段、enum payload、list 和 map 值通过链式 `variablesReference`（>= 1000）
递归展开。嵌套值在 VARIABLES 面板中可以逐层点开。

### 线程

`threads` 请求始终返回单个逻辑线程：

```json
{"threads": [{"id": 1, "name": "ahfl-main"}]}
```

AHFL runtime 是单线程事件循环，不暴露多线程调试。

## Evaluate

`evaluate` 请求在暂停上下文求值表达式，支持 IDE hover 和 watch。

### 支持的表达式

仅支持**标识符 + `.field` 访问链**：

| 表达式 | 解析为 |
|--------|--------|
| `input` | 当前帧 agent 的 input 值 |
| `output` | 当前帧 agent 的 output 值（已完成时） |
| `input.field` | agent input 的 struct 字段 |
| `node_name` | workflow 节点结果（如 `intake`） |
| `node_name.field` | 节点结果的 struct 字段 |
| `input.field.subfield` | 嵌套 struct 字段访问 |

解析遵循 runtime 的作用域语义：`input` / `output` 命名当前帧 agent 的输入输出；
非限定标识符命名 workflow 节点结果或 agent input struct 的字段（runtime 将 input
struct 字段展平到 input 作用域）。

### 不支持的表达式

任意子表达式（算术运算、函数调用、运算符、索引）会被拒绝并返回明确错误：

```json
{"error": "cannot evaluate \"input.count + 1\": only identifier and field-access paths are supported"}
```

这是因为调试器持有 live runtime `Value` 绑定，而完整的表达式求值需要
parse → resolve → typecheck → lower 流水线，暂停的会话无法诚实地重建合成程序。

结构化求值结果会获得链式 `variablesReference`，可以在 WATCH 面板中继续展开。

## 事件

调试适配器在以下时机发射 DAP 事件：

| 事件 | 时机 | body |
|------|------|------|
| `initialized` | launch 成功后 | `{}` |
| `stopped` | 断点命中 / 步进完成 | `{reason, threadId, allThreadsStopped}` |
| `terminated` | workflow 完成 / 失败 / disconnect | `{}` |
| `output` | capability 结果 / 错误 / 编译错误 | `{category, output}` |

`output` 事件的 category：

| category | 时机 |
|----------|------|
| `stdout` | capability 调用成功，结果值通过 `value_to_json` 序列化 |
| `stderr` | capability 调用失败（error_message）、workflow 失败（诊断信息）、编译错误、launch 配置错误 |

workflow 失败时，`WorkflowResult` 的诊断信息会在 `terminated` 事件之前以 `stderr`
output 事件发送到 debug console。

## 示例：调试一个简单 workflow

以下示例基于 DAP 测试中使用的 fixture。创建 `greeting.ahfl`：

```ahfl
module dap::simple_workflow;

struct Payload {
    tag: String;
}

struct Greeting {
    message: String;
}

agent GreeterAgent {
    input: Payload;
    context: Unit;
    output: Greeting;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for GreeterAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Greeting {
            message: "hello",
        };
    }
}

workflow GreetingWorkflow {
    input: Payload;
    output: Greeting;

    node greet: GreeterAgent(input);

    return: greet;
}
```

### 调试步骤

1. **设置断点**：在 `state Init {` 所在行（第 24 行）点击设置断点。该行是
   GreeterAgent 的 state handler，可断性验证为 `verified: true`。

2. **启动调试**：按 F5，选择 "AHFL: Debug workflow"。适配器编译源文件、构建
   可断行集合、启动 workflow 执行线程。

3. **断点命中**：workflow 执行到 `Init` 状态时暂停，发射 `stopped` 事件
   （reason: `breakpoint`）。VS Code 停在第 24 行。

4. **查看调用栈**：CALL STACK 面板显示：
   - `GreeterAgent::Init`（greeting.ahfl:24）
   - `greet`（greeting.ahfl:39）
   - `GreetingWorkflow`（greeting.ahfl:35）

5. **查看变量**：VARIABLES 面板展开：
   - **Agent State**: `state` = `"Init"`
   - **Input/Output**: `input` = `{"_type":"dap::simple_workflow::Payload","tag":"hello"}`
   - **Workflow**: `input` = 同上，`greet` 节点结果尚未产生

6. **单步执行**：按 F10（Step Over）。workflow 恢复，在下一个状态迁移（`Init → Done`）
   处暂停。调用栈现在显示 `GreeterAgent::Done`。

7. **Evaluate**：悬停 `input` 或在 WATCH 中输入 `input.tag`，显示 `"hello"`。

8. **继续执行**：按 F5（Continue）。workflow 完成，发射 `terminated` 事件。
   debug console 显示 capability output（如果有）和 workflow 结果。

9. **查看 output**：Workflow 作用域现在包含 `greet` 节点结果
   `{"_type":"dap::simple_workflow::Greeting","message":"hello"}` 和
   workflow `output`。

## 当前限制

以下是当前实现的已知边界。这些限制是诚实的产品边界，不是 bug：

1. **Context 作用域为空**：agent context 字段在 state handler 内部通过 `ctx.xxx = ...`
   赋值，runtime 钩子不暴露 context 字段的实时值。Context 作用域存在但始终返回空。

2. **Agent State 作用域仅显示状态名**：state handler 内部的局部变量在 WASM 引擎中
   变更，runtime 不暴露。Agent State 作用域只包含 `state` 字段（当前状态名）。

3. **Capability 断点无 DAP 设置入口**：capability 调用钩子和断点检查逻辑已就位，
   但 DAP 协议的 `setBreakpoints` 只支持行断点，`setFunctionBreakpoints` 未启用。
   无法通过 DAP 客户端直接设置"在 capability `http.get` 调用时暂停"。

4. **Evaluate 仅支持标识符路径**：不支持算术、函数调用、运算符、索引等任意表达式。
   完整表达式求值需要合成程序的 parse → resolve → typecheck → lower 流水线，
   暂停的会话无法诚实重建。

5. **`stopOnEntry` 被忽略**：VS Code launch schema 中有 `stopOnEntry` 字段，但
   调试适配器不处理该字段。请使用断点代替。

6. **StepInto 与 StepOver 行为相同**：capability 是外部调用（HTTP / LLM），无法
   step into。StepInto 当前在第一个状态迁移处暂停，与 StepOver 相同。

7. **StepOut 深度跟踪受限**：workflow 嵌套深度跟踪随子 workflow 调试支持落地；
   当前深度始终为 0，StepOut 在状态迁移到不同 agent 时暂停。

8. **单线程模型**：`threads` 始终返回单个逻辑线程。AHFL runtime 是单线程事件循环，
   多 agent 通过顺序执行暴露，不支持并发暂停。

9. **无条件断点 / logpoint**：`supportsConditionalBreakpoints: false`，不支持
   条件断点和 logpoint。

10. **无远程 attach**：仅支持 launch 模式，不支持 attach 到已运行的进程。

11. **无 setVariable**：运行时变量只读，不支持修改变量值。

12. **Capability 帧无源码位置**：capability 调用帧的 `source` 和 `line` 为空，
    因为 runtime 钩子不暴露调用点的源码位置。

## 参考

- [RFC 0015: DAP Runtime Integration](../rfcs/0015-dap-runtime-integration.zh.md) — 设计文档与实现历史
- [CLI 命令参考](./cli-commands.zh.md) — `ahfl-dap` 入口与其他 CLI 命令
- [VS Code 扩展打包与发布](./lsp-vscode-extension.zh.md) — 扩展开发与 VSIX 打包
- [执行与包指南](./user-guide-execution.zh.md) — workflow 运行、provider 配置与运行期事件
- [Debug Adapter Protocol 规范](https://microsoft.github.io/debug-adapter-protocol/) — DAP 协议标准
