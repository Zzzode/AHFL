# AHFL Core V0.2 Compiler Phase Boundaries

本文冻结 AHFL Core V0.2 的编译阶段职责与中间表示边界，面向需要理解 `parse -> ast -> resolve -> typecheck -> validate -> emit` 主链的工程实现者。

关联文档：

- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [frontend-lowering-architecture-v0.2.zh.md](./frontend-lowering-architecture-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)

适用范围：

- `grammar/AHFL.g4`
- `src/parser/generated/*`
- `src/frontend/frontend.cpp`
- `include/ahfl/frontend/ast.hpp`
- `src/semantics/*`
- `src/ir/*`
- `src/backends/*`

## 冻结的前端流水线

AHFL Core 当前固定分为六个阶段：

1. `parse`
2. `ast`
3. `resolve`
4. `typecheck`
5. `validate`
6. `emit`

阶段顺序固定，不交叉回退，不允许后续阶段再直接读取 ANTLR parse tree。

## 各阶段输入输出

| 阶段 | 输入 | 输出 | 说明 |
|------|------|------|------|
| `parse` | `SourceFile` | ANTLR parse tree + syntax diagnostics | 只负责把源码按 `AHFL.g4` 解析成语法树 |
| `ast` | parse tree | `ast::Program` + source ranges + lowering diagnostics | 把 parse tree lower 成稳定的手写 AST |
| `resolve` | `ast::Program` | 符号表、名称绑定、解析后的声明引用 | 只做名字和作用域，不做类型规则裁决 |
| `typecheck` | AST + resolve 结果 | 语义类型、类型化表达式/语句、类型 diagnostics | 不再回看 parse tree 或原始字符串 |
| `validate` | typecheck 结果 | agent/contract/flow/workflow 级静态约束结果 | 检查状态机、白名单、合同限制、DAG 等 |
| `emit` | validate 通过的语义模型 | 稳定 IR | 供后续 checker backend / model checker / 其他后端使用 |

## 阶段边界约束

### 1. `parse`

职责：

- 调用 `grammar/AHFL.g4` 生成的 lexer/parser
- 产生 syntax diagnostics
- 仅在本阶段持有和遍历 ANTLR parse tree

禁止：

- 在 parse tree 上直接做 resolver、typecheck、validator 语义决策
- 将 ANTLR context、token stream、generated parser 类型暴露到公共头文件

## 2. `ast`

职责：

- 将 parse tree lower 为手写 AST
- 保留稳定的 `SourceRange`
- 为后续语义阶段提供与 parser 解耦的语法表示

AST 必须满足：

- 不保存 ANTLR context
- 不保存 ANTLR token 指针
- 不保存 parser-specific helper object
- 不直接携带名称解析结果
- 不直接携带类型检查结果

允许保留：

- declaration headline 所需的调试字段
- `raw_text` 这一类仅用于 dump/debug 的过渡字段

限制：

- `raw_text` 不能成为后续 resolver、typecheck、validator 的常规输入
- 后续语义阶段必须基于结构化 AST 工作，不能重新解析字符串

## 3. `resolve`

职责：

- 注册顶层声明
- 建立作用域
- 完成 name lookup
- 产出稳定的声明引用和符号表

禁止：

- 重新读取 ANTLR parse tree
- 通过 `raw_text` 重做语法分析
- 混入类型等价、状态机可达性、workflow DAG 校验这类后续阶段职责

## 4. `typecheck`

职责：

- 将 AST 类型节点解析为语义类型
- 推导和检查表达式、语句、合同子表达式的静态类型
- 形成稳定的类型 diagnostics

禁止：

- 重新做名称解析
- 直接读取 parse tree
- 将领域规则错误伪装成普通类型错误

## 5. `validate`

职责：

- 检查 agent / contract / flow / workflow 的领域级静态约束
- 例如状态跳转合法性、handler 完整性、capability 白名单、workflow DAG 无环

禁止：

- 重做 parse 或 AST lowering
- 通过未解析名称或未定型表达式继续推进

## 6. `emit`

职责：

- 将 validate 通过的语义模型 lower 成稳定 IR
- 使后续后端不再依赖 AST 细节和 grammar trivia

当前仓库中，`emit` 阶段已经分化出三类产物：

- `emit-ir`：人类可读的稳定文本 IR
- `emit-ir-json`：机器可读的稳定 JSON IR
- `emit-smv`：面向 model-check-oriented tooling 的受限 formal backend

其中 `emit-smv` 允许在 formal backend 中引入显式抽象，但抽象边界必须单独文档化，不能只藏在 emitter 实现里。

IR 必须满足：

- 不包含 ANTLR 类型
- 不依赖 `raw_text`
- 不要求消费方理解源语法细节

## 当前仓库中的映射

当前已经存在的实现映射如下：

- `Frontend::parse_file` / `Frontend::parse_text`
  - 当前同时覆盖 `parse + ast`
  - 对外只返回 `ParseResult`
- `src/frontend/frontend.cpp`
  - 当前是唯一允许直接接触 ANTLR parse tree 的手写模块
- `include/ahfl/frontend/ast.hpp`
  - 当前承载手写 AST 的公共边界
- `src/semantics/resolver.cpp` + `include/ahfl/semantics/resolver.hpp`
  - 当前承载 `resolve`
- `src/semantics/typecheck.cpp` + `include/ahfl/semantics/typecheck.hpp`
  - 当前承载 `typecheck`
- `src/semantics/validate.cpp` + `include/ahfl/semantics/validate.hpp`
  - 当前承载 `validate`
- `src/ir/ir.cpp` + `include/ahfl/ir/ir.hpp`
  - 当前承载 `emit`
  - 对外暴露稳定的 IR 数据结构与文本输出入口
- `src/backends/smv.cpp` + `include/ahfl/backends/smv.hpp`
  - 当前承载第一版 restricted formal backend
  - 只能消费 validate 之后的语义模型与 IR

后续新增这些模块时，必须沿用本文定义的阶段边界，不得把 ANTLR context 重新引入 AST 或 checker 公共接口。

## 所有权与引用规则

前端各阶段统一遵守以下规则：

- 树状拥有关系使用 `Owned<T>`，即 `std::unique_ptr<T>`
- 节点之间的可空借用引用使用 `MaybeRef<T>` / `MaybeCRef<T>`
- 语义对象可以引用 AST 节点，但不得拥有 parse tree
- diagnostics 只记录 `SourceRange` 与消息，不记录 parser context 指针

parse tree 的生命周期只限于 `parse -> ast` lowering 期间；一旦 `ast::Program` 构建完成，后续阶段都必须只消费 AST 与语义对象。

## 对后续实现的约束

后续继续扩展编译器时，默认采用本文边界：

1. 新增语法能力时，只能扩展 hand-written AST 和 lowering，不得把 parse tree 当 AST 的替代物。
2. 新增语义能力时，只能基于 AST、resolve 结果和 typecheck 结果工作，不得回看 parse tree。
3. IR 输出必须基于 validate 后的语义模型，而不是直接序列化 parse tree。
4. formal backend 也必须基于 validate 后的语义模型，不得绕过编译阶段边界。
