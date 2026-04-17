# AHFL Core V0.2 Roadmap

本文给出 AHFL Core 从“单文件可用 checker + restricted formal backend”走向“project-aware checker + 更明确 formal contract/export 边界”的实施路线。

基线输入：

- [roadmap-v0.1.zh.md](./roadmap-v0.1.zh.md)（历史基线）
- [issue-backlog-v0.1.zh.md](./issue-backlog-v0.1.zh.md)（历史基线）
- [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md)
- [compiler-phase-boundaries-v0.2.zh.md](../design/compiler-phase-boundaries-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)
- [repository-layout-v0.2.zh.md](../design/repository-layout-v0.2.zh.md)
- [module-loading-v0.2.zh.md](../design/module-loading-v0.2.zh.md)
- [source-graph-v0.2.zh.md](../design/source-graph-v0.2.zh.md)
- [README.md](../../README.md)

当前实现状态：

1. Core V0.1 的 M0-M4 已完成，相关历史路线记录保留在 V0.1 roadmap/backlog；`ahflc` 已具备 parse、AST lowering、resolve、typecheck、validate、IR/JSON/SMV emission。
2. 当前公共编译入口仍以单个输入文件为中心，CLI 子命令和 frontend 入口都围绕单源文件工作。
3. `module` / `import` 语法、import alias 和 qualified name 已存在，但项目级 source graph、跨文件装载策略与 project-level 回归边界尚未冻结为正式设计。
4. `emit-smv` 已可用，并已对 `requires` / `ensures` 增加受限 contract export；但 statement/data semantics、workflow 值流与 richer scheduling semantics 仍不在当前边界内。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.2 的实际交付目标仍然留在 AHFL Core 轨道内，不直接跳向 runtime / Native，而是：

1. 冻结 project-aware compilation model
2. 建立跨文件 module/import 装载与检查边界
3. 让 resolver / typecheck / validate 能稳定工作在 source graph 上
4. 扩展稳定 IR / JSON / formal backend 的项目级边界
5. 补齐 project-level 回归测试、参考文档和示例

## 非目标

V0.2 仍然不直接承诺：

1. native tool / LLM 执行体
2. runtime launcher
3. observability / compliance authoring
4. saga compensation runtime
5. 完整 CTL / fairness / distributed transaction 语义

## 里程碑

### M0 V0.2 边界冻结

状态：

- [x] Issue 01 冻结 project model 与 compile unit 边界
- [x] Issue 02 冻结多文件 `module` / `import` 语义
- [x] Issue 03 冻结 V0.2 formal contract/export 边界

目标：

1. 明确 V0.2 处理的是“project-aware Core”，不是“AHFL Native”
2. 明确 source file、module、entry source、source graph 的关系
3. 明确 formal backend 下一阶段究竟保留哪些 contract 语义，抽象哪些语义

关键输出：

1. 一致的 V0.2 边界说明
2. 一致的多文件装载/导入规则说明
3. 一致的 formal contract/export 边界说明

退出标准：

1. V0.2 的目标不再与 V0.1 或后续 Native 路线混淆
2. 多文件装载规则在文档、CLI 预期和后续实现任务中一致
3. `requires` / `ensures` 与 embedded pure expr 的 formal export 边界有单独文档约束

### M1 Source Graph 与前端入口

状态：

- [x] Issue 04 引入 project/source graph 数据模型
- [x] Issue 05 扩展 frontend 与 CLI 入口，支持 project-aware 装载

目标：

1. 从“单文件 parse/check”升级到“可装载一组相关源码”
2. 固定 module 到 source file 的归属关系
3. 让 diagnostics 能表达 source graph 层面的错误

关键输出：

1. 稳定的 project/source graph 模型
2. project-aware frontend 入口
3. module/file 冲突、缺失入口、重复归属等错误的稳定 diagnostics

退出标准：

1. 编译器可以从 entry source 装载相关 source set
2. project model 不要求后续阶段直接读取文件系统细节
3. source graph 层面的错误不会混入普通 parser/checker diagnostics

### M2 跨文件 Resolver / Type Checker / Validator

状态：

- [x] Issue 06 实现跨文件符号注册与 import 解析
- [x] Issue 07 实现 project-aware diagnostics 与 source ownership 校验
- [x] Issue 08 扩展跨文件 typecheck / validate 到 workflow 与 schema 场景

目标：

1. 让 resolver 在多 source 的前提下注册和查询符号
2. 让 typecheck / validate 在跨文件引用时保持稳定边界
3. 让 workflow、schema alias、agent target 等跨文件场景进入正式回归

关键输出：

1. 跨文件 symbol table 与 resolved reference 模型
2. 带 file/source range 的 project-aware diagnostics
3. 多文件 golden tests

退出标准：

1. import target、qualified name、跨文件 agent/workflow/type 引用可稳定解析
2. 多文件错误具备可回归的 diagnostics
3. 单文件路径仍然保持可用，不被新模型破坏

### M3 IR / JSON / Formal Backend 深化

状态：

- [x] Issue 09 扩展 IR / JSON 到 project-aware ownership 与 declaration provenance
- [x] Issue 10 扩展 formal backend 到 contract clause export 的下一层

目标：

1. 让稳定 IR 具备 project-aware 声明来源边界
2. 让 formal consumer 能看到更明确的 contract/export 信息
3. 在不伪装“完整执行语义”的前提下推进 formal backend

关键输出：

1. project-aware `emit-ir` / `emit-ir-json`
2. 更新后的 formal contract/export 设计说明
3. 针对 contract / workflow / observation 的新 golden 集

退出标准：

1. 新增 backend 不需要重新定义 source ownership 规则
2. formal export 的新增能力已明确区分“保留语义”和“抽象语义”
3. 文档、实现、golden 三者一致

### M4 工程化收口

状态：

- [x] Issue 11 建立 V0.2 多文件与 backend golden 测试体系
- [x] Issue 12 补齐 project usage、CLI 与 IR reference 文档

目标：

1. 让 V0.2 的多文件能力有持续回归保护
2. 降低后续继续扩展 Core 或转向 Native 时的认知成本

关键输出：

1. 更完整的 `ctest` / CI 覆盖
2. `docs/reference/` 下的 project usage / CLI / IR 参考文档

退出标准：

1. 多文件正例、负例、IR、formal 输出都进入 CI
2. 新贡献者可以按 reference 文档复现典型 project-aware 用法

## 关键依赖顺序

1. 先完成 M0，再进入 M1
2. 先冻结 source graph 边界，再做跨文件 resolver/checker
3. M2 稳定后，再推进 M3 的 project-aware IR / formal export
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在没有 project model 的前提下直接把更多跨文件逻辑堆进 resolver
2. 不要在多文件 diagnostics 未稳定前扩张 CLI 表面能力
3. 不要在 formal contract/export 边界未冻结前继续往 `emit-smv` 塞隐式语义
4. 不要把 V0.2 的实现目标偷换成 runtime / Native 路线

## 当前状态

V0.2 规划项已全部完成。当前仓库已经收口到 project-aware frontend/checker/IR/SMV、多文件 golden，以及 project/CLI/IR reference 文档的完整边界。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.2.zh.md](./issue-backlog-v0.2.zh.md)
