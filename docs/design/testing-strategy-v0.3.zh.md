# AHFL Core V0.3 Testing Strategy

本文说明 AHFL Core V0.3 当前测试资产的分层方式、golden 策略、V0.3 回归切片，以及新增能力时的补测路径，面向需要扩展编译器实现或维护回归套件的工程实现者。

关联文档：

- [compiler-evolution-v0.2.zh.md](./compiler-evolution-v0.2.zh.md)
- [compiler-architecture-v0.2.zh.md](./compiler-architecture-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./semantics-architecture-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](./project-descriptor-architecture-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](../reference/backend-capability-matrix-v0.3.zh.md)

## 目标

本文主要回答四个问题：

1. 当前仓库中的测试按什么层次组织。
2. 哪些能力适合用 `check` 失败用例，哪些更适合用 golden 输出。
3. project-aware 模式为什么需要单独的测试可执行和测试目录。
4. 新增一个语言能力时，最低应补哪几类测试。

## 总体原则

AHFL 当前测试策略遵循三个原则：

1. 尽量在最接近边界的层验证能力。
2. 对稳定输出边界使用 golden，而不是模糊断言。
3. 对 project-aware 能力单独建图测试，而不是只靠单文件回归“顺便覆盖”。

当前测试并不追求“统一一种风格”，而是按边界分成三类：

1. API / 对象形状测试
   - 手写 C++ test executable。
2. CLI 成功/失败测试
   - 直接运行 `ahflc`。
3. 稳定输出 golden
   - 比较 `emit-ir` / `emit-ir-json` / `emit-smv` 的完整输出。

## 测试资产地图

当前主要测试目录为：

- `tests/project/`
- `tests/resolver/`
- `tests/typecheck/`
- `tests/ir/`
- `tests/summary/`
- `tests/formal/`

以及测试注册文件：

- `tests/CMakeLists.txt`
- `tests/cmake/TestTargets.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/SingleFileCliTests.cmake`
- `tests/cmake/LabelTests.cmake`
- `cmake/modules/AhflTesting.cmake`
- `cmake/RunExpectedOutput.cmake`
- `cmake/RunExpectedFailure.cmake`

这套布局表达的是“按编译器边界分组”，不是按 issue 或开发阶段复制目录。

V0.3 额外引入的不是新目录层，而是显式的 CTest label 切片。这样可以在不破坏既有目录布局的前提下，把 manifest/workspace、project-aware debug、IR 新边界和 backend 扩展路径收束成一组可单独执行的回归面。

## 三类测试机制

### 1. 手写 C++ 可执行测试

当前典型文件：

- `tests/project/project_parse.cpp`
- `tests/project/project_resolve.cpp`
- `tests/project/project_check.cpp`

这类测试适合验证：

- `SourceGraph` 的 shape
- `module_to_source` / `import_edges` 等对象层结果
- 某个阶段返回的 diagnostics 是否包含指定消息
- project-aware 行为是否需要精确检查多个来源文件

之所以单独写成 C++ 可执行，而不是只跑 CLI，是因为它们要检查：

1. 返回对象结构
2. diagnostics entry 集合
3. 某些比纯文本输出更稳定的内部边界

### 2. CLI 成功/失败测试

由 `cmake/modules/AhflTesting.cmake` 中的 helper 注册：

- `ahfl_add_check_test(...)`
- `ahfl_add_check_fail_test(...)`

这类测试适合验证：

- `check` 子命令能否成功
- 失败时是否出现关键诊断文本
- 某个语言规则是否被前端/语义阶段拦截

常见样式：

1. `resolver/*.ahfl`
   - 名称解析错误和边界条件
2. `typecheck/*.ahfl`
   - 类型规则、validate 规则和结构错误

这里的 `typecheck/` 目录名并不意味着“所有错误都来自 typecheck.cpp”；它更像“单文件语义失败回归区”，其中也包含 validate 阶段暴露的错误，例如：

- unreachable state
- missing handler
- workflow dependency cycle

## 3. 输出 golden 测试

由 `ahfl_add_output_test(...)` 和 `RunExpectedOutput.cmake` 驱动。

这类测试适合冻结：

- `emit-ir`
- `emit-ir-json`
- `emit-summary`
- `emit-smv`

它们的设计目标不是“测试某一条消息”，而是冻结整个后端边界。

当前 golden 文件主要位于：

