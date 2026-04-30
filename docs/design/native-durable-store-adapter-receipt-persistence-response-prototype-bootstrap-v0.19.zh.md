# AHFL Native Durable Store Adapter Receipt Persistence Response Prototype Bootstrap V0.19

本文定义 AHFL V0.19 的 design baseline。V0.18 已经把 `PersistenceRequest` / `PersistenceReviewSummary` 做成稳定的 durable-adapter-receipt-persistence-facing 输入边界；V0.19 的重点不是直接交付真实 receipt persistence writer、object uploader、database writer、persistence id、resume token 或 crash recovery runtime，而是先把"future real durable store adapter 对 persistence request 的 machine-facing response contract"冻结下来。

换句话说，V0.19 当前要解决的问题是：

1. 当 `PersistenceRequest` 已经稳定存在后，future real durable store adapter 最小要返回什么 machine-facing response 才能继续向真实持久化执行层演进。
2. 这层 response 应该依赖哪些上游 artifact，不能回退读取哪些 reviewer-facing / debug-facing 输出。
3. 在不引入真实 store URI、object path、database key、provider payload、executor ABI 或 recovery protocol 的前提下，如何先冻结 adapter response 的稳定边界。

## V0.19 当前成功标准

V0.19 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing `Response`
2. durable store adapter receipt persistence response 能回答"当前 persistence request 是否被 future adapter contract 确认接受、延后、阻塞或拒绝"
3. durable store adapter receipt persistence response 能回答"当前 response 是否因 persistence request blocker / partial / failed path 被阻塞或拒绝"
4. durable store adapter receipt persistence response review 能把 machine-facing response 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在 `PersistenceRequest` 之上，而不是在 review / trace / grep / host log 层私造 response state

它不表示：

1. 当前仓库已经实现真实 receipt persistence writer
2. 当前仓库已经支持 object upload、database write、transaction commit protocol、durable retry queue 或 repair job
3. 当前仓库已经承诺真实 persistence id、resume token、retry token、store URI、object path、database key 或 launcher ABI
4. 当前仓库已经进入 production durable store / recovery 平台开发

## 当前正式依赖方向

V0.19 当前建议链路是：

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
  -> Request
  -> Decision
  -> Receipt
  -> PersistenceRequest
  -> Response
  -> ResponseReviewSummary
```

这表示：

1. adapter-receipt-persistence-response-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review、export review、store import review、durable request review、durable decision review、durable receipt review、persistence request review 或 host log，而是正式 `Response`
2. `PersistenceRequest` 仍是 durable store adapter receipt persistence response 的直接 machine-facing 上游
3. V0.19 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> export -> store-import -> durable-request -> durable-decision -> durable-receipt -> durable-receipt-persistence-request -> durable-receipt-persistence-response` 前进，而不是回退扫描 source、review 文本或 host telemetry
4. `ResponseReviewSummary` 仍只是 readable projection，不是 future real durable store adapter / persistence executor / recovery ABI
5. future real durable store adapter / persistence executor / recovery protocol 只能在 durable receipt persistence response compatibility 冻结后继续规划

## Response 的第一边界

V0.19 当前建议 `Response` 至少回答下面六类问题：

1. 当前 response 是为哪个 persistence request 生成的
2. 当前 adapter contract 对 persistence request 的响应结果是什么
3. 当前 response 是"已被确认接受"、还是"因 contract 缺口阻塞 / 延后 / 拒绝"
4. 若当前未被接受，最小 blocker / capability gap 是什么
5. 当前 response boundary 仍停留在 local contract reasoning，还是已经具备 stable adapter-response-consumable shape
6. 当前 response 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. response identity 与 source persistence request identity 已结构化、可稳定序列化
2. response status / outcome / blocker 已结构化、可稳定序列化
3. response blocker 来源于正式 artifact，而不是 reviewer 文本猜测
4. response 只描述"future adapter 当前如何响应 persistence request"，不描述"adapter 最终如何执行 durable receipt persistence"

当前不要求：

1. 真实 persistence id
2. recovery handle / resume token / retry token
3. store location / object path / database key / replication metadata / retention policy
4. host / machine / deployment / provider payload / connector credential
5. 真实 executor command、worker lease、distributed recovery orchestration

## Persistence Request / Persistence Response / Future Real Adapter 的职责拆分

V0.19 当前冻结的分层关系是：

1. `PersistenceRequest`
   - durable-adapter-receipt-persistence-facing machine artifact
   - 提供 persistence request identity、persistence status / outcome、persistence blocker、persistence boundary
