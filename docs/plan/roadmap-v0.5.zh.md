# AHFL V0.5 Roadmap

本文给出 AHFL 在 V0.4 Native handoff 边界完成之后的下一阶段实施路线。V0.5 的重点不是直接开始写完整 runtime，而是把当前 handoff contract 继续推进为“可 author、可校验、可被 runtime-adjacent consumer 直接承接”的 package authoring 与 consumer bootstrap 边界。

基线输入：

- [roadmap-v0.4.zh.md](./roadmap-v0.4.zh.md)（已完成基线）
- [issue-backlog-v0.4.zh.md](./issue-backlog-v0.4.zh.md)（已完成基线）
- [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)
- [native-package-metadata-v0.4.zh.md](../design/native-package-metadata-v0.4.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](../reference/native-consumer-matrix-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [README.md](../../README.md)

当前实现状态：

1. V0.4 已完整收口；仓库已具备稳定的 handoff package 数据模型、`emit-native-json` backend、compatibility 契约、consumer matrix、回归与文档闭环。
2. 当前 handoff package 已能稳定导出 package-level capability / policy / execution graph / lifecycle / workflow value summary，但 package metadata 的外部输入来源仍未冻结。
3. `ahfl.project.json` / `ahfl.workspace.json` 当前仍主要描述编译输入，不负责明确 package identity、entry/export target 或 binding alias 的 authoring 入口。
4. 当前 runtime-facing consumer 虽可直接消费 `emit-native-json`，但仓库内尚未提供“面向 authoring / review / planner bootstrap”的显式调试面或参考消费路径。
5. 当前阶段仍未进入 launcher、scheduler、connector、deployment 或完整 statement/data execution semantics 的 runtime 实现。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.5 的交付目标是：

1. 冻结并落地 AHFL package metadata 的正式输入路径，而不是继续依赖 CLI 外部约定。
2. 让 package identity、entry/export targets、capability binding alias / slot authoring 进入统一、可校验的 compiler 输入边界。
3. 增加面向 package review、consumer bootstrap 与 planner 对接的调试/参考消费路径。
4. 保持 handoff package 与 runtime-adjacent consumer 的职责清晰，不把 deployment / scheduler 细节提前混入 Core。
5. 让 compatibility、consumer matrix、regression 与 contributor 文档继续覆盖这条新 authoring 路径。

## 非目标

V0.5 仍然不直接承诺：

1. runtime launcher、scheduler 或 host integration
2. capability connector、provider、`llm_config` 或其他执行器实现
3. deployment、tenant、secret、region、publish / registry / distribution 配置
4. statement/data semantics 的完整执行解释器
5. 多 package build graph、增量构建 daemon 或完整工程系统

## 里程碑

### M0 V0.5 Scope 与 Package Authoring 边界冻结

状态：

- [x] Issue 01 冻结 V0.5 的 package authoring / consumer bootstrap scope 与成功标准
- [x] Issue 02 冻结 package metadata 的 authoring model 与输入归属
- [x] Issue 03 冻结 Package Reader / Execution Planner 参考 consumer 的最小边界

目标：

1. 明确 V0.5 做的是“package authoring + consumer bootstrap”，不是“runtime implementation”。
2. 明确 package metadata 应从哪一层进入编译器，并继续与 project input / deployment 配置解耦。
3. 明确仓库内要提供哪种 runtime-adjacent 参考消费能力，哪些仍留到后续 Native/runtime。

关键输出：

1. 一致的 V0.5 scope 说明
2. 一致的 package metadata authoring 设计说明
3. 一致的 reference consumer / planner bootstrap 边界说明

退出标准：

1. V0.5 的目标不再与 runtime launcher / scheduler 开发混淆
2. project descriptor、package metadata、deployment 配置使用同一组术语
3. 后续 issue 不再把 runtime 私有细节直接写进 compiler backlog

### M1 Package Metadata Model 与编译输入接入

状态：

- [x] Issue 04 引入 package metadata descriptor / authoring 数据模型
- [x] Issue 05 扩展 frontend 与 CLI 消费 package metadata 输入
- [x] Issue 06 增加 package identity / export target / capability binding authoring 校验

目标：

1. 让 package metadata 不再停留在 handoff lowering 之后的空对象，而是有正式 authoring 入口。
2. 让单文件、`--project`、`--workspace --project-name` 三条路径都能进入同一 package metadata family。
3. 让 package identity、entry/export target 与 capability binding alias 的错误进入 frontend / diagnostics / validation 正式边界。

关键输出：

1. 稳定的 package metadata authoring 对象模型
2. 接入 project-aware pipeline 的 package metadata 输入路径
3. 对 authoring 输入的 diagnostics / validation 回归

退出标准：

1. CLI 不再需要依赖外部约定猜 package metadata 来源
2. handoff lowering 不再承担“缺省 authoring 语义”的隐式拼装职责
3. package metadata 错误可在编译阶段稳定暴露

### M2 Package Review 与 Consumer Bootstrap 调试面

状态：

- [x] Issue 07 增加 package-aware debug / review 输出
- [x] Issue 08 增加 reference Package Reader / Execution Planner bootstrap 路径

目标：

1. 让贡献者或下游 consumer 不必直接阅读完整 `emit-native-json` 才能 review package authoring 结果。
2. 让 runtime-adjacent consumer prototype 可基于正式仓库路径完成最小接入验证。
3. 保持 reference consumer 继续依赖 handoff package / shared model，而不是回退到 AST 或 ad-hoc JSON 扫描。

关键输出：

1. package-aware debug / review 命令或等价稳定输出
2. reference Package Reader / Execution Planner bootstrap 路径
3. 对应 golden / regression 与使用文档

退出标准：

1. 典型 package authoring review 不再只能依赖原始 JSON diff
2. reference consumer 不需要重新猜 package metadata 或 workflow graph
3. 新调试面不绕开 driver / frontend / handoff 公共边界

### M3 Compatibility、Migration 与扩展路径

状态：

- [x] Issue 09 冻结 package metadata authoring 的 compatibility / migration 契约
- [x] Issue 10 验证 runtime-adjacent consumer prototype 的扩展路径与矩阵

目标：

1. 让 package metadata authoring 的格式演进不再依赖隐含约定。
2. 让 future Package Reader / Execution Planner / 其他 runtime-adjacent consumer 清楚哪些字段可稳定依赖。
3. 把新增的 authoring 输入路径纳入 backend / consumer 扩展规则，而不是形成旁路。

关键输出：

1. package metadata authoring compatibility / migration 约束
2. 更新后的 runtime-facing consumer matrix
3. 对应 regression 与扩展指南

退出标准：

1. downstream consumer 清楚 authoring 输入与 emitted package 的关系
2. authoring 边界变更有明确升级流程
3. 新 consumer prototype 不需要绕开 driver / docs / tests 体系

### M4 工程化收口

状态：

- [x] Issue 11 建立 V0.5 authoring / consumer bootstrap regression 与 CI 覆盖
- [x] Issue 12 补齐 V0.5 reference 与 contributor 文档

目标：

1. 让 package metadata authoring、debug/review 输出与 reference consumer bootstrap 进入持续回归。
2. 让新贡献者能按文档跑通“author metadata -> emit package -> review / consume”这条 V0.5 路径。

关键输出：

1. 更新后的 `ctest` / CI 覆盖
2. 更新后的 reference 文档与 contributor guidance

退出标准：

1. V0.5 新增 authoring 正例、负例、golden 与 consumer bootstrap 回归都进入 CI
2. 新贡献者可按文档跑通完整的 V0.5 package authoring 路径

## 关键依赖顺序

1. 先完成 M0，再进入 package metadata authoring 模型实现
2. 先冻结 metadata 输入归属，再扩 frontend / CLI / validator
3. 先稳定 authoring 输入和 emitted package 的关系，再增加 reference consumer bootstrap
4. 先冻结 compatibility / migration，再对下游承诺长期消费
5. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 metadata 输入边界未冻结前直接往 CLI 堆更多 runtime-facing flag
2. 不要把 deployment / connector / tenant / secret 配置塞进 project/workspace descriptor
3. 不要让 reference consumer 回退到 AST 或 raw source 猜 package authoring 结果
4. 不要把 V0.5 的实现目标偷换成完整 runtime / scheduler / launcher 开发

## 当前状态

V0.5 已完成 M0、M1、M2、M3 与 M4：package metadata authoring 数据模型、frontend descriptor 解析入口、`emit-native-json --package` CLI 接入、基于 semantic model 的 authoring 校验、`emit-package-review` package-aware 调试面、direct handoff reference consumer helper、authoring compatibility / migration 契约、runtime-adjacent consumer matrix / contributor 模板、V0.5 regression / CI 切片，以及面向新贡献者的 `project usage -> CLI -> handoff usage -> consumer bootstrap` 文档闭环都已落地。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.5.zh.md](./issue-backlog-v0.5.zh.md)
