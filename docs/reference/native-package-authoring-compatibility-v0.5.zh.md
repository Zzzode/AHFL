# AHFL Native Package Authoring Compatibility V0.5

本文冻结 `ahfl.package.json` 在 V0.5 阶段的版本化、兼容扩展与迁移契约。它关注的是 package authoring 输入本身，以及“authoring input -> emitted handoff package”这条映射边界，而不是 `emit-native-json` 输出格式本身。

关联文档：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 package authoring descriptor 的版本标识是什么。
2. 哪些 authoring 变化可视为兼容扩展，哪些属于 breaking change。
3. authoring compatibility 与 emitted package compatibility 的关系是什么。
4. 后续变更 authoring descriptor 时，文档、代码、golden 与回归要按什么流程同步。

## 当前版本标识

当前稳定 authoring 版本标识为：

- `format_version = "ahfl.package-authoring.v0.5"`

当前实现中的单一来源位于：

- `include/ahfl/frontend/frontend.hpp`
  - `ahfl::kPackageAuthoringFormatVersion`

当前契约要求：

1. `ahfl.package.json` 顶层 `format_version` 是 authoring parser 的主校验入口。
2. V0.5 parser 只接受 `ahfl.package-authoring.v0.5`。
3. `format_version` 只描述 authoring descriptor 格式，不等于 emitted package 的 `ahfl.native-package.v1`。

## 当前冻结的兼容层

当前真正冻结的 authoring compatibility 层是：

1. `ahfl.package.json` 的顶层字段名与字段语义
2. `package.name` / `package.version` 的含义
3. `entry.kind` / `entry.name` 的含义
4. `exports[]` 中 `(kind, name)` 的含义
5. `capability_bindings[].capability` / `binding_key` 的含义
6. display name / canonical name 的匹配与规范化规则
7. “authoring input -> PackageMetadata -> emitted package metadata”的字段映射关系

当前不冻结为长期 ABI 的内容包括：

1. JSON 对象字段顺序
2. 空数组或空对象的排版细节
3. descriptor 文件在仓库中的物理存放位置
4. CLI 是否未来增加更便捷的发现方式

## Authoring 与 Emitted Package 的关系

authoring descriptor 与 emitted package 是两个独立版本层：

1. authoring descriptor 版本
   - `ahfl.package-authoring.v0.5`
2. emitted handoff package 版本
   - `ahfl.native-package.v1`

两者关系是：

1. authoring parser 负责接受或拒绝输入格式
2. semantic validation 负责把 display name 规范化为 canonical name，并拒绝 unknown / ambiguous / wrong-kind 引用
3. handoff lowering 负责把合法 authoring 输入映射为 `PackageMetadata`
4. emitted package compatibility 继续由 `ahfl.native-package.v1` 契约约束

这意味着：

1. authoring format 未变时，不应因为 emitted package 的兼容扩展而 bump authoring 版本
2. emitted package 发生 breaking change 时，也不自动意味着 authoring format 要 bump
3. 若 authoring 字段语义变化会改变 emitted metadata 的既有含义，则 authoring 与 emitted package 两侧都应同步评估兼容性影响

## 什么算兼容扩展

在不修改 `ahfl.package-authoring.v0.5` 的前提下，当前允许的兼容扩展包括：

1. 新增可选顶层字段，且旧 parser / consumer 可安全忽略
2. 在 `package`、`entry`、`exports[]`、`capability_bindings[]` 中新增可选字段，且不改变旧字段语义
3. 补充更明确的文档说明，而不改变现有匹配 / 规范化 / 诊断边界
4. 新增不会改变既有成功输入含义的 diagnostics 或回归

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧的合法 authoring 输入在新实现下仍保持相同语义
4. 新字段对旧实现应能被安全忽略，或在文档中明确要求升级 parser

## 什么算 Breaking Change

出现以下任一情况时，必须 bump authoring `format_version`：

1. 删除已有 authoring 字段
2. 重命名已有 authoring 字段
3. 改变已有字段的语义含义
4. 改变 display name / canonical name 的匹配与规范化规则
5. 改变 `entry`、`exports`、`capability_bindings` 的最小合法性约束
6. 让既有合法 authoring 输入在新版本下产生不同 emitted metadata 含义

典型 breaking change 示例：

1. 把 `binding_key` 改名为 `slot`
2. 把 `exports` 从显式列表改成默认隐式包含 `entry`
3. 把 display name 匹配从“唯一命中”改成“多命中时任选一个”
4. 让 `entry.kind = "agent"` 在语义上改为表示别的 executable target

## 迁移要求

若 future 版本引入 breaking change，至少要同步完成：

1. bump `ahfl::kPackageAuthoringFormatVersion`
2. 更新 parser / validator 实现
3. 更新 authoring 正例、负例与相关 golden
4. 在文档中写明旧字段到新字段的迁移映射
5. 明确旧版本是否继续支持，或是否需要单独迁移工具

若只是兼容扩展，则至少要同步完成：

1. 更新相关 reference / design 文档
2. 更新 parser / validator 与回归
3. 确认现有 authoring 输入不发生静默语义漂移

## 变更流程

后续任何 package authoring 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/frontend/frontend.hpp` 与 `src/frontend/project.cpp`
2. 更新 `src/cli/ahflc.cpp`、`src/handoff/package.cpp` 等受影响映射路径
3. 更新 `tests/project/`、`tests/native/`、`tests/review/` 或 `tests/handoff/` 中受影响回归
4. 更新本文档与关联 reference/design 文档

若变化会影响 emitted package metadata 的稳定语义，还必须额外同步：

1. 更新 [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
2. 更新 consumer matrix / handoff usage 文档
3. 重新评估 `emit-native-json` 与 reference consumer golden

## 当前状态

截至当前实现：

1. 当前稳定 authoring 版本为 `ahfl.package-authoring.v0.5`
2. parser / CLI / semantic validation 已围绕这一版本接入正式回归
3. package authoring compatibility 与 emitted package compatibility 已明确分层，不再依赖隐含约定