2. `Response`
   - durable-adapter-receipt-persistence-response-facing machine artifact
   - 提供 response identity、response status / outcome、response blocker、response boundary
3. `ResponseReviewSummary`
   - reviewer-facing durable adapter receipt persistence response summary
   - 只归档 response 的可读视图，不重新创造私有 adapter / persistence executor / recovery 状态机
4. future real durable store adapter / persistence executor / recovery protocol
   - 不属于 V0.19 当前阶段
   - 只能在 durable receipt persistence response compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 receipt persistence response 语义，再倒逼 `Response` 适配
2. 不允许 future real adapter 直接从 persistence request review、receipt review、decision review、request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 response state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 response blocker / capability gap 语义

## Layering-Sensitive Compatibility 规则

V0.19 当前还必须额外冻结下面四条 layering-sensitive 规则：

1. `PersistenceRequest` 仍然是 durable adapter receipt persistence response 的直接 machine-facing 上游
   - `Response` 可以消费 persistence request
   - 但不应回退让 `PersistenceReviewSummary` 变成 machine-facing 第一输入
2. `ResponseReviewSummary` 仍然只是 projection
   - 它可以总结 response preview / blocker / next-step recommendation
   - 但不应承担真实 persistence execution、persistence id 派发、store mutation execution 或 recovery command synthesis
3. future real durable store adapter / persistence executor / recovery protocol 仍然是下一层
   - 它可以复用 response 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、database key、persistence id、retry token 或 operator payload
4. `PersistenceRequest` 与 `Response` 的职责不能合并
   - persistence request 负责 adapter receipt persistence 请求边界
   - persistence response 负责 adapter receipt persistence 响应边界

若后续变化触碰下面任一方向，通常就不再只是"补字段"而是分层变化：

1. 让 `PersistenceReviewSummary`、`ReceiptReviewSummary`、`DecisionReviewSummary`、`ReviewSummary`、`StoreImportReviewSummary`、`PersistenceExportReviewSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 durable receipt persistence response 的第一输入
2. 让 `Response` 承担真实 object uploader、database writer、transaction commit protocol 或 recovery launcher ABI
3. 让 `ResponseReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 adapter-receipt-persistence-response-facing 第一事实来源

## Durable Store Adapter Receipt Persistence Response Review 的当前边界

V0.19 当前明确区分三类与 durable store adapter receipt persistence response 相关的输出：

1. `PersistenceRequest`
   - 回答"当前 future adapter contract 对 receipt persistence 的 machine-facing request 到底长什么样"
   - 第一输入是 `Receipt`
2. `Response`
   - 回答"当前 future adapter contract 对 persistence request 的 machine-facing response 到底长什么样"
   - 第一输入是 `PersistenceRequest`
3. `ResponseReviewSummary`
   - 回答"当前 reviewer 应如何理解 response、blocker 与 next step"
   - 第一输入是 `Response`

因此：

1. 不允许用 `PersistenceReviewSummary` 倒推 `Response`
2. 不允许用 `ResponseReviewSummary` 倒推真实 durable adapter / persistence executor / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 adapter-receipt-persistence-response-facing 第一事实来源

## V0.19 Scope 冻结结论

V0.19 当前范围只包括：

1. local / offline durable store adapter receipt persistence response 的 scope 与术语冻结
2. future real durable store adapter 对 V0.18 persistence request 的最小 machine-facing response 边界
3. persistence request、persistence response、response review 与 future real adapter 的职责切分准备

V0.19 当前明确不包括：

1. 真实 receipt persistence writer / executor
2. 真实 persistence id、store URI、object path、database key、retention policy 或 provider payload
3. 真实 retry token、resume token、launcher ABI、worker lease 或 crash recovery protocol
4. host telemetry、connector credential、deployment metadata 或 operator payload

## Durable Receipt Persistence Response 与 Future Real Adapter 的升级关系

V0.19 当前冻结的升级关系是：

1. `Response`
   - local / offline machine artifact
   - 只回答"当前 future adapter contract 如何响应 persistence request"
2. future real durable store adapter / persistence executor / recovery protocol
   - durable / executable protocol layer
   - 才回答"如何真正执行 durable receipt persistence、如何持久化、如何恢复、由谁执行"

这意味着：

1. future real adapter / recovery protocol 可以复用 `Response` 的 response identity、response outcome、response blocker 与 response boundary
2. future real adapter / recovery protocol 不应要求 V0.19 当前就携带真实 persistence id、resume token、store URI、object path、database key 或 persistence execution payload
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `Response` / `ResponseReviewSummary`
