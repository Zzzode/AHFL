# AHFL Core V0.1 Issue Backlog

本文将里程碑拆成可直接复制为 GitHub issue 的任务文本。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

每个 issue 都包含：

1. 标题
2. 背景
3. 目标
4. 验收标准
5. 依赖
6. 主要涉及文件

## [x] Issue 01

标题：
冻结 AHFL Core V0.1 语言边界

背景：
当前 `docs/design/core-scope-v0.1.en.md` 与 `docs/spec/core-language-v0.1.zh.md` 在 Core 边界上仍有未完全统一之处，尤其是 `flow` 是否属于 Core V0.1。

目标：

1. 统一 Core V0.1 与 AHFL Native 的边界
2. 统一文档中的 `flow` 定位
3. 明确 V0.1 只承诺哪些语法和语义能力

验收标准：

1. `docs/design/core-scope-v0.1.en.md` 与 `docs/spec/core-language-v0.1.zh.md` 无边界冲突
2. `grammar/AHFL.g4` 与规范中的边界一致
3. `examples/refund_audit_core_v0_1.ahfl` 不再使用越界能力

依赖：
无

主要涉及文件：

- `docs/design/core-scope-v0.1.en.md`
- `docs/spec/core-language-v0.1.zh.md`
- `grammar/AHFL.g4`
- `examples/refund_audit_core_v0_1.ahfl`

## [x] Issue 02

标题：
冻结 AHFL Core V0.1 类型系统边界

背景：
规范、设计说明和当前 `types.hpp` 的内部类型集合尚未完全对齐。

目标：

1. 明确用户可写类型集合
2. 明确内部辅助类型是否暴露
3. 明确 `Float`、`Duration`、`Any`、`Never` 的定位

验收标准：

1. 规范与实现中的类型集合一致
2. 类型系统文档明确区分用户可写类型与内部类型
3. 示例代码不会依赖未冻结的类型行为

依赖：

- Issue 01

主要涉及文件：

- `docs/spec/core-language-v0.1.zh.md`
- `docs/design/core-scope-v0.1.en.md`
- `include/ahfl/semantics/types.hpp`

## [x] Issue 03

标题：
冻结前端阶段职责与中间表示边界

背景：
当前仓库已有 parser bootstrap，但 parse tree、AST、语义对象、checker 阶段边界尚未明确写死。

目标：

1. 明确 `parse -> ast -> resolve -> typecheck -> validate -> emit` 阶段
2. 明确每层的输入输出
3. 避免后续实现混用 ANTLR context 和 AST

验收标准：

1. 仓库中新增一份简短的前端架构说明
2. 后续 issue 默认以该说明为边界
3. AST 与 checker 不再直接依赖 parse tree 作为常规接口

依赖：

- Issue 01
- Issue 02

主要涉及文件：

- `docs/design/frontend-architecture-v0.1.zh.md`
- `docs/plan/roadmap-v0.1.zh.md`
- `src/frontend/frontend.cpp`
- `include/ahfl/frontend/ast.hpp`

## [x] Issue 04

标题：
扩展顶层 AST，使 declaration 节点承载完整语义信息

背景：
当前 AST 顶层节点只保存名字、目标名和原始文本，不足以支撑 resolver 和 checker。

目标：

1. 为 `struct`、`enum`、`capability`、`predicate`、`agent`、`contract`、`flow`、`workflow` 增加完整字段
2. 移除对 `raw_text` 的语义依赖

验收标准：

1. 每类 declaration 节点都包含后续 checker 所需的结构化信息
2. `headline()` 仍可保留用于调试输出
3. 语义分析不再需要从字符串重新解析 declaration 内容

依赖：

- Issue 03

主要涉及文件：

- `include/ahfl/frontend/ast.hpp`
- `src/frontend/ast.cpp`

## [x] Issue 05

标题：
为 AHFL 增加完整类型 AST

背景：
类型系统已有 `TypeKind` 雏形，但 parser frontend 尚未产出完整类型 AST。

目标：

