# AHFL Core V0.3 IR Compatibility And Versioning

本文冻结 AHFL IR / JSON IR 在 V0.3 阶段的版本化与兼容性契约，作为 `emit-ir`、`emit-ir-json`、下游 tooling，以及后续 Issue 10+ 扩展的共同约束。

关联文档：

- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
- [ir-backend-architecture-v0.2.zh.md](../design/ir-backend-architecture-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)
- [issue-backlog-v0.3.zh.md](../plan/issue-backlog-v0.3.zh.md)

## 目标

本文主要回答四个问题：

1. 当前 IR 的版本标识是什么，出现在什么位置。
2. 哪些变化可以视为兼容扩展，哪些变化必须 bump 版本。
3. `emit-ir` 与 `emit-ir-json` 的稳定性承诺如何对齐。
4. 后续改 IR 时，文档、代码、golden、测试要按什么流程一起改。

## 当前版本标识

当前稳定版本标识为：

- `format_version = "ahfl.ir.v1"`

当前实现中的单一来源位于：

- `include/ahfl/ir/ir.hpp`
  - `ahfl::ir::kFormatVersion`

该版本号会同时出现在：

1. 文本 IR 的首行
2. JSON IR 的顶层 `format_version`

这表示当前仓库里：

- `emit-ir`
- `emit-ir-json`

属于同一个 IR family，只是两种不同展示形态，而不是两个彼此独立版本体系。

## 兼容性层级

### 1. 语义兼容层

当前真正冻结的兼容层是：

- `ir::Program` 的结构化语义边界

包括：

1. 顶层 program 结构
2. declaration kind 集合
3. JSON 字段名与字段语义
4. `formal_observations`
5. flow / workflow summary 的结构化含义
6. `provenance`、`format_version` 等公共元信息

### 2. 文本展示层

`emit-ir` 是人读与 golden diff 友好的文本形态。

它的兼容承诺是：

1. 与 JSON IR 共享同一个 `format_version`
2. 共享同一组结构化语义边界
3. 关键语义块不会在不 bump 版本的情况下被静默删除或偷换含义

它不承诺：

1. 绝对逐字符排版 ABI
2. 缩进、空行、换行风格长期不变
3. 适合脚本稳定解析

因此：

- 机器消费应优先依赖 `emit-ir-json`
- `emit-ir` 主要用于 code review、golden diff、人工审查

## 什么算兼容扩展

在不修改 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 在 JSON object 中新增可选字段，且不改变旧字段含义
2. 在文本 IR 中新增对应的可识别语义块，且不删除旧语义块
3. 新增 declaration/summary 内的附加信息，只要旧 consumer 仍可忽略
4. 为已有字段补充更明确的文档说明，但不改变字段语义
5. 为现有 IR 节点增加新的、可选的辅助 summary

这些扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. JSON consumer 仍可按旧逻辑工作
4. 文档已同步说明新增字段可忽略

## 什么算 breaking change

出现以下任一情况时，必须 bump `format_version`：

1. 删除已有 JSON 字段
2. 重命名已有 JSON 字段
3. 改变已有字段的语义含义
4. 改变 declaration / observation / summary 的节点种类语义
5. 让旧 consumer 无法安全忽略新增内容
6. 让文本 IR 与 JSON IR 不再表示同一套结构化语义边界

典型 breaking change 示例：

1. 把 `provenance.source_path` 改名
2. 把 `workflow_node_output` 改成另一套不兼容枚举值
3. 把 `summary.reads` 从“读取来源”改成“执行顺序”
4. 删除 `formal_observations`

## `emit-ir` 与 `emit-ir-json` 的共同承诺

两者当前必须满足：

1. 使用同一个 `format_version`
2. 表达同一个 `ir::Program` 语义模型
3. 对同一 declaration / observation / summary 暴露一致含义

这意味着：

1. 若 JSON IR 需要 bump 版本，文本 IR 也必须一起 bump
2. 若文本 IR 只是排版调整，而结构语义不变，则不需要 bump
3. 若新增字段只出现在 JSON IR，而文本 IR 完全不暴露对应语义，应视为边界不一致，原则上不应接受

## Consumer 建议

### 机器消费

优先使用：

- `emit-ir-json`

并至少做以下检查：

1. 读取并校验 `format_version`
2. 对未知字段保持 forward-compatible 忽略能力
3. 不依赖文本 IR 的换行和缩进

### 人工审查

优先使用：

- `emit-ir`

适用场景：

1. golden diff
2. declaration ownership 审查
3. flow / workflow summary 快速检查

## 变更流程

后续任何 IR 变化，至少要同步完成四件事：

1. 更新 `include/ahfl/ir/ir.hpp` 与对应实现
2. 更新本文档与 [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
3. 更新受影响 golden
4. 更新或新增回归测试

若属于 breaking change，还必须额外完成：

1. bump `ahfl::ir::kFormatVersion`
2. 在文档中写明迁移影响
3. 明确旧版本是否继续支持

## 当前状态

截至 V0.3 当前实现：

1. 当前稳定版本仍为 `ahfl.ir.v1`
2. `emit-ir` 与 `emit-ir-json` 已共享同一版本标识
3. flow summary、workflow value summary、project-aware provenance 都已进入同一兼容边界
