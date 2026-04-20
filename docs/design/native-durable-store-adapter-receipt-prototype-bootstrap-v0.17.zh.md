# AHFL Native Durable Store Adapter Receipt Prototype Bootstrap V0.17

本文定义 AHFL V0.17 的 design baseline。V0.16 已经把 `DurableStoreImportDecision` / `DurableStoreImportDecisionReviewSummary` 做成稳定的 durable-adapter-decision-facing 输入边界；V0.17 的重点不是直接交付真实 durable store adapter executor、object uploader、database writer、import receipt store、resume token 或 crash recovery runtime，而是先把“future real durable store adapter 对 decision 的 machine-facing receipt contract”冻结下来。

换句话说，V0.17 当前要解决的问题是：

1. 当 `DurableStoreImportDecision` 已经稳定存在后，future real durable store adapter 最小要返回什么 machine-facing receipt 才能继续向真实执行层演进。
2. 这层 receipt 应该依赖哪些上游 artifact，不能回退读取哪些 reviewer-facing / debug-facing 输出。
3. 在不引入真实 store URI、object path、database key、provider payload、executor ABI 或 recovery protocol 的前提下，如何先冻结 adapter receipt 的稳定边界。

## V0.17 当前成功标准

V0.17 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing `DurableStoreImportDecisionReceipt`
2. durable store adapter receipt 能回答“当前 decision 是否形成可归档的 adapter receipt record”
3. durable store adapter receipt 能回答“当前 receipt 是否因 decision blocker / partial / failed path 被阻塞或拒绝”
4. durable store adapter receipt review 能把 machine-facing receipt 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在 `DurableStoreImportDecision` 之上，而不是在 review / trace / grep / host log 层私造 receipt state

它不表示：

1. 当前仓库已经实现真实 durable store adapter executor
2. 当前仓库已经支持 object upload、database write、transaction commit protocol、receipt persistence 或 repair job
3. 当前仓库已经承诺真实 import receipt id、resume token、retry token、store URI、object path、database key 或 launcher ABI
4. 当前仓库已经进入 production durable store / recovery 平台开发

## 当前正式依赖方向

V0.17 当前建议链路是：

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
  -> DurableStoreImportDecisionReceipt
  -> DurableStoreImportDecisionReceiptReviewSummary
```

这表示：

1. adapter-receipt-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review、export review、store import review、durable request review、durable decision review 或 host log，而是正式 `DurableStoreImportDecisionReceipt`
2. `DurableStoreImportDecision` 仍是 durable store adapter receipt 的直接 machine-facing 上游
3. V0.17 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export -> store-import -> durable-request -> durable-decision -> durable-receipt` 前进，而不是回退扫描 source、review 文本或 host telemetry
4. `DurableStoreImportDecisionReceiptReviewSummary` 仍只是 readable projection，不是 future real durable store adapter / executor / recovery ABI
5. future real durable store adapter / receipt persistence / recovery protocol 只能在 durable receipt compatibility 冻结后继续规划

## DurableStoreImportDecisionReceipt 的第一边界

V0.17 当前建议 `DurableStoreImportDecisionReceipt` 至少回答下面六类问题：

1. 当前 receipt 是为哪个 durable decision 生成的
2. 当前 receipt 对 decision 的 machine-facing回执结果是什么
3. 当前 receipt 是“可归档进入 future execution trail”、还是“因 contract 缺口暂缓 / 阻塞 / 拒绝”
4. 若当前未进入可归档状态，最小 blocker / capability gap 是什么
5. 当前 receipt boundary 仍停留在 local contract reasoning，还是已经具备 stable adapter-receipt-consumable shape
6. 当前 receipt 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. durable receipt identity 与 source decision identity 已结构化、可稳定序列化
2. receipt status / outcome / blocker 已结构化、可稳定序列化
3. receipt blocker 来源于正式 artifact，而不是 reviewer 文本猜测
4. receipt 只描述“future adapter 当前是否形成可归档回执”，不描述“adapter 最终如何执行 durable import”

当前不要求：

1. 真实 import receipt persistence id
2. recovery handle / resume token / retry token
3. store location / object path / database key / replication metadata / retention policy
4. host / machine / deployment / provider payload / connector credential
5. 真实 executor command、worker lease、distributed recovery orchestration

## Durable Decision / Durable Receipt / Future Real Adapter 的职责拆分

V0.17 当前冻结的分层关系是：

1. `DurableStoreImportDecision`
   - durable-adapter-decision-facing machine artifact
   - 提供 decision identity、decision outcome、capability gap、decision blocker
2. `DurableStoreImportDecisionReceipt`
   - durable-adapter-receipt-facing machine artifact
   - 提供 receipt identity、receipt status / outcome、receipt blocker、receipt boundary
