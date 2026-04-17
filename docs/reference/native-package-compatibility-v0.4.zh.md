# AHFL Native Package V0.4 Compatibility And Versioning

本文冻结 `emit-native-json` / handoff package 在 V0.4 阶段的版本化与兼容性契约，作为 future Native/runtime-adjacent consumer、golden、回归测试与后续 Issue 10+ 扩展的共同约束。

关联文档：

- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)
- [native-package-metadata-v0.4.zh.md](../design/native-package-metadata-v0.4.zh.md)
- [issue-backlog-v0.4.zh.md](../plan/issue-backlog-v0.4.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 handoff package 的版本标识是什么，出现在什么位置。
2. 哪些变化可视为兼容扩展，哪些变化必须 bump 版本。
3. `emit-native-json` 当前对 consumer 的稳定性承诺是什么。
4. 后续扩 handoff package 时，文档、代码、golden 和测试要按什么流程一起改。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.native-package.v1"`

当前实现中的单一来源位于：

- `include/ahfl/handoff/package.hpp`
  - `ahfl::handoff::kFormatVersion`

该版本号当前会出现在两个位置：

1. `emit-native-json` 顶层 `format_version`
2. `metadata.identity.format_version`
   - 仅当 package identity 存在时出现

当前契约要求：

1. 顶层 `format_version` 是 consumer 的主校验入口。
2. 若 `metadata.identity` 存在，则其 `format_version` 必须与顶层版本一致。
3. 仓库内实现应把 `PackageIdentity.format_version` 规范化到 `ahfl.native-package.v1`，避免出现同一 package 内部自相矛盾的版本字段。

## 当前冻结的兼容层

当前真正冻结的兼容层是：

- handoff package 的 runtime-facing 结构化语义边界

包括：

1. 顶层 package 结构
2. `metadata` 的字段名与字段语义
3. `executable_targets` 的节点种类与结构语义
4. `execution_graph` / `lifecycle` 的结构语义
5. `capability_binding_slots`
6. `policy_obligations`
7. `formal_observations`
8. node `input_summary` 与 workflow `return_summary`

当前不冻结为长期 ABI 的内容包括：

1. JSON 对象字段的物理输出顺序
2. 空数组之间的排版细节
3. future runtime 私有 deployment 配置
4. 完整 temporal / expr 公式树作为 handoff package 的直接 ABI

## Consumer 当前应依赖什么

当前 consumer 可以稳定依赖：

1. 顶层 `format_version`
2. `metadata.identity`、`entry_target`、`export_targets` 的字段语义
3. `agent` / `workflow` 两类 executable target
4. workflow `execution_graph.entry_nodes`
5. workflow `execution_graph.dependency_edges`
6. workflow node `lifecycle` 的最小摘要
7. `capability_binding_slots[].required_by_targets`
8. `policy_obligations[].owner_target`、`kind`、`clause_index`、`observation_symbols`
9. `formal_observations`
10. workflow `input_summary` / `return_summary`

当前 consumer 不应依赖：

1. 某些字段总是出现为非空
2. metadata 已经具备 package identity / entry / export 默认值
3. `formal_observations` 列表中的顺序具备额外语义
4. handoff package 会直接承诺完整执行器输入

换句话说：

- consumer 应把 handoff package 当作“runtime-facing structured contract”，而不是“完整 runtime execution ABI”。

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在现有枚举空间外增加新 kind，但旧 consumer 仍可安全忽略该节点
3. 在 `metadata` 中新增可忽略字段
4. 在 `lifecycle`、`execution_graph`、`policy_obligations` 中新增可选辅助字段
5. 补充更明确的文档说明，而不改变旧字段语义

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 仍能通过读取 `format_version` 和忽略未知字段安全工作
4. 文档已同步声明新增字段的可忽略性

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变已有字段的语义含义
4. 改变 executable target / policy obligation / capability slot 的 kind 语义
5. 改变 `execution_graph`、`lifecycle`、`input_summary`、`return_summary` 的结构含义
6. 让旧 consumer 无法安全忽略新增内容

典型 breaking change 示例：

1. 把 `dependency_edges.from_node` / `to_node` 改成另一套不兼容命名
2. 把 `policy_obligations.kind = "workflow_safety"` 改成另一套枚举而不保留旧值
3. 把 `capability_binding_slots.required_by_targets` 从“依赖该 capability 的 executable targets”改成“声明该 capability 的模块”
4. 删除 `formal_observations`

## 当前稳定性承诺

`emit-native-json` 当前必须满足：

1. 共享单一 `ahfl::handoff::kFormatVersion`
2. 表达同一个 handoff package 结构化语义模型
3. 不要求 consumer 回看 AST 或 raw source 补语义
4. 在未显式提供 metadata 输入来源时，仍输出结构稳定的空 metadata 区段

这意味着：

1. 若 handoff package 发生 breaking change，必须 bump `ahfl::handoff::kFormatVersion`
2. 若只是新增可选字段且旧语义不变，则不需要 bump
3. 若文档与 goldens 已把某个字段列为稳定边界，就不能在不 bump 版本的情况下静默偷换含义

## Consumer 建议

### 机器消费

优先使用：

- `emit-native-json`

并至少做以下检查：

1. 读取并校验顶层 `format_version`
2. 若 `metadata.identity` 存在，再校验其 `format_version`
3. 对未知字段保持 forward-compatible 忽略能力
4. 不依赖 JSON object 的字段顺序

### 人工审查

优先使用：

- native package golden
- `tests/native/`

适用场景：

1. handoff contract review
2. runtime-facing surface diff
3. compatibility 回归审查

## 变更流程

后续任何 handoff package 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/handoff/package.hpp` 与对应实现
2. 更新本文档及相关 reference/design 文档
3. 更新受影响 native golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::handoff::kFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至当前实现：

1. 当前稳定版本为 `ahfl.native-package.v1`
2. `emit-native-json` 已稳定导出 executable target、execution graph、lifecycle、capability binding、policy obligations、formal observations 与 workflow value summary
3. package metadata 的外部输入来源仍处于后续阶段，但当前版本字段契约已冻结
