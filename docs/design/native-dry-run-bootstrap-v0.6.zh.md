# AHFL Native Dry-Run Bootstrap V0.6

本文冻结 AHFL V0.6 中 local dry-run bootstrap 的最小边界。它承接 V0.6 Execution Plan artifact，但不定义生产 runtime、真实 connector、deployment 或 host integration。

关联文档：

- [native-execution-plan-architecture-v0.6.zh.md](./native-execution-plan-architecture-v0.6.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](./native-consumer-bootstrap-v0.5.zh.md)
- [roadmap-v0.6.zh.md](../plan/roadmap-v0.6.zh.md)
- [native-consumer-matrix-v0.5.zh.md](../reference/native-consumer-matrix-v0.5.zh.md)

## 目标

本文主要回答四个问题：

1. local dry-run runner 允许消费哪些输入。
2. mock capability binding 的最小边界是什么。
3. dry-run trace 需要呈现哪些稳定信息。
4. dry-run runner 与 future runtime / scheduler / connector 的职责如何拆分。

## 非目标

本文不定义：

1. 真实 capability 调用或 provider integration
2. 真实 agent state machine transition 执行
3. scheduler、parallel execution、retry、timeout、persistence
4. deployment、tenant、region、secret、endpoint 或 registry

## 输入边界

V0.6 local dry-run 当前建议只消费三类输入：

```text
Execution Plan
  + DryRunInput
  + CapabilityMockSet
  -> DryRunTrace
```

其中：

1. Execution Plan
   - 提供 workflow DAG、node order、binding reference、value summary
2. DryRunInput
   - 提供 workflow 初始输入值或等价 deterministic fixture
3. CapabilityMockSet
   - 提供 capability mock result，不提供真实 connector 配置

dry-run runner 不应直接消费 AST、raw source、`ahfl.project.json`、`ahfl.workspace.json` 或 `ahfl.package.json`。

## DryRunInput 的最小形态

当前建议的最小 DryRunInput 只包含：

1. 目标 workflow canonical name
2. workflow input fixture
3. 可选的 deterministic seed / run id

当前不允许进入 DryRunInput 的内容：

1. connector endpoint
2. auth / secret / token
3. tenant / region / environment
4. host placement / runtime worker selector

## Capability Mock 的最小形态

当前建议 mock capability 只描述：

1. capability canonical name 或 binding key
2. deterministic mock result
3. 可选 mock invocation label

当前不允许 mock capability 描述：

1. provider SDK class
2. network endpoint
3. auth 配置
4. 重试 / timeout / backoff
5. 租户、地域或 deployment 环境

## Trace 的最小稳定边界

当前建议 DryRunTrace 至少包含：

1. 顶层 `format_version`
2. source execution plan identity
3. workflow canonical name
4. run status
5. node execution order
6. 每个 node 的 dependency satisfaction 结果
7. 每个 node 使用了哪些 binding key / mock capability
8. workflow input summary / return summary
9. runner diagnostics 或 skipped node 信息

当前不承诺稳定的 trace 内容包括：

1. 真实 capability payload schema
2. host side effect
3. wall clock duration / performance metrics
4. runtime thread / process / machine id

## Deterministic 约束

V0.6 local dry-run 需要满足：

1. 相同 Execution Plan + 相同 DryRunInput + 相同 CapabilityMockSet
2. 应得到相同 node order
3. 应得到相同 mock usage 记录
4. 应得到相同 trace summary

这表示 runner 不应读取外部时钟、访问网络，或依赖本机环境变量决定 mock 结果。

## 与 future runtime 的边界

local dry-run runner 不是 future runtime。它只用于：

1. 审查 workflow DAG 是否可被 plan 消费
2. 审查 node dependency 是否可被满足
3. 审查 mock capability 是否完整
4. 审查 return summary 与 trace 是否一致

它不用于：

1. 证明真实 provider 可用
2. 证明 deployment 配置正确
3. 证明 scheduler 性能或并发语义正确
4. 证明 state persistence / recovery 正确

## 建议的最小流程

```text
emit-execution-plan
  -> prepare dry-run input
  -> prepare capability mock set
  -> run local dry-run
  -> emit dry-run trace
```

失败应分层暴露为：

1. plan validation failure
2. missing / duplicate mock failure
3. trace consistency failure

## 当前状态

截至当前设计：

1. V0.6 已把 local dry-run bootstrap 定义为基于 Execution Plan 的 deterministic review 路径
2. mock capability 已与真实 connector / deployment 配置显式分层
3. 后续 runner、trace emitter、compatibility 与 contributor 文档都应以本文为共同边界
