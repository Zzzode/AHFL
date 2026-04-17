# AHFL V0.4 Roadmap

本文给出 AHFL 在 V0.3 主线完成之后的下一阶段实施路线。V0.4 的重点不再是继续把 Core 内部边界越做越深，而是把现有 Core 编译器收束成 future Native/runtime 可正式承接的 handoff 边界。

基线输入：

- [roadmap-v0.3.zh.md](./roadmap-v0.3.zh.md)（已完成基线）
- [issue-backlog-v0.3.zh.md](./issue-backlog-v0.3.zh.md)（已完成基线）
- [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md)
- [core-scope-v0.1.en.md](../design/core-scope-v0.1.en.md)
- [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)
- [native-package-metadata-v0.4.zh.md](../design/native-package-metadata-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [ir-compatibility-v0.3.zh.md](../reference/ir-compatibility-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](../reference/backend-capability-matrix-v0.3.zh.md)
- [README.md](../../README.md)

当前实现状态：

1. V0.3 已完整收口；仓库已具备 project-aware frontend/checker/IR/summary/SMV、manifest/workspace、V0.3 regression / CI 和 contributor 文档。
2. 当前稳定 IR 已能表达 provenance、flow summary、workflow value summary、formal observations，但这些信息仍主要面向 compiler backend / tooling consumer。
3. 仓库已新增独立 `handoff` 层与 `emit-native-json` backend，可导出 runtime-facing handoff package JSON，但仍未进入 Native/runtime 实现。
4. `ahfl.project.json` / `ahfl.workspace.json` 当前仍只描述编译输入，不描述 runtime-facing package / export / deployment 边界。
5. 当前 handoff emission 不会猜 package metadata 默认值；metadata 外部输入来源仍留在后续阶段冻结。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.4 的交付目标是：

1. 把现有 Core 输出收束成 future Native/runtime 可消费的 handoff package / execution descriptor。
2. 冻结 runtime-facing 的 package identity、execution graph、capability surface 与 policy surface 边界。
3. 保持 Core 与 Native/runtime 的职责拆分清晰，不把 runtime 实现细节提前混入 Core。
4. 为后续真正实现 AHFL Native 准备稳定的 consumer 契约，而不是直接开始写 launcher / scheduler。
5. 让 project-aware 路径、IR 契约、backend 扩展规则继续沿用到 handoff export。

## 非目标

V0.4 仍然不直接承诺：

1. runtime launcher、scheduler 或 host integration
2. `tool` 实现体、`llm_config`、外部 connector 执行模型
3. observability / compliance runtime
4. registry、publish、distribution pipeline
5. statement/data semantics 的完整执行解释器

## 里程碑

### M0 V0.4 Native Handoff 边界冻结

状态：

- [x] Issue 01 冻结 V0.4 的 Native handoff 范围与成功标准
- [x] Issue 02 冻结 Native handoff package / execution descriptor 架构
- [x] Issue 03 冻结 capability binding 与 runtime-facing metadata 的最小边界

目标：

1. 明确 V0.4 做的是“handoff contract”，不是“runtime implementation”。
2. 明确 handoff package 与当前 IR / project descriptor 的关系。
3. 明确哪些 runtime-facing 元信息进入正式边界，哪些仍保持外部实现细节。

关键输出：

1. 一致的 V0.4 scope 说明
2. 一致的 Native handoff 架构说明
3. 一致的 capability / package metadata 边界说明

退出标准：

1. V0.4 的目标不再与 V0.3 Core 深化或真正 runtime 实现混淆
2. handoff package、project descriptor、runtime metadata 使用同一组术语
3. 后续 issue 不再直接把 runtime 细节写进 Core backlog

### M1 Handoff Package Model 与导出入口

状态：

- [x] Issue 04 引入 handoff package / package metadata 数据模型
- [x] Issue 05 扩展 IR/backend，生成 runtime-facing handoff package
- [x] Issue 06 增加 project-aware handoff debug / emission 入口

目标：

1. 让 Native handoff 不再只是口头约定，而是正式的编译输出边界。
2. 让 handoff export 继续支持 `--search-root`、`--project`、`--workspace --project-name`。
3. 让 package identity、entry/export target 与 source provenance 形成稳定对象模型。

关键输出：

1. 稳定的 handoff package 数据模型
2. 新的 runtime-facing emission backend 或等价导出入口
3. project-aware handoff 输出与调试路径

退出标准：

1. handoff package 不要求 consumer 回看 AST 或 raw source
2. CLI 不直接承担 handoff object 拼装细节
3. 单文件、descriptor、workspace 三条入口都能导出同一 handoff family

### M2 Runtime-Facing 语义投影

状态：

- [x] Issue 07 导出 workflow execution graph 与节点生命周期摘要
- [x] Issue 08 导出 contract obligation、capability dependency 与受限值流摘要

目标：

1. 把 future Native 真正需要的执行边界从当前 IR 中投影出来。
2. 让 capability surface、workflow graph、policy surface 对 runtime-facing consumer 可直接消费。
3. 保持“摘要语义”和“完整执行语义”的边界清晰。

关键输出：

1. workflow graph / lifecycle 的 runtime-facing 结构
2. contract / observation / capability dependency 的 runtime-facing 结构
3. 面向 handoff package 的 golden 集

退出标准：

1. runtime-facing consumer 不需要重新猜工作流拓扑和关键约束
2. handoff package 明确保留哪些语义、继续抽象哪些语义
3. IR、handoff package、文档与 golden 一致

### M3 Consumer 契约与扩展路径

状态：

- [x] Issue 09 冻结 handoff package 的 compatibility / versioning 契约
- [x] Issue 10 验证 runtime-facing backend 扩展路径与 consumer matrix

目标：

1. 让 future Native/runtime consumer 清楚哪些字段可稳定依赖。
2. 让 handoff package 的格式演进不依赖隐含约定。
3. 用一条受控的 runtime-facing 导出路径验证当前 backend 架构可继续扩张。

关键输出：

1. handoff package compatibility / versioning 约束
2. runtime-facing capability / consumer matrix
3. 对应 regression 与扩展指引

退出标准：

1. downstream Native tooling 可以基于文档消费 handoff package
2. 新导出路径不需要绕开 driver / project-aware pipeline
3. 扩展规则可被文档和测试共同约束

### M4 工程化收口

状态：

- [x] Issue 11 建立 V0.4 handoff regression 与 CI 覆盖
- [x] Issue 12 补齐 V0.4 reference 与 contributor 文档

目标：

1. 让 handoff package、project-aware emission、compatibility 契约进入持续回归。
2. 让新贡献者能按文档跑通 V0.4 handoff 路径，并理解 Core / Native 的职责边界。

关键输出：

1. 更新后的 `ctest` / CI 覆盖
2. 更新后的 reference 文档与 contributor guidance

退出标准：

1. handoff 正例、负例、golden、compatibility regression 都进入 CI
2. 新贡献者可按文档跑通 V0.4 Native handoff 路径

## 关键依赖顺序

1. 先完成 M0，再进入 handoff package 数据模型实现
2. 先冻结 package / metadata 边界，再扩 backend / CLI 导出入口
3. 先冻结 compatibility / versioning，再对下游承诺长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 handoff package 架构未冻结前直接定义 runtime-specific JSON
2. 不要把 scheduler、connector、deployment 配置直接塞回 project/workspace descriptor
3. 不要让 Native consumer 重新解释 Core 的 module/type/resolve 规则
4. 不要把 V0.4 的实现目标偷换成 runtime launcher / host integration 开发

## 当前状态

V0.4 已完成 M0、M1、M2、M3 与 M4。当前 handoff package、runtime-facing export、compatibility、consumer matrix、regression 与 contributor/reference 文档已形成完整闭环。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.4.zh.md](./issue-backlog-v0.4.zh.md)
