# AHFL Core V0.2 Project Usage

本文给出 AHFL Core V0.2 的 project-aware 用法参考，目标是让新贡献者可以直接按文档跑通多文件 source graph 场景。

关联文档：

- [source-graph-v0.2.zh.md](../design/source-graph-v0.2.zh.md)
- [module-loading-v0.2.zh.md](../design/module-loading-v0.2.zh.md)
- [cli-commands-v0.2.zh.md](./cli-commands-v0.2.zh.md)
- [ir-format-v0.2.zh.md](./ir-format-v0.2.zh.md)

## 术语

- `entry source`
  - 用户在 CLI 中直接传入的入口 `.ahfl` 文件。
- `search root`
  - project-aware loader 用来解析 `module -> file` 的根目录集合。
- `source graph`
  - 由 entry source、递归装载到的 source、以及 import edge 组成的稳定输入模型。
- `module ownership`
  - 一个 source file 对应一个声明的 `module`，该 module 拥有该文件里的顶层声明。

## 最小目录约定

V0.2 当前采用“module path 对应相对文件路径”的装载规则：

```text
<search-root>/<module path>.ahfl
```

例如：

```text
app::main     -> <root>/app/main.ahfl
lib::types    -> <root>/lib/types.ahfl
lib::agents   -> <root>/lib/agents.ahfl
```

一个最小 project 可以像这样组织：

```text
tests/project/check_ok/
  app/main.ahfl
  lib/types.ahfl
  lib/agents.ahfl
```

对应源码：

```ahfl
// app/main.ahfl
module app::main;
import lib::types as types;
import lib::agents as agents;

flow for agents::AliasAgent {
    state Init { goto Done; }
    state Done {
        return types::ResponseAlias { value: input.value };
    }
}

workflow MainWorkflow {
    input: types::RequestAlias;
    output: types::ResponseAlias;
    node run: agents::AliasAgent(input);
    liveness: eventually completed(run, Done);
    return: run;
}
```

```ahfl
// lib/types.ahfl
module lib::types;

struct Request { value: String; }
struct Context { value: String = "pending"; }
struct Response { value: String; }

type RequestAlias = Request;
type ContextAlias = Context;
type ResponseAlias = Response;
```

```ahfl
// lib/agents.ahfl
module lib::agents;
import lib::types as types;

capability Echo(request: types::RequestAlias) -> types::ResponseAlias;

agent AliasAgent {
    input: types::RequestAlias;
    context: types::ContextAlias;
    output: types::ResponseAlias;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}
```

## 常用命令

检查整个 project：

```bash
./build/dev/src/cli/ahflc check \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

查看 source graph 装载结果：

```bash
./build/dev/src/cli/ahflc dump-project \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

查看 project-aware AST：

```bash
./build/dev/src/cli/ahflc dump-ast \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

查看合并后的类型环境：

```bash
./build/dev/src/cli/ahflc dump-types \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

导出 project-aware IR：

```bash
./build/dev/src/cli/ahflc emit-ir \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

导出 project-aware JSON IR：

```bash
./build/dev/src/cli/ahflc emit-ir-json \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

导出 project-aware backend summary：

```bash
./build/dev/src/cli/ahflc emit-summary \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

导出 project-aware SMV：

```bash
./build/dev/src/cli/ahflc emit-smv \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

## 当前支持矩阵

project-aware 模式下，`--search-root` 当前支持：

- `check`
- `dump-ast`
- `dump-project`
- `dump-types`
- `emit-ir`
- `emit-ir-json`
- `emit-summary`
- `emit-smv`

## 装载与 ownership 规则

当前应遵守以下规则：

1. 每个 source file 必须声明一个 `module`。
2. `module` 名必须与相对路径匹配，例如 `lib::types` 对应 `lib/types.ahfl`。
3. 同一个 module 不能被多个文件同时拥有。
4. `import` 解析只依赖 `search root + module path`，后续阶段不直接读取文件系统。
5. 顶层声明的 canonical name 由所属 module 决定，因此多文件下 local name 可以重复，canonical name 不能冲突。

## 常见失败场景

- 缺失 module owner
  - 目标文件不存在，或没有声明预期的 `module`。
- module/file mismatch
  - 文件路径和 `module` 声明不一致。
- duplicate module owner
  - 不同文件声明了同一个 `module`。
- missing import
  - `import` 指向的 module 无法从任一 `search root` 解析。
- cross-file type mismatch
  - resolver 能解析目标声明，但 typecheck/validate 发现跨文件 schema 或 workflow 约束不匹配。

这些错误的正式回归样例都在 `tests/project/` 下。

## 对下游的含义

project-aware 模式对下游最重要的影响是：

1. checker/backend 的稳定输入不再是假定“单文件程序”，而是 source graph。
2. IR/JSON/SMV 虽然最终会摊平成单个输出，但声明 ownership 不再隐含，必须显式消费 provenance。
3. 如果你在工具链里缓存 declaration 结果，缓存键应基于 canonical name 和 provenance，而不是仅基于 local name。
