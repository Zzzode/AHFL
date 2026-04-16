# AHFL Core V0.1 Roadmap

本文给出 AHFL Core V0.1 从“已有规范和 parser bootstrap”走向“可用 checker”的实施路线。

基线输入：

- [core-scope-v0.1.en.md](../design/core-scope-v0.1.en.md)
- [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md)
- [frontend-architecture-v0.1.zh.md](../design/frontend-architecture-v0.1.zh.md)
- [AHFL.g4](../../grammar/AHFL.g4)
- [frontend.cpp](../../src/frontend/frontend.cpp)

当前实现状态：

1. 已有可编译的 C++23 前端骨架
2. 已接入 ANTLR4 C++ parser
3. `ahflc` 能完成 parse、完整 hand-written AST lowering、resolver、类型检查、独立 validate 阶段，以及 AST/type/IR dump
4. V0.1 前端主链路已经闭环，声明/表达式/时序公式/语句 IR 已结构化，后续主要是衔接后端 lowering 与继续补 golden
5. 已经打开第一版 restricted formal backend 方向：`emit-smv`

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.1 的实际交付目标不是运行时，不是 native tool/LLM 执行，也不是 observability/compliance authoring，而是：

1. 稳定的 Core 语言边界
2. 完整 parser -> AST -> checker 前端链路
3. 可验证的静态语义约束
4. 可回归的测试集
5. 可继续衔接后端的 IR 输出

## 里程碑

### M0 规范冻结

状态：

- [x] Issue 01 冻结 AHFL Core V0.1 语言边界
- [x] Issue 02 冻结 AHFL Core V0.1 类型系统边界
- [x] Issue 03 冻结前端阶段职责与中间表示边界

目标：

1. 统一设计说明、规范、ANTLR 文法、示例之间的边界定义
2. 明确 Core V0.1 与后续 AHFL Native 的分界
3. 冻结编译器前端阶段职责

关键输出：

1. 一致的语言边界说明
2. 一致的类型系统边界说明
3. 一致的前端阶段边界说明

退出标准：

1. `docs/design/core-scope-v0.1.en.md` 与 `docs/spec/core-language-v0.1.zh.md` 不再冲突
2. `grammar/AHFL.g4` 与规范一致
3. `docs/design/frontend-architecture-v0.1.zh.md` 冻结 `parse -> ast -> resolve -> typecheck -> validate -> emit` 阶段边界

### M1 AST 与前端建模

状态：

- [x] Issue 04 扩展顶层 AST，使 declaration 节点承载完整语义信息
- [x] Issue 05 为 AHFL 增加完整类型 AST
- [x] Issue 06 为表达式、语句与时序公式增加分层 AST
- [x] Issue 07 完成 ANTLR parse tree 到 AST 的 lowering
- [x] Issue 08 升级 `ahflc --dump-ast` 输出

目标：

1. 从当前“顶层 envelope AST”升级为完整 AST
2. 将类型、表达式、时序公式、语句、workflow 节点显式建模
3. 完成 parse tree 到 AST lowering

关键输出：

1. 完整 AST 数据结构
2. 完整的 lowering 实现
3. 更可读的 `--dump-ast`

退出标准：

1. 示例文件能 lower 成完整 AST
2. checker 不依赖 `raw_text` 或 ANTLR parse tree 直接做语义工作

### M2 Resolver / Type Checker / Validator

状态：

- [x] Issue 09 实现符号表与名称解析
- [x] Issue 10 实现类型解析与类型等价规则
- [x] Issue 11 实现表达式与语句类型检查
- [x] Issue 12 实现 contract checker
- [x] Issue 13 实现 agent 与 flow validator
- [x] Issue 14 实现 workflow validator

目标：

1. 实现符号表与名称解析
2. 实现类型解析、表达式检查、时序公式检查
3. 实现 agent / contract / flow / workflow 验证

关键输出：

1. `ahflc check <file>`
2. 带 source range 的稳定 diagnostics
3. 规范第 5 节静态检查清单的可执行实现

退出标准：

1. 声明级检查可用
2. agent / contract / flow / workflow 检查可用
3. 示例正例通过，负例稳定失败

### M3 测试、CI 与 IR

状态：

- [x] Issue 15 建立测试体系、扩展 CI 并增加 IR 输出

目标：

1. 建立 parser/checker 回归测试
2. 将 CI 从 smoke test 扩展到语义测试
3. 增加稳定 IR 输出

关键输出：

1. `ctest` 测试集
2. parser/checker golden diagnostics
3. `ahflc emit-ir` / `ahflc emit-ir-json`

退出标准：

1. 语法和语义修改有自动回归保护
2. IR 能稳定描述 agent / contract / workflow 核心结构

### M4 Formal Backend

状态：

- [x] Issue 16 提交并冻结第一版 `emit-smv`
- [x] Issue 17 冻结 AHFL Core V0.1 的 formal subset 语义
- [x] Issue 18 扩展 `emit-smv` 到 flow / workflow 语义级 lowering
- [x] Issue 19 建立 formal observation abstraction 层
- [x] Issue 20 抽象 backend API 并拆分 `emit-ir` / `emit-ir-json` / `emit-smv`

目标：

1. 把当前 `emit-smv` 原型收口成受限但正式的 formal backend
2. 冻结 `called`、`in_state`、`running`、`completed` 与 embedded pure expr 的映射边界
3. 从声明级 exporter 继续推进到 flow / workflow 语义级 lowering
4. 为后续 formal backend 保留稳定扩展点

关键输出：

1. `ahflc emit-smv`
2. `docs/design/formal-backend-v0.1.zh.md`
3. `tests/formal/*` golden 集
4. 更清晰的 backend driver / emitter 边界

退出标准：

1. `emit-smv` 已有回归测试并明确能力边界
2. formal subset 语义不再只体现在实现里
3. observation abstraction 规则稳定
4. 新增 backend 不需要继续膨胀 `ahflc` CLI 入口文件

## 关键依赖顺序

1. 先完成 M0，再进入 M1
2. 先完成 M1，再写 M2 checker
3. M2 成型后，再做 M3 的测试与 IR
4. M3 收口后，再做 M4 的 formal backend 冻结与扩张

不建议跳步：

1. 不要在当前极薄 AST 上直接堆 checker
2. 不要在 checker 未稳定前扩张到 `tool` 实现体、`llm_config`、runtime launcher
3. 不要在没有回归测试前推进复杂 workflow/saga 扩展
4. 不要在 formal subset 未冻结前直接往 `emit-smv` 塞更多隐式语义

## 当前状态

M4 当前规划项已经全部完成，后续工作不再属于本轮 roadmap 里的 issue-ready 范围。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.1.zh.md](./issue-backlog-v0.1.zh.md)
