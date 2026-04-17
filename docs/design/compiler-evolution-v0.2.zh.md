# AHFL Core V0.2 Compiler Evolution

本文给出 AHFL 编译器模块的实现概念与扩展方案，面向后续继续增加语法、语义规则、IR 节点或 backend 能力时的工程落地。

关联文档：

- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [compiler-phase-boundaries-v0.2.zh.md](./compiler-phase-boundaries-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)
- [roadmap-v0.2.zh.md](../plan/roadmap-v0.2.zh.md)

## 目标

本文不重新定义语言规则，而是回答：

1. 新增一个语言能力时，通常应该改哪些模块。
2. 每一层的实现概念是什么。
3. 怎样避免把一个局部需求改成跨层污染。

## 一个能力的标准落地路径

对 AHFL 来说，大多数新增能力都应沿着同一条链路扩展：

```text
grammar
  -> frontend AST/lowering
  -> resolve
  -> typecheck
  -> validate
  -> IR
  -> backend
  -> tests
  -> docs
```

并不是每次都要改到最末端，但默认应按这个顺序检查影响面。

## 各阶段的“该做什么”

### 1. Grammar / Parser

应该做：

- 定义 surface syntax。
- 让 parse tree 能区分新语法形态。

不该做：

- 语义裁决。
- 名称解析。
- 类型推导。

判断标准：

- 如果你在纠结“这个名字是不是某个已声明符号”，那已经不是 grammar 阶段的问题。

### 2. Frontend AST / Lowering

应该做：

- 为新语法建立 hand-written AST 节点。
- 把 parse tree lower 成后续可稳定消费的结构。
- 绑定 source range 与最早期 diagnostics。

不该做：

- 用字符串二次解析来替代 AST。
- 直接在 lowering 里偷偷做 resolver/typechecker 决策。

判断标准：

- 如果某个信息是后续阶段普遍要用的结构化语法事实，它就应该落进 AST。

### 3. Resolver

应该做：

- 定义新能力是否引入新的命名空间、引用种类或 import 行为。
- 让名字解析结果显式稳定。

不该做：

- 对表达式做类型判断。
- 对 flow/workflow 做领域规则校验。
- 重新读文件系统。

判断标准：

- 如果问题是“这个名字指向谁”，属于 resolver。
- 如果问题是“这个值能不能赋给那个类型”，不属于 resolver。

### 4. TypeCheck

应该做：

- 决定新 AST 节点的静态类型。
- 扩展 `TypeEnvironment` 或 `ExpressionTypeInfo`。
- 明确 purity、可赋值性、schema 兼容性等静态规则。

不该做：

- 验证状态机 reachability。
- 验证 workflow DAG。
- 用 type error 掩盖领域规则错误。

判断标准：

- 如果不需要知道运行时调度，仅靠声明和类型就能判断，通常属于 typecheck。

### 5. Validate

应该做：

- 检查结构约束、生命周期约束、graph 约束、temporal atom 约束。
- 保证 contract / flow / workflow 的“整体成立”。

不该做：

- 重新建立语义类型。
- 重新推导 expression type。

判断标准：

- 如果判断需要“看整体结构是否合理”，通常属于 validate。

### 6. IR

应该做：

- 决定新能力在稳定中间表示中的形状。
- 决定该能力是否需要新的 provenance、observation 或 stable field。

不该做：

- 依赖 parser trivia。
- 把 backend-specific 假设偷塞成 IR 唯一表达方式。

判断标准：

- 如果将来可能有多个 backend 都要消费它，这个能力应先冻结到 IR。

### 7. Backend

应该做：

- 说明新能力是“保留语义”还是“抽象语义”。
- 在 backend-specific 文档中冻结 lowering 边界。

不该做：

- 重新定义 source ownership。
- 直接绕过 IR 去消费 parse tree / raw text。

判断标准：

- 如果后端需要自己猜 declaration 属于哪个 source/module，说明 IR 边界还没定义好。

