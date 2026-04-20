# AHFL Native Durable Store Adapter Decision Prototype Bootstrap V0.16

本文定义 AHFL V0.16 的 design baseline。V0.15 已经把 `DurableStoreImportRequest` / `DurableStoreImportReviewSummary` 做成稳定的 durable-store-import-facing 输入边界；V0.16 的重点不是直接交付真实 durable store adapter、object uploader、database writer、resume token、import receipt 或 crash recovery runtime，而是先把“future real durable store adapter 对 request 的 machine-facing decision contract”冻结下来。

换句话说，V0.16 当前要解决的问题是：

1. 当 `DurableStoreImportRequest` 已经稳定存在后，future real durable store adapter 最小要返回什么 machine-facing decision 才能继续向真实执行层演进。
2. 这层 decision 应该依赖哪些上游 artifact，不能回退读取哪些 reviewer-facing / debug-facing 输出。
3. 在不引入真实 store URI、object path、database key、provider payload 或 executor ABI 的前提下，如何先冻结 adapter decision 的稳定边界。

## V0.16 当前成功标准

V0.16 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing `DurableStoreImportDecision`
2. durable store adapter decision 能回答“当前 request 是否已被 future adapter contract 接受、延后、阻塞或拒绝”
3. durable store adapter decision 能回答“当前 adapter contract 仍缺什么最小能力 / 前提”
4. durable store adapter decision review 能把 machine-facing decision 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在 `DurableStoreImportRequest` 之上，而不是在 review / trace / grep / host log 层私造 adapter state

它不表示：

1. 当前仓库已经实现真实 durable store adapter
2. 当前仓库已经支持 object upload、database write、transaction commit protocol 或 repair job
3. 当前仓库已经承诺真实 import receipt、resume token、store URI、object path、database key 或 launcher ABI
4. 当前仓库已经进入 production durable store / recovery 平台开发

## 当前正式依赖方向

V0.16 当前建议链路是：

```text
ExecutionPlan
  + DryRunRequest
  + CapabilityMockSet
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> SchedulerSnapshot
  -> CheckpointRecord
  -> CheckpointPersistenceDescriptor
  -> PersistenceExportManifest
  -> StoreImportDescriptor
  -> DurableStoreImportRequest
  -> DurableStoreImportDecision
  -> DurableStoreImportDecisionReviewSummary
```

这表示：