- `tests/ir/*.ir`
- `tests/ir/*.json`
- `tests/summary/*.summary`
- `tests/formal/*.smv`

golden 比较采用完整文本相等，而不是局部 regex。这表示：

1. IR / JSON IR / SMV 都被视为稳定输出契约。
2. 变更这些输出时，应该先确认是设计边界变化，而不是偶然重排。

## project-aware 测试为何单独建模

project-aware 能力当前不只是“给单文件命令加个 `--search-root`”，它引入了新的对象边界：

- `ProjectInput`
- `ProjectParseResult`
- `SourceGraph`
- `ImportEdge`
- per-source diagnostics 归属

因此，当前测试把它单独组织在：

- `tests/project/<case>/...`
- `tests/project/project_*.cpp`

以及若干 CLI project 模式测试。

这样做的原因是：

1. graph shape 不能通过单文件 `check` 失败信息充分表达。
2. 导入缺失、module owner 冲突、跨文件引用等行为都依赖多文件布局。
3. project-aware regression 往往同时牵涉 frontend、resolver、typecheck、IR 和 backend。

## V0.3 回归切片与标签

V0.3 当前通过 `tests/cmake/*.cmake` 这组注册文件为关键新增能力追加显式标签与回归项：

- `ahfl-v0.3`
  - 所有 V0.3 收口回归的总标签。
- `v0.3-project-model`
  - manifest / workspace 的 descriptor 驱动入口、正例与负例。
- `v0.3-project-debug`
  - project-aware `dump-project` 与 `dump-ast` 调试面。
- `v0.3-semantics`
  - project-aware `check` 在 search-root / manifest / workspace 路径上的语义回归。
- `v0.3-ir`
  - flow summary 与 workflow value summary 的 textual/JSON IR 回归。
- `v0.3-backend`
  - `emit-summary` 参考 backend 和 `emit-smv` 的受限消费边界。
- `v0.3-compat`
  - V0.2 `--search-root` 兼容入口在 V0.3 中仍需保持可见的 regression。

这组标签的目标不是替代全量测试，而是让维护者可以明确回答三个问题：

1. V0.3 新边界是否有单独的回归入口。
2. 某次失败属于 project model / debug / semantics / IR / backend 的哪一层。
3. 兼容入口是否仍与 descriptor 驱动路径一起保持可见。

本地可直接执行：

```bash
ctest --preset test-dev -L ahfl-v0.3
ctest --preset test-dev -L 'v0.3-(project-model|project-debug|semantics|compat)'
ctest --preset test-dev -L 'v0.3-(ir|backend)'
```

CI 也显式执行这些 V0.3 切片，然后再继续跑全量 `ctest --preset test-dev --output-on-failure`。这样既保留了全量回归，又让 V0.3 主线能力在日志中具备单独的失败面。

## 当前测试层次与边界的对应关系

### Frontend / SourceGraph

主要覆盖：

- `parse_project(...)`
- `load_project_descriptor(...)`
- `load_project_descriptor_from_workspace(...)`
- import path 解析
- module owner 唯一性
- descriptor root 越界
- workspace 选择歧义
- source graph shape

适合的测试形态：

- `tests/project/project_parse.cpp`
- `ahflc dump-project`

### Resolver / 单文件语义

主要覆盖：

- duplicate top-level names
- unknown references
- type alias cycle
- import alias 相关行为

适合的测试形态：

- `resolver/*.ahfl`
- `ahfl_add_check_fail_test(...)`

### TypeCheck / Validate

主要覆盖：

- schema 约束
- 表达式类型
- flow / workflow 结构规则
- temporal 使用语境

适合的测试形态：

- `typecheck/*.ahfl`
- `ahfl_add_check_fail_test(...)`
- 必要时 `dump-types`

### IR / JSON IR

主要覆盖：

- declaration lowering
- provenance 输出
- `formal_observations`
- workflow / contract / flow 的结构化表示

适合的测试形态：

- `tests/ir/*.ir`
- `tests/ir/*.json`

### Formal Backend

主要覆盖：

- observation lowering
- state machine / workflow lifecycle 映射
- SMV 文本边界

适合的测试形态：

- `tests/formal/*.smv`

### Native Handoff Package

主要覆盖：

- handoff object model
- runtime-facing emission
- compatibility / versioning 守卫

适合的测试形态：

- `tests/handoff/*.cpp`
- `tests/native/*.native.json`
- `ahfl_label_tests(... LABELS ahfl-v0.4 v0.4-package-*)`