1. 建模 primitive、named、optional、list、set、map、bounded string、decimal scale
2. 为后续类型解析提供稳定输入

验收标准：

1. 所有语言中的类型语法都能映射到 AST
2. 类型 AST 不依赖字符串拼接表示
3. `Decimal(2)`、`String(10, 100)` 等 refinement 可被结构化表示

依赖：

- Issue 02
- Issue 04

主要涉及文件：

- `include/ahfl/frontend/ast.hpp`
- `include/ahfl/semantics/types.hpp`
- `src/frontend/frontend.cpp`

## [x] Issue 06

标题：
为表达式、语句与时序公式增加分层 AST

背景：
V0.1 设计强调 value expr、predicate expr、temporal formula 分层，但当前实现尚未建模。

目标：

1. 为 value expression 建模
2. 为 flow statement 建模
3. 为 temporal formula 建模

验收标准：

1. `requires/ensures` 与 `invariant/forbid` 使用不同 AST 类型
2. `goto`、`return`、`assert`、`if`、assignment 都有明确语义节点
3. workflow temporal formula 与普通表达式不混淆

依赖：

- Issue 04
- Issue 05

主要涉及文件：

- `include/ahfl/frontend/ast.hpp`
- `src/frontend/frontend.cpp`

## [x] Issue 07

标题：
完成 ANTLR parse tree 到 AST 的 lowering

背景：
当前 `ProgramBuilder` 只抽取顶层 declaration 名字，尚未下钻构建完整 AST。

目标：

1. 完整遍历 parse tree
2. lower 所有 declaration、类型、表达式、语句、时序公式
3. 保留稳定 source range

验收标准：

1. 示例文件能 lower 成完整 AST
2. lowering 失败时有稳定 diagnostics
3. `raw_text` 不再是 checker 的前置依赖

依赖：

- Issue 04
- Issue 05
- Issue 06

主要涉及文件：

- `src/frontend/frontend.cpp`
- `include/ahfl/frontend/frontend.hpp`
- `include/ahfl/support/source.hpp`

## [x] Issue 08

标题：
升级 `ahflc --dump-ast` 输出

背景：
当前 `--dump-ast` 仅输出顶层 declaration 列表，调试价值有限。

目标：

1. 输出完整 AST 树形结构
2. 支持 declaration、类型、表达式、时序公式、workflow 节点可视化

验收标准：

1. 示例文件的 `--dump-ast` 能看见字段、参数、状态、clause、node
2. 输出结构适合人工调试和 golden test

依赖：

- Issue 07

主要涉及文件：

- `src/cli/ahflc.cpp`
- `src/frontend/ast.cpp`
- `include/ahfl/frontend/ast.hpp`

## [x] Issue 09

标题：
实现符号表与名称解析

背景：
静态检查的第一前提是声明注册、作用域与引用解析。

目标：

1. 注册顶层声明
2. 实现重名检查
3. 实现未声明引用检查
4. 实现 type alias 循环检查
5. 处理 import 与 qualified name

验收标准：

1. 规范 5.1 声明级检查落地
2. diagnostics 能定位到声明和使用位置
3. resolver 产出稳定的语义引用

依赖：

- Issue 07

主要涉及文件：

- `include/ahfl/*`
- `src/*`

## [x] Issue 10

标题：
实现类型解析与类型等价规则

背景：
当前类型系统只有数据结构，没有真正接入 resolver/checker。

目标：

1. 将 AST 类型解析成语义类型
2. 实现类型等价与精确 schema 匹配
3. 明确允许与禁止的隐式规则

验收标准：

1. struct/schema 能完成精确匹配
2. capability 调用可检查参数与返回值
3. workflow 输入输出可检查

依赖：

- Issue 05
- Issue 09

主要涉及文件：

- `include/ahfl/semantics/types.hpp`
- `src/*`

## [x] Issue 11

标题：
实现表达式与语句类型检查

背景：
合同检查、flow 检查、workflow 节点输入检查都依赖表达式类型检查。

目标：