1. adapter-decision-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review、export review、store import review、durable request review 或 host log，而是正式 `DurableStoreImportDecision`
2. `DurableStoreImportRequest` 仍是 durable store adapter decision 的直接 machine-facing 上游
3. V0.16 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export -> store-import -> durable-request -> durable-decision` 前进，而不是回退扫描 source、review 文本或 host telemetry
4. `DurableStoreImportDecisionReviewSummary` 仍只是 readable projection，不是 future real durable store adapter / executor ABI
5. future real durable store adapter / receipt / recovery protocol 只能在 durable decision compatibility 冻结后继续规划

## DurableStoreImportDecision 的第一边界

V0.16 当前建议 `DurableStoreImportDecision` 至少回答下面六类问题：

1. 当前 decision 是为哪个 durable store import request 生成的
2. 当前 adapter contract 对 request 的判定结果是什么
3. 当前 decision 是“可接受进入 future execution”、还是“因 contract 缺口暂缓 / 阻塞 / 拒绝”
4. 若当前未被接受，最小 blocker / capability gap 是什么
5. 当前 decision boundary 仍停留在 local contract reasoning，还是已经具备 stable adapter-decision-consumable shape
6. 当前 decision 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. durable decision identity 与 source request identity 已结构化、可稳定序列化
2. decision status / outcome / blocker 已结构化、可稳定序列化
3. decision blocker 来源于正式 artifact，而不是 reviewer 文本猜测
4. decision 只描述“future adapter 当前是否接受该 request”，不描述“adapter 最终如何执行 durable import”

当前不要求：

1. 真实 import receipt / store acknowledgment id
2. recovery handle / resume token / retry token
3. store location / object path / database key / replication metadata / retention policy
4. host / machine / deployment / provider payload / connector credential
5. 真实 executor command、worker lease、distributed recovery orchestration

## Durable Store Import Request / Durable Store Import Decision / Future Real Adapter 的职责拆分

V0.16 当前冻结的分层关系是：

1. `DurableStoreImportRequest`
   - durable-store-import-facing machine artifact
   - 提供 request identity、requested artifact set、adapter readiness 与 blocker
2. `DurableStoreImportDecision`
   - durable-store-adapter-decision-facing machine artifact
   - 提供 adapter contract 对 request 的接受 / 暂缓 / 阻塞 / 拒绝结论，以及 decision blocker / capability gap
3. `DurableStoreImportDecisionReviewSummary`
   - reviewer-facing durable adapter decision summary
   - 只归档 decision 的可读视图，不重新创造私有 adapter / executor / recovery 状态机
4. future real durable store adapter / receipt / recovery protocol
   - 不属于 V0.16 当前阶段
   - 只能在 durable decision compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 adapter decision 语义，再倒逼 `DurableStoreImportDecision` 适配
2. 不允许 future real adapter 直接从 request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 decision state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 decision blocker / capability gap 语义

## Layering-Sensitive Compatibility 规则

V0.16 当前还必须额外冻结下面四条 layering-sensitive 规则：

1. `DurableStoreImportRequest` 仍然是 durable adapter decision 的直接 machine-facing 上游
   - `DurableStoreImportDecision` 可以消费 request
   - 但不应回退让 `DurableStoreImportReviewSummary` 变成 machine-facing 第一输入
2. `DurableStoreImportDecisionReviewSummary` 仍然只是 projection
   - 它可以总结 decision preview / capability gap / next-step recommendation
   - 但不应承担真实 import receipt synthesis、resume token 派发、store mutation execution 或 recovery command synthesis
3. future real durable store adapter / receipt / recovery protocol 仍然是下一层
   - 它可以复用 durable decision 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、database key、import receipt、retry token 或 operator payload
4. `DurableStoreImportRequest` 与 `DurableStoreImportDecision` 的职责不能合并
   - request 负责 adapter-facing request boundary
   - decision 负责 adapter contract 结论边界

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `DurableStoreImportReviewSummary`、`StoreImportReviewSummary`、`PersistenceExportReviewSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 durable decision 的第一输入
2. 让 `DurableStoreImportDecision` 承担真实 object uploader、database writer、transaction commit protocol 或 recovery launcher ABI
3. 让 `DurableStoreImportDecisionReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 adapter-decision-facing 第一事实来源

## Durable Store Adapter Decision Review 的当前边界

V0.16 当前明确区分三类与 durable store adapter decision 相关的输出：

1. `DurableStoreImportRequest`
   - 回答“当前 future adapter 最小可稳定消费的 request 到底长什么样”
   - 第一输入是 `StoreImportDescriptor`
2. future `DurableStoreImportDecision`
   - 回答“当前 future adapter contract 对 request 的 machine-facing decision 到底长什么样”
   - 第一输入是 `DurableStoreImportRequest`
3. future `DurableStoreImportDecisionReviewSummary`
   - 回答“当前 reviewer 应如何理解 adapter decision、capability gap 与 next step”
   - 第一输入是 `DurableStoreImportDecision`

因此：

1. 不允许用 `DurableStoreImportReviewSummary` 倒推 `DurableStoreImportDecision`
2. 不允许用 `DurableStoreImportDecisionReviewSummary` 倒推真实 durable adapter / receipt / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 adapter-decision-facing 第一事实来源

## V0.16 Scope 冻结结论

V0.16 当前范围只包括：

1. local / offline durable store adapter decision 的 scope 与术语冻结
2. future real durable store adapter 对 V0.15 request 的最小 machine-facing decision 边界
3. durable request、durable decision、decision review 与 future real adapter 的职责切分准备

V0.16 当前明确不包括：

1. 真实 durable store adapter
2. 真实 import receipt、store URI、object path、database key、retention policy 或 provider payload
3. 真实 retry token、resume token、launcher ABI、worker lease 或 crash recovery protocol
4. host telemetry、connector credential、deployment metadata 或 operator payload

## Durable Decision 与 Future Real Adapter 的升级关系

V0.16 当前冻结的升级关系是：

1. `DurableStoreImportDecision`
   - local / offline machine artifact
   - 只回答“当前 future adapter contract 是否接受该 request”
2. future real durable store adapter / receipt / recovery protocol
   - durable / executable protocol layer
   - 才回答“如何真正执行 durable import、如何产出 receipt、如何恢复、由谁执行”

这意味着：

1. future real adapter / recovery protocol 可以复用 `DurableStoreImportDecision` 的 decision identity、decision outcome、capability gap 与 decision boundary
2. future real adapter / recovery protocol 不应要求 V0.16 当前就携带真实 receipt id、resume token、store URI、object path、database key 或 import receipt payload
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `DurableStoreImportDecision` / `DurableStoreImportDecisionReviewSummary`
