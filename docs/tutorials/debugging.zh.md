# 调试 workflow 的状态与变量

使用 `ahfl-dap` 在编辑器中设置断点，查看 Agent 当前状态、输入输出和节点结果。步进以状态转移为粒度，不逐条执行 handler 中的表达式；外部 HTTP 和模型服务不能进入内部调试。

调试适配器使用 Debug Adapter Protocol（DAP，编辑器与调试器之间的通信协议）。下面以 VS Code 为例，其他支持 DAP 的编辑器也可配置同一适配器。

## 准备调试器

安装 [AHFL VS Code 扩展](../reference/lsp-vscode-extension.zh.md)，并确认能找到 `ahfl-dap`。扩展依次查找：

1. 设置项 `ahfl.debugAdapterPath` 指定的路径。
2. 扩展内的 `server/ahfl-dap`。
3. `PATH` 中的 `ahfl-dap`。

需要从源码构建时，在仓库根目录运行：

```bash
case "$(uname -s)" in
  Darwin) BUILD_JOBS=$(sysctl -n hw.ncpu) ;;
  *) BUILD_JOBS=$(nproc) ;;
esac
cmake --preset dev
cmake --build --preset build-dev -j"$BUILD_JOBS" --target ahfl-dap
```

调试器位于 `build/dev/src/tooling/dap/ahfl-dap`。在 VS Code 设置中指定它的绝对路径：

```json
{
  "ahfl.debugAdapterPath": "/path/to/AHFL/build/dev/src/tooling/dap/ahfl-dap"
}
```

工具链构建必须启用 `AHFL_ENABLE_BACKEND_WASM`（默认开启）；否则启动调试时会报告 `debug adapter requires WASM backend support`。

## 创建一个不调用外部服务的示例

将以下内容保存为工作区根目录下的 `greeting.ahfl`：

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

## 配置并启动调试

创建 `.vscode/launch.json`：

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "ahfl",
      "request": "launch",
      "name": "AHFL: Debug greeting",
      "program": "${workspaceFolder}/greeting.ahfl",
      "workflow": "dap::simple_workflow::GreetingWorkflow",
      "input": {
        "_type": "dap::simple_workflow::Payload",
        "tag": "hello"
      }
    }
  ]
}
```

`program` 是源文件绝对路径；`workflow` 选择完整限定名称，省略时使用该文件的第一个 workflow。`input` 必须匹配 workflow 的输入类型：struct 使用 `_type`，enum 使用 `_enum` 和 `_variant`。不提供输入时默认是 `None`，不适用于本例的 `Payload`。

1. 在 `state Init {` 所在行左侧点击设置断点。
2. 按 F5，选择 `AHFL: Debug greeting`。
3. 暂停后，CALL STACK 显示当前 Agent 状态、节点 `greet` 和 workflow。
4. 在 VARIABLES 的 Agent State 中查看 `state`，在 Input/Output 中查看 `input`。
5. 在 WATCH 中输入 `input.tag`，结果应为 `"hello"`。
6. 按 F10（Step Over），在转入 `Done` 时暂停。
7. 按 F5 继续，workflow 完成。

## 设置有效断点

可设置断点的位置包括 state handler、Agent 声明和 workflow 节点声明所在行。空行、注释和 import 行不能命中。编辑器将不能命中的断点显示为未验证（DAP 字段 `verified: false`）；启动后可能更新验证状态。

`stopOnEntry` 不会使程序在入口暂停，请显式设置断点。不支持条件断点、logpoint（只输出日志的断点）和按 capability 名称设置的函数断点。

## 选择步进方式

| 操作 | 当前行为 |
| --- | --- |
| Continue（F5） | 继续到下一个断点或 workflow 结束 |
| Step Over（F10） | 在下一次状态转移处暂停 |
| Step Into（F11） | 当前与 Step Over 相同，不能进入外部服务 |
| Step Out（Shift+F11） | 在不同 Agent 的下一次状态转移处暂停；不支持按嵌套 workflow 深度退出 |

断点和步进命中同一位置时，编辑器按断点暂停处理。会话只提供一个逻辑线程，暂停时整个调试运行暂停。

## 查看状态、输入和结果

| VARIABLES 作用域 | 可见内容 |
| --- | --- |
| Agent State | 当前 `state`；不显示 handler 的局部变量 |
| Context | 当前为空，不能查看 `ctx` 字段实时值 |
| Input/Output | 当前 Agent 的输入，以及已产生的输出 |
| Workflow | workflow 输入、已完成节点结果和最终输出 |

结构化值的 struct 字段、enum 内容、list 和 map 可以逐层展开。变量只读，不能通过调试器修改。

调用栈显示当前状态、节点、workflow 和 capability。capability 帧不带源码位置；定位外部调用失败时还应查看 DEBUG CONSOLE 中的错误。

## 使用悬停与 WATCH

支持标识符和字段访问路径：

| 表达式 | 查看什么 |
| --- | --- |
| `input`、`output` | 当前 Agent 的输入或已完成输出 |
| `input.tag` | 输入 struct 的字段 |
| `greet`、`greet.message` | 已完成节点的结果或其字段 |
| `input.field.subfield` | 嵌套 struct 的字段 |

不支持算术、函数调用、索引等表达式。例如 `input.count + 1` 会返回“仅支持标识符和字段路径”的错误。查看计算结果时，选择已输出该结果的节点或 Agent。

## 定位启动与运行问题

| 问题 | 处理 |
| --- | --- |
| 找不到调试适配器 | 检查 `ahfl.debugAdapterPath`，确认文件存在并可执行 |
| 编译失败 | 在 DEBUG CONSOLE 读取错误，先用 `ahflc check` 修复源码 |
| 找不到 workflow | 检查 `workflow` 的完整限定名称和 `program` 路径 |
| 输入类型不匹配 | 核对 `_type`、enum 标识和字段类型 |
| 断点未验证 | 将断点移到 state handler 或节点声明所在行 |
| Context 或局部变量为空 | 使用可见的输入、节点结果和状态定位问题；当前不支持这些变量 |
| capability 失败 | 查看 DEBUG CONSOLE 中的诊断；外部调用配置与报告用法见 [执行指南](./execution.zh.md) |
| 想连接已运行的进程 | 当前仅支持 `launch` 启动，不支持远程或本地 `attach` |

需要编译命令参数时查 [CLI 命令参考](../reference/cli-commands.zh.md)，编辑器配置见 [VS Code 配置](../reference/lsp-vscode-extension.zh.md)。