## 新能力的最小补测路径

### 1. 新增语法但不改变语义边界

至少补：

- 一个 `check` 成功用例
- 若会改变 AST 后续可见结构，再补一个 IR golden

### 2. 新增名称解析规则

至少补：

- 一个 resolver 成功/失败用例
- 若 project-aware 可见，再补一个 `tests/project/` case

### 3. 新增类型规则或结构规则

至少补：

- 一个 `check` 成功用例
- 一个 `check` 失败用例

若结果会改变 `dump-types`、IR 或 diagnostics 归属，再补对应 golden 或 project test。

### 4. 新增 IR 节点或 provenance 字段

至少补：

- 一个 `.ir` golden
- 一个 `.json` golden

不要只补 `emit-ir`，因为 JSON IR 是另一条稳定边界。

### 5. 新增 formal backend 能力

至少补：

- 一个 `.smv` golden
- 若 observation 集合变化，也应同步补 `.json` golden

### 6. 新增 Native handoff / runtime-facing 边界

至少补：

- 一个 `tests/handoff/*.cpp` 对象模型或 compatibility 测试
- 一个 `tests/native/*.native.json` golden
- 若存在 project-aware 差异，再补 `--project` / `--workspace` 路径 golden
- 同步补 `v0.4-package-model`、`v0.4-package-emission` 或 `v0.4-package-compat` 标签

## 选择测试形态的判断标准

可以用下面的规则快速判断：

1. 只关心成功/失败和一条关键错误消息
   - 用 `ahfl_add_check_test` 或 `ahfl_add_check_fail_test`
2. 关心完整稳定输出
   - 用 golden
3. 关心对象图 shape、source graph 或 diagnostics entry 集合
   - 用手写 C++ test executable

不要把所有问题都压成 CLI regex 测试，否则很快会出现：

1. 覆盖面虚高
2. 失败定位模糊
3. 无法检查 graph-level 边界

## golden 的使用约束

当前 golden 测试隐含了两个约束：

1. 输出顺序应稳定。
2. 文本格式变更应被视为显式边界变更。

因此修改 golden 前，应先问：

1. 这是设计变化还是重构副作用。
2. 下游是否依赖当前输出字段或顺序。
3. 是否还需要同步更新相关设计文档。

特别是：

- `tests/ir/project_check_ok.ir`
- `tests/ir/project_check_ok.json`

Native handoff 当前额外要求：

1. package model、package emission、package compatibility 三层测试应尽量分标签
2. CI 应显式跑 `v0.4-package-model`、`v0.4-package-emission`、`v0.4-package-compat`
3. 失败定位应能区分对象模型问题、发射问题和 compatibility 契约问题
- `tests/summary/project_workflow_value_flow.summary`
- `tests/formal/project_check_ok.smv`

这类 project-aware golden 同时冻结了多文件语义和 provenance 边界，不应轻率更新。

## 当前测试策略的限制

当前仓库测试仍有一些明确限制：

1. 目录名 `typecheck/` 兼容承载了 validate 失败用例，语义上略宽。
2. parser / AST lowering 的单文件粒度 golden 还不多。
3. diagnostics 的完整文本排序和多错误聚合规则还没有单独成套冻结。
4. 某些复杂 backend 语义仍主要依赖少量 representative golden，而不是按维度系统枚举。

因此后续继续补测试时，优先方向通常是：

1. 为新增边界补 representative golden
2. 为 project-aware 行为补专门 case
3. 为 diagnostics 归属补更细的断言

## 推荐阅读顺序

建议按下面顺序读测试体系：

1. `tests/cmake/ProjectTests.cmake`
2. `cmake/modules/AhflTesting.cmake`
3. `cmake/RunExpectedOutput.cmake`
4. `cmake/RunExpectedFailure.cmake`
5. `tests/project/project_parse.cpp`
6. `tests/ir/*.ir` / `tests/ir/*.json`
7. `tests/summary/*.summary`
8. `tests/formal/*.smv`

## 对后续实现的约束

后续继续扩展测试体系时，应保持以下原则：

1. 在最接近边界的层补测试，不要一律堆到端到端。
2. 任何稳定输出边界变化，都应通过 golden 显式呈现。
3. project-aware 功能应优先补多文件 case，而不是只补单文件近似用例。
4. diagnostics 相关改动，不仅要看报错内容，还要看报错是否附着到正确 source。
