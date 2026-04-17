# AHFL Core V0.2 Diagnostics Architecture

本文说明 AHFL Core V0.2 中 source-aware diagnostics 的对象模型、跨阶段传递方式和 project-aware 模式下的错误归属策略，面向需要修改前端、语义阶段或 CLI 输出的工程实现者。

关联文档：

- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [testing-strategy-v0.3.zh.md](./testing-strategy-v0.3.zh.md)
- [project-usage-v0.3.zh.md](../reference/project-usage-v0.3.zh.md)

## 目标

本文主要回答五个问题：

1. AHFL 当前如何表示一个 diagnostic。
2. 为什么 diagnostics 基础设施属于 `support` 层，而不是 frontend 私有实现。
3. 单文件模式和 project-aware 模式下，diagnostics 如何附着到 source。
4. 各阶段应该如何汇总和转发 diagnostics，而不是改写它们。
5. 新增错误检查时，应如何选择 `error` / `error_in_source` / `append_from_source`。

## 总体定位

diagnostics 的基础对象位于：

- `include/ahfl/support/source.hpp`
- `include/ahfl/support/diagnostics.hpp`

这意味着 diagnostics 是全编译器共享基础设施，服务于：

- frontend
- resolver
- typecheck
- validate
- CLI

它不是 parser 专用能力，也不是 CLI 的字符串格式化逻辑。

## 基础对象模型

### Source 相关对象

当前 source 基础对象包括：

- `SourceId`
- `SourcePosition`
- `SourceRange`
- `SourceFile`

其中：

1. `SourceRange`
   - 只表示偏移区间，不包含文件名。
2. `SourceFile`
   - 持有 `display_name` 与 `content`，并负责把 offset 转成行列号。
3. `SourceId`
   - 在 project-aware 模式下为跨 source 归属建立稳定标识。

这套设计有一个明确意图：

- range 与 source ownership 分离

因此，单独一个 `SourceRange` 不足以构成可展示的错误；只有在附着到 `SourceFile` 或外部上下文后，才能稳定渲染为 `path:line:column`。

### Diagnostic 对象

`Diagnostic` 当前包含：

- `severity`
- `message`
- `range`
- `source_name`
- `position`

关键点有两个：

1. `range` 是可选的。
2. `source_name` / `position` 也是可选的。

这表示 AHFL 允许两类 diagnostics：

1. source-attached
   - 能精确定位到某个文件和偏移。
2. generic
   - 只有消息，或暂时只有 range 但还未绑定 source。

## DiagnosticBag 的职责

`DiagnosticBag` 的职责不是“立刻打印错误”，而是：

1. 聚合 diagnostics
2. 允许跨阶段传递
3. 在最后统一 render

当前主要 API：

- `add(...)`
- `add_in_source(...)`
- `note(...)`
- `warning(...)`
- `error(...)`
- `note_in_source(...)`
- `warning_in_source(...)`
- `error_in_source(...)`
- `append(...)`
- `append_from_source(...)`
- `render(...)`

这套 API 体现了两种常见工作模式：

1. 当前阶段已经明确知道 source
   - 直接用 `*_in_source(...)`
2. 当前阶段拿到的是别处产出的 bag，需要补 source 归属
   - 用 `append_from_source(...)`

## 单文件模式的诊断路径

单文件模式下，主要路径是：

```text
parse_file
  -> ParseResult.diagnostics
  -> resolve/typecheck/validate result.diagnostics
  -> CLI render(std::cerr, parse_result.source)
```

这里有两个层次：

1. frontend parse 阶段
   - 常常只知道 range，还没有提前把 source_name / position 写死
2. CLI render 阶段
   - 可通过 `render(..., source)` 把 range 补成最终行列

这就是为什么 `Diagnostic::source_name` / `position` 可以为空，而 `render` 仍然接受一个可选 `SourceFile` 上下文。

### parser 错误的处理

`DiagnosticErrorListener` 当前在 frontend 中把 ANTLR 报错转成：

- `message`
- `SourceRange`

它不直接负责最终展示格式。

这条边界很重要：

1. parser 负责产生定位信息。
2. `DiagnosticBag` 负责保存它。
3. CLI 才负责最终渲染给用户。

## project-aware 模式的诊断路径

project-aware 模式下，diagnostics 路径更长：

```text
parse_file(source A)
  -> parse_result.diagnostics
  -> parse_project.append_from_source(...)
  -> ProjectParseResult.diagnostics
  -> resolve/typecheck/validate
  -> CLI render(std::cerr)
```

与单文件模式最大的不同是：

1. 最终 CLI render 不再依赖单一 fallback `SourceFile`。
2. 每条 diagnostic 在进入 project 级结果前，原则上都应已经带上自己的 source 归属。

这正是 `append_from_source(...)` 存在的原因。

## append_from_source 的作用

`append_from_source(other, source)` 的行为是：

1. 复制 `other` 中每条 diagnostic。
2. 若原 diagnostic 尚未带 `source_name`，则补上当前 `source.display_name`。
3. 若原 diagnostic 有 `range` 但没有 `position`，则基于当前 source 计算行列。

它的核心用途不是“简单 append”，而是：

- 将 source-local diagnostics 提升到 project/global bag 时，补全展示归属

这在 `Frontend::parse_project(...)` 中尤为关键，因为 `parse_file(...)` 产生的是单 source 结果，而 `ProjectParseResult` 需要承载多 source 汇总。

## Frontend 阶段的 diagnostics 策略

frontend 里当前大致有三类错误来源：

### 1. parser / lowering 内部错误

特点：

