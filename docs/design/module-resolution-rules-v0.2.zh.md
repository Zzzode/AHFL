# AHFL Core V0.2 Module Resolution Rules

本文冻结 AHFL Core V0.2 在 project-aware 模式下从 `import`、qualified name 到 canonical symbol 的解析规则，面向需要修改 resolver、跨文件 typecheck 或 project-aware IR lowering 的工程实现者。

关联文档：

- [module-loading-v0.2.zh.md](./module-loading-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [frontend-lowering-architecture-v0.2.zh.md](./frontend-lowering-architecture-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)

## 目标

本文主要回答五个问题：

1. project-aware 模式下，模块名、local name 和 canonical name 分别代表什么。
2. import alias 如何参与 qualified name 归一化。
3. resolver 当前按什么顺序做 local / canonical lookup。
4. 哪些名字解析属于 resolver，哪些仍属于 loader 或后续 typecheck/validate。
5. 新增跨文件语义能力时，应该怎样复用现有解析规则。

## 总体定位

module resolution 位于：

- source graph 构建之后
- typecheck 之前

它解决的问题不是：

- 模块文件在哪里

而是：

- 当前 source 里的这个名字，到底指向哪个跨文件声明

因此它是 loader 和语义阶段之间的桥：

```text
module loading
  -> source graph
  -> module resolution
  -> typecheck / validate / IR
```

## 三种名字

当前 V0.2 下，至少要区分三类名字：

### 1. Module Name

例如：

- `app::main`
- `lib::types`

作用：

- 表示 source unit 的逻辑所有权

### 2. Local Name

例如当前 module 里声明的：

- `Request`
- `AliasAgent`

作用：

- 在当前 module 局部命名空间内参与声明注册和单段 lookup

### 3. Canonical Name

例如：

- `lib::types::Request`
- `lib::agents::AliasAgent`

作用：

- 作为跨模块稳定标识，供 typecheck、IR、backend 共享

当前 resolver 中 `canonical_name_for(local_name)` 的规则非常直接：

1. 若当前有 `module_name`
   - `canonical = module_name + "::" + local_name`
2. 否则
   - canonical 退化为 local name

## SymbolTable 的双索引模型

当前 `SymbolTable` 在每个 namespace 下同时维护两套索引：

1. `module_local_names`
2. `canonical_names`

这意味着 symbol lookup 不是一个单 map。

### module_local_names

作用：

- 在当前 module 下查找 local declaration

适合：

- 单段名字
- 当前 module 内声明

### canonical_names

作用：

- 在全局 project 维度查找稳定 canonical symbol

适合：

- 全限定名
- alias 归一化后的名字

## import alias 的解析规则

当前 resolver 只把带 alias 的 import 视为“局部名称改写入口”。

例如：

```ahfl
import lib::types as types;
```

随后：

```ahfl
types::Request
```

在 resolver 看来，不是一个新的命名空间，而是：

1. 先识别 `types` 是否为当前 source 的 import alias
2. 若是，则将其替换为 `lib::types`
3. 最终得到 canonical-like 名字 `lib::types::Request`

### 不带 alias 的 import

```ahfl
import lib::types;
```

当前只表示：

- source graph 依赖

不会自动注入：

- `types`
- `Request`

这样的本地简写名。

因此引用时仍需写：

- `lib::types::Request`

## alias 归一化顺序

当前 `normalize_name(...)` 的规则是：

1. 空名字返回空串。
2. 单段名字不做 alias 归一化。
3. 多段名字检查第一段是否为 import alias。
4. 若是，则把 alias 对应 module path 拆成 segments，拼回完整 qualified name。
5. 若不是，则保持原拼写。

这表示 alias 只影响：

- qualified name 的首段

不会影响：

- 单段 local name
- 中间段
- 非 import alias 的普通标识符

## lookup 顺序

当前 resolver 的 `lookup(...)` 顺序可概括为：

### 1. 单段名优先查当前 module local

若 `name.segments.size() == 1`，先查：

- 当前 namespace
- 当前 `module_name`
- 当前 local name

### 2. 再查原始拼写的 canonical

适用于：

- 已写全限定名
- 或 local lookup 失败后的全局匹配

### 3. 再查 alias 归一化后的 canonical

若 `normalize_name(name)` 改变了拼写，再去 canonical 表中查归一化结果。

这条顺序表达了一个很重要的优先级：

1. 当前 module local declaration
2. 原始 canonical spelling
3. alias-expanded canonical spelling

## namespace 规则

module resolution 不是对所有名字都统一查一个集合，而是分 namespace 进行：

- `Types`
- `Consts`
- `Capabilities`
- `Predicates`
- `Agents`
- `Workflows`

这意味着：

1. 同一 spelling 在不同 namespace 下可能合法共存。
2. 调用目标、类型名、workflow node target 等引用种类必须先决定自己的 namespace。

否则 resolver 就无法稳定判断“该去哪个索引表查”。

## 几类关键引用

### 1. 类型名

通过：

- `ReferenceKind::TypeName`

解析到：

- `SymbolNamespace::Types`

### 2. 调用目标

通过：

- `ReferenceKind::CallTarget`

同时尝试：

- `Capabilities`
- `Predicates`

若两边都命中，则报告：

- ambiguous callable

### 3. temporal capability

`called(...)` 走：

- `ReferenceKind::TemporalCapability`
- `SymbolNamespace::Capabilities`

### 4. 合同/flow/workflow 目标

这些 target 都是专门的 reference kind：

- `ContractTarget`
- `FlowTarget`
- `WorkflowNodeTarget`

这保证不同语境的错误消息和后续消费路径可区分。

## QualifiedValue 的特殊规则

`QualifiedValue` 当前并不总是一个“普通类型名解析”。

resolver 的处理分两步：

1. 先尝试把整个 qualified name 当作 const value 查找。
2. 若失败且它有多段，则取 owner name，再按 type 去解析 owner。

这条规则的意义是：

1. `A::B` 可能是 const，也可能是 enum/类型 owner 下的值。
2. resolver 至少要把“owner 是哪个 type”稳定下来，后续 typecheck 才能继续判断 variant/member 语义。

## source-local 生命周期

当前 import alias 的生命周期固定为：

- 只在声明它的 `SourceUnit` 内有效

在实现上，resolver 通过：

- `enter_source(...)`
- `leave_source()`
- `import_aliases_`

每进入一个 source 时重建本地 alias 表。

这意味着：

1. alias 不跨 source 传播。
2. 即便两个 source 使用同一个 alias 文本，也不会互相污染。
3. 后续 typecheck / IR 若需要理解 alias 结果，应依赖 resolver 产出的 reference/symbol，而不是重新维护一套 alias 表。

## diagnostics 归属规则

module resolution 错误通常应附着到：

- 当前 source 的引用位置

若需要指出另一个 declaration 位置，则通过：

- `note_for_source(...)`

附加到原 source。

这尤其适用于：

- duplicate declaration
- duplicate import alias
- ambiguous callable

## resolver 不该做的事

即使这篇文档叫 module resolution，resolver 仍然不该：

1. 读取文件系统
2. 决定 search root
3. 决定 module -> path 映射
4. 决定某个 type alias 最终展开成什么语义类型
5. 决定某个 temporal atom 在 contract/workflow 中是否合法

这些分别属于：

1. frontend loader
2. typecheck
3. validate

## 对 typecheck / IR 的影响

typecheck 和 IR lowering 当前都明确依赖 resolver 结果中的：

- `canonical_name`
- `source_id`
- `module_name`
- `ResolvedReference`

这意味着：

1. 解析规则一旦变化，会直接影响 type environment、IR naming 和 backend 输出。
2. 新增命名能力时，不能只改 resolver 错误消息，还要检查 typecheck/IR 的消费边界。

## 新增能力时的落点指南

### 新增 import 变体

先问：

1. 它是否改变 source graph 构建。
2. 它是否改变 local name 注入规则。
3. 它是否引入新的 alias 生命周期。

若第一项为真，先改 loader；若后二项为真，再改 resolver。

### 新增可命名 declaration

先决定：

1. 它属于哪个 `SymbolNamespace`
2. canonical name 如何生成
3. 哪些引用语境会指向它

### 新增 qualified 引用形态

先决定：

1. 它是 const lookup、type owner lookup，还是全新 reference kind。
2. 它的错误应该归入 unknown / ambiguous / invalid owner 中哪一类。

## 推荐阅读顺序

建议按下面顺序读：

1. `include/ahfl/semantics/resolver.hpp`
2. `src/semantics/resolver.cpp`
3. `docs/design/module-loading-v0.2.zh.md`
4. `src/semantics/typecheck.cpp`
5. `src/ir/ir.cpp`

## 对后续实现的约束

后续继续扩展跨文件解析时，应保持以下原则：

1. module loading 和 module resolution 分层处理，不要重新混回一个阶段。
2. alias 只作为 source-local qualified name 归一化入口，不升级成全局符号机制。
3. canonical name 始终作为跨模块稳定标识，供后续阶段共享。
4. 新的名字解析能力若会影响 IR/backend，应同步审查其 canonical naming 后果。
5. 若未来引入 manifest 或更复杂模块系统，应新增更高层设计文档，而不是直接破坏本文规则。
