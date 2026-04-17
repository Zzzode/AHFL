# AHFL Core V0.2 Module Loading

本文冻结 AHFL Core V0.2 在多文件 `module` / `import` 方向的当前设计边界，作为 project-aware frontend、跨文件 resolver，以及后续 project-level regression 的统一约束。

关联文档：

- [module-resolution-rules-v0.2.zh.md](./module-resolution-rules-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)

适用范围：

- `docs/spec/core-language-v0.1.zh.md`
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`
- `docs/plan/roadmap-v0.2.zh.md`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `include/ahfl/semantics/resolver.hpp`
- `src/semantics/resolver.cpp`

## 目标

1. 冻结 V0.2 的多文件装载模型
2. 冻结 `module` / `import` 在 project-aware 模式下的装载与名称边界
3. 明确哪些错误属于 loader，哪些错误继续属于 resolver / checker

## 非目标

本文不定义：

1. runtime package manager
2. 远程模块仓库
3. 增量编译缓存
4. `AHFL Native` 的部署/发布语义

## 术语

### Source File

单个 `.ahfl` 源文件，对应一个 `SourceFile` 内容对象和一个物理路径。

### Entry Source

用户直接交给 CLI/frontend 的入口源文件。

### Module Owner

声明某个 `module foo::bar;` 且被 source graph 接受的唯一 source file。

### Source Graph

由 entry source 出发，经 `import` 递归装载得到的 source file 集合及其 import 边。

### Project-Aware Mode

不同于 V0.1 单文件 parse/check 的模式；frontend 需要从 entry source 出发，递归装载相关 source files，构建 source graph，再进入 resolve / typecheck / validate。

## V0.2 的兼容性立场

V0.2 保留两种编译入口：

1. 单文件兼容模式
2. project-aware 模式

兼容规则：

1. 单文件兼容模式继续允许沿用 V0.1 的 `parse_file` / `check <file>` 心智模型。
2. project-aware 模式是 V0.2 的新增正式能力。
3. 单文件兼容模式不自动递归装载其他源码。
4. project-aware 模式不回退成“仅把 `import` 当注释”。

## Module Ownership 规则

V0.2 在 project-aware 模式下固定采用以下规则：

1. 每个被装载进入 source graph 的 source file 必须且只能有一个 `module` 声明。
2. 一个 module 只能有一个 module owner。
3. 若两个 source files 声明相同 module，则属于 loader/project-level 错误，而不是普通 duplicate symbol 错误。
4. 无 `module` 声明的源码只能存在于单文件兼容模式；一旦进入 project-aware source graph，即视为错误。
5. `module` 名字是逻辑所有权，不等于文件路径字符串本身。

## Module 到文件路径的映射

V0.2 为了收窄实现复杂度，固定如下约定：

1. project-aware 模式从一个或多个 entry source 开始。
2. 每个 entry source 都附带一个或多个 search root。
3. 对 import 目标 `foo::bar`，loader 只尝试在 search root 下查找规范路径 `foo/bar.ahfl`。
4. V0.2 不支持对同一 module 进行多候选路径猜测。
5. V0.2 不支持在 project-aware 模式下通过“扫描整个目录树”隐式发现模块。

这样做的目的不是追求灵活，而是先冻结一个单义、可回归的装载规则。

## `import` 的语义

V0.2 明确区分两类 `import`：

1. `import foo::bar;`
2. `import foo::bar as baz;`

其语义固定如下：

1. `import` 首先是 source graph 装载请求。
2. 不带 alias 的 `import` 只声明依赖关系，不向本地作用域注入新的简写名字。
3. 不带 alias 的跨模块引用，仍应使用完整限定名，例如 `foo::bar::Order`。
4. 带 alias 的 `import` 会在当前 source file 的本地导入表中引入一个局部别名。
5. alias 只在声明它的 source file 内有效，不跨文件传播。
6. V0.2 不支持 wildcard import。
7. V0.2 不支持 re-export 语义。

这一点与当前 resolver 中已有的 alias-normalization 行为保持一致：alias 参与 qualified name 归一化，不带 alias 的 import 不生成额外本地名。

## Source Graph 构建规则

从 entry source 构建 source graph 时，固定遵守以下顺序：

1. 装载 entry source。
2. 读取并校验其 `module` 声明。
3. 收集该 source 的所有 `import` 目标。
4. 根据 search root 和 module-path 规则查找依赖源文件。
5. 对尚未装载的依赖模块递归重复上述过程。
6. 对已装载模块仅复用已有 source unit，不重复解析。

额外规则：

1. import graph 可以有环；loader 只负责去重和终止递归，不在本阶段拒绝环。
2. import graph 的环不自动等于语义错误。
3. 真正的非法循环仍由后续更具体的语义规则处理，例如 type alias cycle。

## Diagnostics 分层

V0.2 必须把以下错误留在 project-aware loader 层，而不是混进 resolver/typecheck：

1. import 目标模块不存在
2. module 到文件路径映射失败
3. 两个源文件声明同一 module
4. project-aware 模式下源文件缺失 `module`
5. search root 无法唯一解释当前 import 请求

以下错误继续属于 resolver/checker：

1. duplicate type / const / capability / agent / workflow
2. unknown type / unknown callable / ambiguous callable
3. contract / flow / workflow 的语义错误

换句话说，loader 负责“文件集合是否成立”，resolver/checker 负责“语言语义是否成立”。

## 对 resolver 的约束

进入 V0.2 后，resolver 不应再承担以下职责：

1. 根据 import 目标去读文件
2. 决定 module 到路径的映射
3. 报告 project root/search root 级错误

resolver 只消费已经冻结好的 source graph，并负责：

1. 注册跨文件顶层符号
2. 解析 alias-qualified 与 canonical-qualified 名称
3. 继续产出稳定的 resolved reference

更细的名称归一化、canonical name 和 lookup 顺序规则，见 [module-resolution-rules-v0.2.zh.md](./module-resolution-rules-v0.2.zh.md)。

## 与单文件兼容模式的关系

V0.2 明确保留以下兼容规则：

1. 单文件入口仍可直接工作，不要求 module-path 文件布局。
2. 只有在 project-aware 模式下，module owner 与 search root 规则才是强约束。
3. 单文件兼容模式不是 V0.2 新能力的实现基础；它只是保留给现有示例、测试和调试用法的兼容入口。

## 当前不承诺的能力

以下能力明确不在 V0.2 的 module loading 设计承诺范围内：

1. package manifest
2. 多版本依赖解析
3. vendor / registry / remote import
4. 条件导入
5. 基于 build graph 的增量重编译

## 对后续实现的约束

Issue 02 之后，多文件装载相关实现必须遵守以下规则：

1. 先按本文构建 source graph，再进入 resolver/typecheck/validate。
2. 新增 CLI 参数时，不得绕过本文定义的 module-path 规则。
3. 若后续要支持 manifest 或更灵活的 module lookup，必须新增单独设计文档，而不是直接改写本文假设。
4. project-aware regression tests 必须覆盖 missing module、missing import target、duplicate module owner 和 alias collision。