3. `DurableStoreImportDecisionReceiptReviewSummary`
   - reviewer-facing durable adapter receipt summary
   - 只归档 receipt 的可读视图，不重新创造私有 adapter / executor / recovery 状态机
4. future real durable store adapter / receipt persistence / recovery protocol
   - 不属于 V0.17 当前阶段
   - 只能在 durable receipt compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 adapter receipt 语义，再倒逼 `DurableStoreImportDecisionReceipt` 适配
2. 不允许 future real adapter 直接从 decision review、request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 receipt state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 receipt blocker / capability gap 语义

## Layering-Sensitive Compatibility 规则

V0.17 当前还必须额外冻结下面四条 layering-sensitive 规则：

1. `DurableStoreImportDecision` 仍然是 durable adapter receipt 的直接 machine-facing 上游
   - `DurableStoreImportDecisionReceipt` 可以消费 decision
   - 但不应回退让 `DurableStoreImportDecisionReviewSummary` 变成 machine-facing 第一输入
2. `DurableStoreImportDecisionReceiptReviewSummary` 仍然只是 projection
   - 它可以总结 receipt preview / blocker / next-step recommendation
   - 但不应承担真实 import receipt persistence、resume token 派发、store mutation execution 或 recovery command synthesis
3. future real durable store adapter / receipt persistence / recovery protocol 仍然是下一层
   - 它可以复用 durable receipt 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、database key、import receipt、retry token 或 operator payload
4. `DurableStoreImportDecision` 与 `DurableStoreImportDecisionReceipt` 的职责不能合并
   - decision 负责 adapter contract 结论边界
   - receipt 负责 adapter receipt 归档边界

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `DurableStoreImportDecisionReviewSummary`、`DurableStoreImportReviewSummary`、`StoreImportReviewSummary`、`PersistenceExportReviewSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 durable receipt 的第一输入
2. 让 `DurableStoreImportDecisionReceipt` 承担真实 object uploader、database writer、transaction commit protocol 或 recovery launcher ABI
3. 让 `DurableStoreImportDecisionReceiptReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 adapter-receipt-facing 第一事实来源

## Durable Store Adapter Receipt Review 的当前边界

V0.17 当前明确区分三类与 durable store adapter receipt 相关的输出：

1. `DurableStoreImportDecision`
   - 回答“当前 future adapter contract 对 request 的 machine-facing decision 到底长什么样”
   - 第一输入是 `DurableStoreImportRequest`
2. future `DurableStoreImportDecisionReceipt`
   - 回答“当前 future adapter contract 对 decision 的 machine-facing receipt 到底长什么样”
   - 第一输入是 `DurableStoreImportDecision`
3. future `DurableStoreImportDecisionReceiptReviewSummary`
   - 回答“当前 reviewer 应如何理解 adapter receipt、blocker 与 next step”
   - 第一输入是 `DurableStoreImportDecisionReceipt`

因此：

1. 不允许用 `DurableStoreImportDecisionReviewSummary` 倒推 `DurableStoreImportDecisionReceipt`
2. 不允许用 `DurableStoreImportDecisionReceiptReviewSummary` 倒推真实 durable adapter / receipt persistence / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 adapter-receipt-facing 第一事实来源

## V0.17 Scope 冻结结论

V0.17 当前范围只包括：

1. local / offline durable store adapter receipt 的 scope 与术语冻结
2. future real durable store adapter 对 V0.16 decision 的最小 machine-facing receipt 边界
3. durable decision、durable receipt、receipt review 与 future real adapter 的职责切分准备

V0.17 当前明确不包括：

1. 真实 durable store adapter executor
2. 真实 import receipt persistence、store URI、object path、database key、retention policy 或 provider payload
3. 真实 retry token、resume token、launcher ABI、worker lease 或 crash recovery protocol
4. host telemetry、connector credential、deployment metadata 或 operator payload

## Durable Receipt 与 Future Real Adapter 的升级关系

V0.17 当前冻结的升级关系是：

1. `DurableStoreImportDecisionReceipt`
   - local / offline machine artifact
   - 只回答“当前 future adapter contract 是否形成可归档回执”
2. future real durable store adapter / receipt persistence / recovery protocol
   - durable / executable protocol layer
   - 才回答“如何真正执行 durable import、如何持久化 receipt、如何恢复、由谁执行”

这意味着：

1. future real adapter / recovery protocol 可以复用 `DurableStoreImportDecisionReceipt` 的 receipt identity、receipt outcome、receipt blocker 与 receipt boundary
2. future real adapter / recovery protocol 不应要求 V0.17 当前就携带真实 receipt persistence id、resume token、store URI、object path、database key 或 import receipt payload
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `DurableStoreImportDecisionReceipt` / `DurableStoreImportDecisionReceiptReviewSummary`