## 四类常见扩展方案

### 1. 新增顶层声明

例如：

- 新的 declaration kind

推荐顺序：

1. grammar 增加新 declaration。
2. AST 增加新节点。
3. resolver 决定是否进 symbol table、落在哪个 namespace。
4. typecheck 决定签名与依赖类型。
5. validate 决定结构规则。
6. IR 增加对应 declaration node。
7. backend 决定是否消费。

### 2. 新增表达式或语句

例如：

- 新的 literal
- 新的 operator
- 新的 flow statement

推荐顺序：

1. grammar + AST shape。
2. typecheck 决定类型规则、purity、assignability。
3. validate 只在该语句影响控制流或结构关系时介入。
4. IR 增加对应 expr/statement node。

### 3. 新增 temporal atom 或 contract clause

例如：

- 新的 `completed(...)` 变体
- 新的 temporal operator
- 新的 contract clause 类别

推荐顺序：

1. grammar + AST。
2. resolver 决定它是否引用 capability / workflow node / type / agent。
3. typecheck 决定 embedded expr 的类型约束。
4. validate 决定它在哪个语境允许使用。
5. IR 冻结新 temporal / contract node。
6. backend 决定保留还是抽象。

### 4. 新增 project-aware 能力

例如：

- 新的 source ownership 元数据
- 新的 project-level diagnostics

推荐顺序：

1. 先补公共对象模型，例如 `SourceGraph`、`provenance`、`source_id`。
2. 再补 diagnostics 归属。
3. 再补 CLI 和 golden。

不要反过来先开放命令，再回头补公共边界。

## 典型实现模式

### 模式 1：先公共头，后 `.cpp`

AHFL 当前很多阶段都采用“先定公共边界，再把 pass state 塞进 `.cpp` 内部类”的模式。

例如：

- `Frontend`
- `Resolver`
- `TypeChecker`
- `Validator`

这有两个好处：

1. 公共 API 保持窄。
2. pass 内部状态不污染头文件。

### 模式 2：多文件模式优先显式带上下文

V0.2 之后，很多查找都必须显式带：

- `module_name`
- `source_id`

如果一个 lookup 在多文件下可能歧义，就不要继续写成“只按名字查”的 helper。

### 模式 3：diagnostics 就近附着 ownership

一旦某个阶段已经知道错误属于哪个 source，就应该尽早把 source ownership 挂到 diagnostics 上，而不是等更后面阶段再猜。

### 模式 4：golden 先于“感觉正确”

对于 IR / JSON / SMV 这类稳定输出：

1. 先把结构和命名规则冻结。
2. 再补 golden。
3. 最后再扩 CLI。

否则很容易出现实现看似能跑，但输出边界不断漂移。

## 反模式

下面这些做法应尽量避免：

1. 在 checker/backend 里重新解析 `raw_text`。
2. 在 frontend lowering 里顺手做类型规则。
3. 在 validate 里重复写一份 resolver/typecheck 逻辑。
4. 在 CLI 里塞 loader / backend 的具体实现细节。
5. 在多文件模式下继续依赖“range 唯一”这种单文件假设。

## 一次改动前的自检问题

在动手前，建议先问自己：

1. 这个需求是语法问题、命名问题、类型问题、结构问题，还是 backend 问题？
2. 它是否需要新的公共对象？
3. 它在多文件模式下是否还成立？
4. 它需要新的 diagnostics ownership 吗？
5. 它是否应该先冻结到 IR，而不是直接在 backend 私自处理？

## 建议的最小交付单元

对 AHFL 当前仓库，比较稳妥的增量通常是：

1. 一次只扩一层主能力。
2. 至少补一条正例和一条负例。
3. 若改了 IR/JSON/SMV，就必须补 golden。
4. 若改了边界，必须补 `docs/design/` 或 `docs/reference/`。

这样做的目的不是保守，而是让编译器继续维持“边界先于实现细节”的演进方式。
