# AHFL Core V0.3 Roadmap

本文给出 AHFL Core 在 V0.2 完成之后的下一阶段实施路线。V0.3 不把目标偷换成 runtime / Native 落地，而是在现有 project-aware Core 边界上继续推进语义深化、项目工程能力和 backend 扩展契约。

基线输入：

- [roadmap-v0.2.zh.md](./roadmap-v0.2.zh.md)（已完成基线）
- [issue-backlog-v0.2.zh.md](./issue-backlog-v0.2.zh.md)（已完成基线）
- [compiler-architecture-v0.2.zh.md](../design/compiler-architecture-v0.2.zh.md)
- [compiler-evolution-v0.2.zh.md](../design/compiler-evolution-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)
- [module-loading-v0.2.zh.md](../design/module-loading-v0.2.zh.md)
- [module-resolution-rules-v0.2.zh.md](../design/module-resolution-rules-v0.2.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [testing-strategy-v0.3.zh.md](../design/testing-strategy-v0.3.zh.md)
- [README.md](../../README.md)

当前实现状态：

1. V0.2 规划项已全部完成；仓库已具备 project-aware frontend/checker/IR/SMV、多文件 regression，以及 project/CLI/IR reference 文档。
2. `ahflc` 已支持 `check`、`dump-ast`、`dump-types`、`dump-project`、`emit-ir`、`emit-ir-json`、`emit-summary`、`emit-smv` 的 project-aware 用法。
3. formal backend 当前仍是受限导出边界：statement/data semantics、workflow 值流、capability timing/value 和更强时序语义仍保持抽象。
4. manifest/workspace 最小 project model 已进入正式边界，并已接入 frontend/CLI/project-aware regression；增量编译与更高层工程能力仍未进入边界。
5. IR 兼容性 / 版本化契约、workflow value summary，以及 `emit-summary` 参考 backend 已进入正式文档与 regression 边界；当前剩余主线任务集中在测试/文档收口。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.3 的交付目标是：

1. 继续留在 AHFL Core 轨道，不直接实现 Native/runtime 执行体。
2. 把 project-aware 编译从“可用”推进到“可维护、可调试、可扩展”。
3. 把 formal / IR 边界从当前 observation-heavy 子集推进到下一层明确受限语义。
4. 冻结下游 backend / tooling consumer 的兼容与扩展契约。
5. 为后续是否进入 AHFL Native 准备清晰的 handoff boundary，而不是提前把 runtime 细节混入 Core。

## 非目标

V0.3 仍然不直接承诺：

1. native tool / LLM body 执行
2. runtime launcher、scheduler 或 host integration
3. 完整 statement-by-statement 执行语义
4. 远程 registry / package manager
5. build daemon 或完整增量重编译系统

## 里程碑

### M0 V0.3 边界与决策门冻结

状态：

- [x] Issue 01 冻结 V0.3 的 Core 深化目标与 Native handoff 边界
- [x] Issue 02 冻结 manifest / workspace 的最小 project model
- [x] Issue 03 冻结 V0.3 formal semantic/export 的下一层边界

目标：

1. 明确 V0.3 解决什么，不解决什么。
2. 明确 project-aware 下一阶段是否引入 manifest/workspace。
3. 明确 formal backend 下一步保留哪些语义，继续抽象哪些语义。

关键输出：

1. 一致的 V0.3 scope 说明
2. 一致的 project descriptor 设计说明
3. 一致的 formal semantic/export 设计说明

退出标准：

1. V0.3 的目标不再停留在口头方向
2. project model、CLI 入口和后续 issue 使用同一组术语
3. formal 深化边界在文档、实现任务和测试策略中一致

### M1 Project Model 与调试面

状态：

- [x] Issue 04 引入 manifest / workspace 数据模型
- [x] Issue 05 扩展 frontend 与 CLI 入口消费 manifest / workspace
- [x] Issue 06 增加 project-aware AST / debug 输出

目标：

1. 降低 `--search-root` 驱动 project-aware 用法的认知成本。
2. 为项目级入口、search root 和 entry source 建立更明确的声明式描述。
3. 让 project-aware 调试不再只能依赖 `dump-project` 与后续 backend 输出。

关键输出：

1. 稳定的 manifest / workspace 模型
2. project-aware CLI 新入口或等价声明式入口
3. `dump-ast` 或等价 debug 输出的项目级能力

退出标准：

1. 典型 project-aware 用法不再必须手工拼 search root
2. loader 规则没有因为项目入口增强而变得隐式
3. project-aware 调试能力可进入 regression

### M2 语义与 IR 深化

状态：

- [x] Issue 07 扩展 flow statement 语义边界到下一层稳定 IR
- [x] Issue 08 扩展 workflow 数据流 / 返回值相关边界

目标：

1. 让当前 formal / IR 边界在不伪装完整执行语义的前提下前进一步。
2. 让 flow / workflow 中目前只被检查、不被稳定表达的一部分信息进入正式后端边界。
3. 让“保留语义”和“抽象语义”的拆分继续保持可解释。

关键输出：

1. 更新后的 IR / JSON 结构
2. 更新后的 formal semantic/export 设计文档
3. 面向 flow / workflow 新边界的 golden 集

退出标准：

1. 新增能力不要求 backend 重新回读 AST 或 raw text
2. 新增语义边界已文档化为保留或抽象
3. diagnostics、IR、backend 与 golden 四者一致

### M3 Consumer 契约与 Backend 扩展

状态：

- [x] Issue 09 冻结 IR 兼容性与版本化契约
- [x] Issue 10 验证 backend 扩展路径与能力矩阵

目标：

1. 让下游 consumer 清楚哪些 IR / JSON 字段可稳定依赖。
2. 让新增 backend 不需要重新定义 source ownership、observation 命名或 declaration provenance。
3. 用一条受控的 backend 扩展路径验证当前架构不是只对 `emit-smv` 成立。

关键输出：

1. IR compatibility / versioning 约束
2. backend extension 的参考实现或参考骨架
3. backend capability matrix 与对应 regression

退出标准：

1. downstream tool 可以基于文档消费 IR / JSON
2. 新 backend 的边界不再依赖隐含约定
3. backend 扩展规则可被文档和测试共同约束

### M4 工程化收口

状态：

- [x] Issue 11 建立 V0.3 新能力的回归与 CI 覆盖
- [x] Issue 12 补齐 V0.3 的 reference 与贡献者文档

目标：

1. 让 manifest/project/debug/formal 新能力进入持续回归。
2. 让新贡献者能按文档复现 V0.3 典型用法与扩展路径。

关键输出：

1. 更新后的 `ctest` / CI 覆盖
2. 更新后的 reference 文档与 contributor guidance

退出标准：

1. V0.3 新增正例、负例、IR、backend 输出都进入 CI
2. 新贡献者可按文档跑通 project-aware V0.3 路径

## 关键依赖顺序

1. 先完成 M0，再进入 manifest/workspace 或 formal 深化实现
2. 先冻结 project descriptor，再改 frontend / CLI 入口
3. 先冻结 IR / formal 新边界，再扩 backend 能力
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在没有 manifest/workspace 设计的前提下直接往 CLI 继续堆 project 参数
2. 不要在 formal 下一层语义未文档化前直接向 `emit-smv` 或新 backend 塞隐式 lowering
3. 不要在 IR 兼容性未冻结前向下游承诺长期稳定字段
4. 不要把 V0.3 的实现目标偷换成 runtime / Native 开发

## 当前状态

V0.3 已完成 M0、M1、M2、M3 和 M4。当前 roadmap 主线已收口，后续更适合转入下一版本规划或新增专项设计议题。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.3.zh.md](./issue-backlog-v0.3.zh.md)
