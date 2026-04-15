# AHFL Core V0.1 Roadmap

本文给出 AHFL Core V0.1 从“已有规范和 parser bootstrap”走向“可用 checker”的实施路线。

基线输入：

- [ahfl-core-v0.1.md](/Users/bytedance/Develop/AHFL/docs/ahfl-core-v0.1.md)
- [ahfl-spec-v0.1-zh.md](/Users/bytedance/Develop/AHFL/docs/ahfl-spec-v0.1-zh.md)
- [AHFL.g4](/Users/bytedance/Develop/AHFL/grammar/AHFL.g4)
- [frontend.cpp](/Users/bytedance/Develop/AHFL/src/frontend.cpp)

当前实现状态：

1. 已有可编译的 C++23 前端骨架
2. 已接入 ANTLR4 C++ parser
3. `ahflc` 能完成 parse 和顶层 declaration outline 输出
4. 尚未完成 AST 完整建模、名称解析、类型检查、静态语义校验、IR lowering

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
- [ ] Issue 03 冻结前端阶段职责与中间表示边界

目标：

1. 统一设计说明、规范、ANTLR 文法、示例之间的边界定义
2. 明确 Core V0.1 与后续 AHFL Native 的分界
3. 冻结编译器前端阶段职责

关键输出：

1. 一致的语言边界说明
2. 一致的类型系统边界说明
3. 一致的文法/规范/示例基线

退出标准：

1. `docs/ahfl-core-v0.1.md` 与 `docs/ahfl-spec-v0.1-zh.md` 不再冲突
2. `grammar/AHFL.g4` 与规范一致
3. 示例文件完全符合规范

### M1 AST 与前端建模

状态：

- [ ] Issue 04 扩展顶层 AST，使 declaration 节点承载完整语义信息
- [ ] Issue 05 为 AHFL 增加完整类型 AST
- [ ] Issue 06 为表达式、语句与时序公式增加分层 AST
- [ ] Issue 07 完成 ANTLR parse tree 到 AST 的 lowering
- [ ] Issue 08 升级 `ahflc --dump-ast` 输出

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

- [ ] Issue 09 实现符号表与名称解析
- [ ] Issue 10 实现类型解析与类型等价规则
- [ ] Issue 11 实现表达式与语句类型检查
- [ ] Issue 12 实现 contract checker
- [ ] Issue 13 实现 agent 与 flow validator
- [ ] Issue 14 实现 workflow validator

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

- [ ] Issue 15 建立测试体系、扩展 CI 并增加 IR 输出

目标：

1. 建立 parser/checker 回归测试
2. 将 CI 从 smoke test 扩展到语义测试
3. 增加稳定 IR 输出

关键输出：

1. `ctest` 测试集
2. parser/checker golden diagnostics
3. `ahflc emit-ir`

退出标准：

1. 语法和语义修改有自动回归保护
2. IR 能稳定描述 agent / contract / workflow 核心结构

## 关键依赖顺序

1. 先完成 M0，再进入 M1
2. 先完成 M1，再写 M2 checker
3. M2 成型后，再做 M3 的测试与 IR

不建议跳步：

1. 不要在当前极薄 AST 上直接堆 checker
2. 不要在 checker 未稳定前扩张到 `tool` 实现体、`llm_config`、runtime launcher
3. 不要在没有回归测试前推进复杂 workflow/saga 扩展

## 最近三项优先任务

1. 冻结前端阶段职责与中间表示边界
2. 扩展 AST，覆盖类型、表达式、语句与时序公式
3. 完成 parse tree 到 AST lowering

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.1.md](/Users/bytedance/Develop/AHFL/docs/issue-backlog-v0.1.md)