1. 检查字面量、路径访问、调用、结构体字面量、集合字面量
2. 检查 `let`、assignment、`if`、`assert`
3. 为 capability 调用区分合法上下文

验收标准：

1. 表达式都有静态类型
2. 错误表达式会产生稳定 diagnostics
3. `if` 与 `assert` 条件强制为 `Bool`

依赖：

- Issue 09
- Issue 10

主要涉及文件：

- `src/*`
- `include/ahfl/semantics/types.hpp`

## [x] Issue 12

标题：
实现 contract checker

背景：
合同是 AHFL Core 的核心之一，但当前只有 parser，没有语义检查。

目标：

1. 检查 `requires/ensures` 为纯布尔表达式
2. 检查 `invariant/forbid` 为合法时序公式
3. 限制合同中的可用符号与内建 predicate

验收标准：

1. 合同中 capability 调用会被拒绝
2. 非法时序公式会被拒绝
3. 状态引用、上下文引用、输出引用都能正确校验

依赖：

- Issue 06
- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `src/*`
- `docs/spec/core-language-v0.1.zh.md`

## [x] Issue 13

标题：
实现 agent 与 flow validator

背景：
agent 状态机与 flow handler 的一致性是语言最核心的静态保证之一。

目标：

1. 检查状态集合法性、可达性、终态无出边
2. 检查 handler 完整性
3. 检查 `goto` 合法性
4. 检查 flow 内 capability 白名单约束

验收标准：

1. 规范 5.2 与 5.4 的检查项落地
2. 不可达状态、非法跳转、缺失 handler 等情况能稳定报错

依赖：

- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `src/*`
- `docs/spec/core-language-v0.1.zh.md`

## [x] Issue 14

标题：
实现 workflow validator

背景：
workflow 是 agent 编排边界的核心，但当前尚无 DAG、依赖与类型检查。

目标：

1. 检查节点名唯一
2. 检查节点绑定 agent 存在
3. 检查 `after` 依赖存在
4. 检查 DAG 无环
5. 检查节点输入表达式可见性
6. 检查 workflow `return` 类型匹配

验收标准：

1. 规范 5.5 检查项落地
2. workflow 环、非法依赖、错误 return 都能报错

依赖：

- Issue 09
- Issue 10
- Issue 11

主要涉及文件：

- `src/*`
- `docs/spec/core-language-v0.1.zh.md`

## [x] Issue 15

标题：
建立测试体系、扩展 CI 并增加 IR 输出

背景：
当前仓库已经有 parser/resolver 基础回归和 CI 执行能力，但缺少更完整的 checker golden tests 与稳定 IR 输出。

目标：

1. 建立 `ctest` 测试集
2. 覆盖 parser 正例与负例
3. 覆盖 checker 正例与负例
4. 增加 `emit-ir` 作为稳定中间输出
5. 将测试接入 CI

验收标准：

1. 新增 parser/checker golden tests
2. CI 会执行测试而不仅是 smoke build
3. `ahflc` 能输出稳定 IR

依赖：

- Issue 08
- Issue 12
- Issue 13
- Issue 14

主要涉及文件：

- `CMakeLists.txt`
- `.github/workflows/ci.yml`
- `src/cli/ahflc.cpp`
- `tests/`

## [x] Issue 16

标题：
提交并冻结第一版 `emit-smv`

背景：
当前仓库已经具备结构化 IR、`emit-ir` 和 `emit-ir-json`。工作区内还存在一版 `emit-smv` 原型，但尚未完成提交、文档冻结和正式回归收口。

目标：

1. 提交当前 `emit-smv` 实现
2. 将 `emit-smv` 接入正式回归测试
3. 在 README 和路线图中明确其定位是“受限 formal backend”

验收标准：

1. `ahflc emit-smv <file>` 可稳定输出
2. `ctest` 中包含至少一条 `emit-smv` golden test
3. 文档明确说明 `emit-smv` 的能力边界与限制

依赖：

- Issue 15

主要涉及文件：

