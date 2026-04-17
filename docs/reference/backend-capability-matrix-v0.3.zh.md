# AHFL Core V0.3 Backend Capability Matrix

本文给出 AHFL 当前 backend 的能力矩阵，目标是让新增 backend、下游 consumer 和贡献者都能快速判断“哪个 backend 消费了哪些 IR 能力，哪些只是参考输出，哪些仍然不保留完整语义”。

关联文档：

- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](../design/formal-backend-v0.2.zh.md)

## 当前 backend 列表

当前仓库中的 backend/导出命令包括：

- `emit-ir`
- `emit-ir-json`
- `emit-native-json`
- `emit-summary`
- `emit-smv`

其中：

1. `emit-ir`
   - 人读型结构化 IR
2. `emit-ir-json`
   - 机器消费型结构化 IR
3. `emit-native-json`
   - 面向 future Native/runtime-adjacent consumer 的 handoff package JSON
4. `emit-summary`
   - 参考 backend，实现最小的 backend 扩展路径，并输出 capability-oriented summary
5. `emit-smv`
   - 受限 formal backend

## 能力矩阵

| 能力 | `emit-ir` | `emit-ir-json` | `emit-native-json` | `emit-summary` | `emit-smv` |
|------|-----------|----------------|--------------------|----------------|------------|
| 共享版本字段 | IR 版本 | IR 版本 | handoff 版本 | summary 版本 | 间接依赖 IR 版本 |
| declaration 结构 | 全量 | 全量 | executable subset | 计数/摘要 | 部分消费 |
| project-aware provenance | 可见 | 可见 | 可见 | 仅计数/开关 | 不直接输出 |
| package metadata 边界 | 否 | 否 | 是 | 否 | 否 |
| execution graph / lifecycle 摘要 | 否 | 否 | 是 | 否 | 仅间接消费 |
| capability binding slots | 否 | 否 | 是 | 否 | 否 |
| policy obligations | 否 | 否 | 是 | 否 | 仅间接消费 |
| `formal_observations` | 可见 | 可见 | 可见 | 计数/摘要 | 直接消费 |
| flow statement tree | 全量 | 全量 | 不输出 | 不输出 tree | 不直接消费 tree |
| flow summary | 可见 | 可见 | 当前不输出 | 汇总消费 | 直接消费控制摘要 |
| workflow expr tree | 全量 | 全量 | 不输出 | 不输出 tree | 不直接消费值流 |
| workflow value summary | 可见 | 可见 | 可见 | 汇总消费 | 当前不消费 |
| temporal formulas | 全量 | 全量 | 不直接导出公式树 | 不输出公式 | 部分 lower 到 LTL |
| 完整值语义 | 否 | 否 | 否 | 否 | 否 |

## 读取方式说明

### `emit-ir`

适合：

1. 人工审查
2. golden diff
3. declaration / flow / workflow 结构阅读

特点：

1. 保留语义最完整
2. 不是稳定脚本接口

### `emit-ir-json`

适合：

1. 下游工具
2. CI 结构化消费
3. 版本检查与字段读取

特点：

1. 是机器消费首选
2. 与 `emit-ir` 共享同一 `format_version`

### `emit-summary`

适合：

1. 快速确认某个 program 是否用到了哪些 IR 能力
2. 审查 project-aware / observations / flow summary / workflow summary 是否进入输出边界
3. 作为新增 backend 的最小参考实现

特点：

1. 不试图保留完整语义树
2. 只输出稳定摘要
3. 验证了 `BackendKind -> driver -> CLI -> golden -> docs` 的完整扩展路径

### `emit-native-json`

适合：

1. future Native/runtime-adjacent consumer
2. handoff package golden diff
3. 审查 executable target、capability binding slot、policy obligation、formal observation 与 project-aware provenance 的 runtime-facing 投影
4. 审查 workflow execution graph、节点 lifecycle 摘要与 workflow value summary

特点：

1. 基于 validate 后的统一 IR 边界
2. 输出 handoff package，而不是 full IR
3. 当前会显式导出 `execution_graph` 和每个 workflow node 的最小 `lifecycle` 摘要
4. 当前会显式导出 package 级 `policy_obligations`
5. 当前不会猜 package identity / entry / export 默认值；未提供 metadata 来源时会稳定输出空 metadata 区段

### `emit-smv`

适合：

1. 受限 formal 导出
2. state-machine / workflow lifecycle / temporal property 检查

特点：

1. 只消费 IR 的一部分
2. 对 contract / workflow temporal 采用 observation abstraction
3. 当前不导出完整 flow/value/workflow dataflow execution semantics

## 当前推荐

若目标是：

1. 做机器消费
   - 用 `emit-ir-json`
2. 做人读审查
   - 用 `emit-ir`
3. 做快速 capability 检查或验证新增 backend 路径
   - 用 `emit-summary`
4. 做 Native/runtime-facing handoff 导出
   - 用 `emit-native-json`
5. 做受限 formal export
   - 用 `emit-smv`

## 对新增 backend 的含义

若后续继续新增 backend，应至少回答三个问题：

1. 它消费的是 full IR、summary IR，还是 observation-backed subset
2. 它是否需要新增共享 IR 字段
3. 它在这张矩阵中新增了哪一列，以及和现有 backend 有何边界差异

## 当前状态

截至当前实现：

1. `emit-summary` 已作为参考 backend 进入仓库
2. `emit-native-json` 已把 handoff package 正式接入统一 backend 扩展路径
3. backend capability matrix 已文档化
4. `emit-smv` 仍然是唯一正式 formal exporter，但 backend 扩展路径不再只靠它单点验证