- 已知当前 `SourceFile`
- 通常有精确 `SourceRange`

推荐方式：

- `error_in_source(...)`

### 2. project graph 构建错误

例如：

- entry file 缺失
- import 目标无法解析
- source file 缺少 module
- duplicate module owner

特点：

- 常常同时涉及“报错文件”和“请求来源文件”

推荐方式：

1. 主错误附着到当前被装载或被检查的 source。
2. 辅助 note 附着到 import 发起处或先前 owner 处。

这也是为什么 `parse_project(...)` 会同时产生：

- `error_in_source(...)`
- `note_in_source(...)`

### 3. graph 级但无具体 source 的输入错误

例如：

- `entry_files` 为空
- `search_roots` 为空

这种情况下没有自然的 source owner，应使用：

- `error(...)`

不要伪造一个文件路径或空 range 来冒充 source-local 错误。

## 语义阶段的 diagnostics 策略

`resolver`、`typecheck`、`validate` 当前都采用相似模式：

- 保存 `current_source_`
- 保存 `current_source_id_`
- 在报错时优先走 `error_in_source(...)`

这样做的意义是：

1. 单文件和 project-aware 模式共用一套错误生成逻辑。
2. 多 source 下可以把报错定位回当前 declaration / expr 所属文件。
3. 某些 cross-source note 可以通过 `source_id` 再找到原 source。

### Resolver

当前常见错误包括：

- duplicate declaration
- duplicate import alias
- unknown reference
- ambiguous callable
- type alias cycle

当 resolver 需要指出“上一个声明在别处”时，会额外发 note 到原 source，而不是把两处位置揉进一条字符串。

### TypeCheck

当前 typecheck 错误通常是 expression-local：

- 条件不是 `Bool`
- 不可赋值
- workflow node input 类型不匹配
- contract expr 不 pure

这类错误原则上都应附着到当前 expr / statement 的 source range，而不是 declaration 顶层范围。

### Validate

当前 validate 错误更偏结构级：

- unknown state
- illegal goto
- missing handler
- workflow dependency cycle
- temporal atom 用在错误语境

即便它们是“结构错误”，仍应尽量附着到触发该检查的 declaration 或语法节点，而不是退化成全局文本。

## render 的职责边界

`DiagnosticBag::render(...)` 当前负责把 diagnostic 转成：

```text
<severity>: <message> (<source>:<line>:<column>)
```

它不负责：

- 决定某条错误是否存在
- 给错误挑选归属 source
- 重写 message 文案

这意味着：

1. 归属应在产生日志的阶段就决定。
2. render 只是最后的格式化和 fallback 位置计算。

## note 的使用原则

AHFL 当前已经在 project-aware frontend 和 resolver 中大量使用 `note` 辅助主错误定位。

推荐规则：

1. 主错误表达“哪里不成立”。
2. note 表达“相关上下文在哪里”。

典型场景：

1. duplicate module owner
   - 主错误在当前文件
   - note 指向 previous owner
2. import 目标无法解析或 module mismatch
   - 主错误在被请求或当前 source
   - note 指向 import 发起处
3. duplicate declaration / alias
   - 主错误在后一个位置
   - note 指向先前位置

不要把这些上下文都塞到主错误 message 里，否则会损失多 source 定位能力。

## 新增 diagnostics 时的判断标准

### 何时用 `error(...)`

当错误没有自然 source owner 时，例如：

- CLI / project input 级配置错误
- 缺少 entry files
- 无 search roots

### 何时用 `error_in_source(...)`

当当前阶段已经明确知道：

- 哪个 `SourceFile`
- 哪个 `SourceRange`

这是最常见的选择。

### 何时用 `append_from_source(...)`

当你在做：

- 多 source 结果汇总
- 把下层阶段返回的 bag 提升到更高层结果

并且需要补全 source 名和行列时。

不要把它误用为普通 append；若 diagnostics 原本就是全局无 source 错误，`append(...)` 更合适。

## 对测试的影响

diagnostics 架构直接影响测试策略，尤其是 project-aware 测试。

例如 `tests/project/project_parse.cpp` 当前不仅检查 message，还会检查：

- 报错文本里是否出现正确的 `path:line:column`
- 主错误和 note 是否分别附着到正确 source

这说明 diagnostics 的“归属是否正确”本身就是公开边界，而不只是消息内容。

## 推荐阅读顺序

建议按下面顺序读 diagnostics 链路：

1. `include/ahfl/support/source.hpp`
2. `include/ahfl/support/diagnostics.hpp`
3. `src/frontend/frontend.cpp`
4. `src/frontend/project.cpp`
5. `src/semantics/resolver.cpp`
6. `src/semantics/typecheck.cpp`
7. `src/semantics/validate.cpp`
8. `src/cli/ahflc.cpp`

阅读重点：

1. 先看 `Diagnostic` / `DiagnosticBag` 的字段和 helper。
2. 再看 `src/frontend/project.cpp` 如何在 project/source-graph 装载时汇总 per-source diagnostics。
3. 最后看 CLI 如何在单文件和 project-aware 模式下分别 render。

## 对后续实现的约束

后续继续扩展 diagnostics 时，应保持以下原则：

1. `SourceRange` 只表示区间，不隐式携带文件归属。
2. 多 source 汇总时，优先保留每条错误自己的 source 归属。
3. 主错误和相关 note 分开表达，不要把多位置信息压成一行字符串。
4. 新的 project-aware 错误若能附着到具体 import / module 声明，就不要退化成 generic error。
5. CLI 负责 render，不负责弥补语义阶段遗漏的归属信息。