- `include/ahfl/backends/smv.hpp`
- `src/backends/smv.cpp`
- `src/cli/ahflc.cpp`
- `CMakeLists.txt`
- `README.md`
- `tests/formal/`

## [x] Issue 17

标题：
冻结 AHFL Core V0.1 的 formal subset 语义

背景：
`emit-smv` 已经打开了后端 lowering 的入口，但目前“哪些语义可以 lower，哪些语义会被抽象”仍然主要体现在实现里，缺少正式文档约束。

目标：

1. 明确 AHFL Core V0.1 中可 lower 到 formal backend 的受限子集
2. 明确 embedded pure expr 的 observation abstraction 规则
3. 明确 `called`、`in_state`、`running`、`completed` 到 formal backend 的映射边界

验收标准：

1. 仓库新增 formal backend 语义说明文档
2. 规范与实现对于 formal subset 的约束一致
3. `emit-smv` 的抽象策略可以从文档直接读出，而不需要反推代码

依赖：

- Issue 16

主要涉及文件：

- `docs/spec/core-language-v0.1.zh.md`
- `docs/design/core-scope-v0.1.en.md`
- `docs/plan/roadmap-v0.1.zh.md`
- `docs/design/frontend-architecture-v0.1.zh.md`
- `docs/design/formal-backend-v0.1.zh.md`

## [x] Issue 18

标题：
扩展 `emit-smv` 到 flow / workflow 语义级 lowering

背景：
当前 `emit-smv` 主要依赖声明级状态机和时序公式进行导出，尚未充分利用 `flow` 中的控制流信息，也没有把 workflow 调度语义编码到足够细的粒度。

目标：

1. 从 `flow` 中抽取更接近执行语义的状态转移关系
2. 完善 workflow node 启动条件、依赖和完成条件的 lowering
3. 让 `emit-smv` 不只是 declaration exporter，而是 flow/workflow 语义 lowering

验收标准：

1. `goto`、final-state `return`、state handler 终结行为能影响 SMV lowering
2. workflow `after` 依赖与 node 完成条件被编码到输出中
3. 至少新增一组覆盖 flow/workflow 语义的 SMV golden

依赖：

- Issue 16
- Issue 17

主要涉及文件：

- `src/backends/smv.cpp`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `tests/formal/`

## [x] Issue 19

标题：
建立 formal observation abstraction 层

背景：
当前 `emit-smv` 对 embedded pure expr 的处理仍偏直接，observation symbol 的命名、去重和作用域规则还没有抽出成稳定层，后续会影响更多 formal backend。

目标：

1. 抽象独立的 observation collection / naming 机制
2. 定义 observation 的作用域、去重和命名稳定性规则
3. 让 JSON / SMV / 后续 formal backend 能共享同一套 observation 模型

验收标准：

1. observation 不再散落在 `smv.cpp` 内部 ad-hoc 生成
2. 同一语义 observation 在不同后端输出中命名稳定
3. 新增针对 observation abstraction 的 golden 或单元测试

依赖：

- Issue 17
- Issue 18

主要涉及文件：

- `include/ahfl/*`
- `src/*`
- `tests/formal/`
- `tests/ir/`

## [x] Issue 20

标题：
抽象 backend API 并拆分 `emit-ir` / `emit-ir-json` / `emit-smv`

背景：
随着 `emit-ir`、`emit-ir-json` 和 `emit-smv` 增长，CLI 和 emitter 代码会继续膨胀。需要把 backend driver、公共 lowering 边界和具体 backend 实现层拆开。

目标：

1. 抽象统一的 backend API 或 driver 层
2. 让 `ahflc` 只负责参数分发，不直接感知过多 backend 细节
3. 为未来新增 backend 保留稳定扩展点

验收标准：

1. backend 相关代码有清晰目录和接口边界
2. `ahflc` CLI 文件不再持续膨胀
3. 新增 backend 时不需要复制粘贴已有 emitter 流水线

依赖：

- Issue 16
- Issue 18
- Issue 19

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/*.hpp`
- `src/*.cpp`
- `CMakeLists.txt`
