# AHFL Native Durable Store Import Prototype Bootstrap V0.15

本文冻结 AHFL V0.15 中 durable-store-import prototype、`DurableStoreImportRequest` / requested artifact set，以及 future durable store adapter 可稳定消费的 durable-store-import-facing local state 最小边界。它承接 V0.14 已完成的 `StoreImportDescriptor`、`StoreImportReviewSummary`、compatibility / consumer matrix / contributor guide 闭环，目标是把仓库推进到“可稳定表达 durable-store-import-facing machine artifact”的下一层边界，但仍不进入真实 durable store import executor、object uploader、database writer、resume protocol、crash recovery 或 operator payload 领域。

关联文档：

- [native-store-import-prototype-bootstrap-v0.14.zh.md](./native-store-import-prototype-bootstrap-v0.14.zh.md)
- [store-import-prototype-compatibility-v0.14.zh.md](../reference/store-import-prototype-compatibility-v0.14.zh.md)
- [roadmap-v0.15.zh.md](../plan/roadmap-v0.15.zh.md)
- [issue-backlog-v0.15.zh.md](../plan/issue-backlog-v0.15.zh.md)

## 本文回答的问题

本文主要回答六个问题：

1. V0.15 为什么要在 `StoreImportDescriptor` 之后引入新的 durable-store-import-facing machine artifact。
2. `DurableStoreImportRequest` 的第一落点在哪里。
3. `StoreImportDescriptor`、durable store import request、review 与 future durable store adapter / recovery ABI 各自如何分层。
4. 当前最小 requested artifact set 应稳定表达什么，而不应偷偷承诺什么。
5. reviewer-facing durable store import summary 当前只应回答什么。
6. 哪些变化已经触及下一层真实 durable store adapter / recovery protocol，而不再属于 V0.15。

## 为什么 V0.15 不直接进入真实 durable store adapter

V0.14 已经把 `StoreImportDescriptor` 与 `StoreImportReviewSummary` 做成稳定 artifact，但它们仍然只回答：

1. 当前 store import handoff 是否已经 ready。
2. staging artifact set 中逻辑上包含哪些 entry。
3. contributor / reviewer 该如何理解当前 staging boundary。

它们仍然没有正式回答：

1. future durable store adapter 收到的 machine-facing request 到底长什么样。
2. adapter-facing requested artifact set 与 staging artifact set 的投影关系是什么。
3. adapter-ready / adapter-blocker 应如何建立在正式 `StoreImportDescriptor` 之上，而不是由 CLI 文本或外部脚本猜测。

因此 V0.15 的重点不是立刻实现真实 durable store import executor，而是先把：

```text
StoreImportDescriptor
  -> DurableStoreImportRequest
  -> DurableStoreImportReviewSummary
```

这一层 machine-facing / reviewer-facing 边界正式冻结下来。

## V0.15 当前成功标准

V0.15 当前成功标准是：

1. 当前仓库能稳定表达 machine-facing `DurableStoreImportRequest`
2. durable store import request 能回答“当前 future durable store adapter 最小可稳定消费的 requested artifact set 是什么”
3. durable store import request 能回答“当前 adapter 是否 ready；若未 ready，adapter blocker 是什么”
4. durable store import review 能把 machine-facing durable store import 语义转成 contributor / reviewer 可读摘要
5. 上述能力都建立在 `StoreImportDescriptor` 之上，而不是在 CLI / review / grep / trace 层私造 adapter contract

它不表示：

1. 当前仓库已经实现真实 durable store import executor
2. 当前仓库已经支持 object upload、database write 或 commit protocol
3. 当前仓库已经拥有 import receipt、resume token、recovery handle 或 crash recovery runtime
4. 当前仓库已经承诺 store URI、object path、database key、tenant / region / credential 或 operator command ABI

## 当前正式依赖方向

V0.15 当前建议链路是：

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
  -> DurableStoreImportReviewSummary
```

这表示：

1. durable-store-import-facing state 的第一落点不是 AST、trace、checkpoint review、persistence review、export review、store import review 或 host log，而是正式 `DurableStoreImportRequest`
2. `StoreImportDescriptor` 仍是 durable store import request 的直接 machine-facing 上游
3. durable store import prototype 应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable store import request` 前进，而不是回退扫描 source、descriptor 文件、review 文本或 host log
4. `StoreImportReviewSummary` 仍只是 readable projection，不是 durable store import request ABI 的第一输入
5. future real durable store adapter / recovery protocol 只能在 durable store import request compatibility 冻结后继续规划

## DurableStoreImportRequest 的第一边界

V0.15 当前建议 `DurableStoreImportRequest` 至少回答下面六类问题：

1. 当前 adapter-facing request 是为哪个 package / workflow / session / run / store import candidate 生成的
2. 当前 requested artifact set 中应包含哪些 adapter-facing logical entry
3. requested artifact set 中每个 entry 对 durable adapter 的用途角色是什么
4. 当前 request 是否 adapter-ready；若未 ready，adapter blocker 是什么
5. 当前 request boundary 是 local intent only，还是已经具备 stable adapter-contract-consumable shape
6. 当前 request 依赖的 source artifact 版本链是什么

当前最小边界只要求：

1. durable store import request identity 与 source store import candidate identity 已结构化、可稳定序列化
2. requested artifact set entry 已结构化、可稳定序列化
3. adapter blocker 来源于正式 artifact，而不是 review 文本猜测
4. request 只描述“future adapter 当前可稳定消费什么 request”，不描述“adapter 最终如何写入 durable store”

当前不要求：

1. durable checkpoint id
2. recovery handle / resume token / import receipt
3. store location / object path / database key / replication metadata / retention policy
4. host / machine / deployment / provider payload

## Store Import Descriptor / Durable Store Import Request / Future Real Adapter 的职责拆分

V0.15 当前冻结的分层关系是：

1. `PersistenceExportManifest`
   - export-package-facing machine artifact
   - 提供 export package identity、artifact bundle、manifest readiness 与 store-import-adjacent boundary
2. `StoreImportDescriptor`
   - store-import-facing machine artifact
   - 提供 store import candidate identity、staging artifact set、import readiness 与 staging blocker
3. `DurableStoreImportRequest`
   - durable-store-import-facing machine artifact
   - 提供 adapter request identity、requested artifact set、adapter readiness 与 adapter blocker
4. `DurableStoreImportReviewSummary`
   - reviewer-facing durable store import summary
   - 只归档 durable store import request 的可读视图，不重新创造私有 durable store / recovery 状态机
5. future real durable store adapter / recovery protocol
   - 不属于 V0.15 当前阶段
   - 只能在 durable store import request compatibility 冻结后继续规划

因此：

1. 不允许先在 review / CLI 层捏造 adapter-ready 语义，再倒逼 durable store import request 适配
2. 不允许 future real adapter 直接从 store import review、export review、persistence review、checkpoint review、scheduler review、trace、source 或 host log 倒推 durable store import state
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 requested artifact set / blocker 语义

## Layering-Sensitive Compatibility 规则

V0.15 当前还必须额外冻结下面四条 layering-sensitive 规则：

1. `StoreImportDescriptor` 仍然是 durable store import request 的直接 machine-facing 上游
   - `DurableStoreImportRequest` 可以消费 store import descriptor
   - 但不应回退让 `StoreImportReviewSummary` 变成 machine-facing 第一输入
2. `DurableStoreImportReviewSummary` 仍然只是 projection
   - 它可以总结 requested artifact preview / adapter boundary / next-step recommendation
   - 但不应承担 durable mutation、import receipt synthesis、resume token 派发或 recovery command synthesis
3. future real durable store adapter / recovery protocol 仍然是下一层
   - 它可以复用 durable store import request 的 stable fields
   - 但不应反向要求当前版本先携带 store URI、object path、database key、recovery handle、import receipt 或 operator payload
4. `StoreImportDescriptor` 与 `DurableStoreImportRequest` 的职责不能合并
   - store import descriptor 负责 staging handoff 边界
   - durable store import request 负责 adapter-facing request 边界

若后续变化触碰下面任一方向，通常就不再只是“补字段”而是分层变化：

1. 让 `StoreImportReviewSummary`、`PersistenceExportReviewSummary`、`PersistenceReviewSummary`、`CheckpointReviewSummary`、`SchedulerDecisionSummary`、`AuditReport` 或 `DryRunTrace` 反向成为 durable store import request 的第一输入
2. 让 `DurableStoreImportRequest` 承担真实 object uploader、database writer、transaction commit protocol 或 recovery launcher ABI
3. 让 `DurableStoreImportReviewSummary` 承担独立 machine-facing taxonomy
4. 让 trace / audit / host log 成为 durable-store-import-facing 第一事实来源

## Durable Store Import Review 的当前边界

V0.15 当前明确区分三类与 durable store import 相关的输出：

1. `StoreImportDescriptor`
   - 回答“当前 stable store import handoff 到底长什么样”
   - 第一输入是 `PersistenceExportManifest`
2. future `DurableStoreImportRequest`
   - 回答“当前 future durable store adapter 最小可稳定消费的 request 到底长什么样”
   - 第一输入是 `StoreImportDescriptor`
3. future `DurableStoreImportReviewSummary`
   - 回答“当前 reviewer 应如何理解 requested artifact set、adapter boundary 与 next step”
   - 第一输入是 `DurableStoreImportRequest`

因此：

1. 不允许用 `StoreImportReviewSummary` 倒推 `DurableStoreImportRequest`
2. 不允许用 `DurableStoreImportReviewSummary` 倒推真实 durable adapter / recovery protocol
3. 不允许用 `AuditReport` / `DryRunTrace` 替代 durable-store-import-facing 第一事实来源

## V0.15 Scope 冻结结论

V0.15 当前范围只包括：

1. local / offline durable store import request 的 scope 与术语冻结
2. future real durable store adapter 的最小 durable-store-import-facing 输入边界
3. durable store import request、durable store import review 与 future real adapter 的职责切分准备

V0.15 当前明确不包括：

1. 真实 durable store import executor
2. 真实 object path、store URI、bucket layout、database key、retention policy 或 import receipt
3. 真实 recovery command、launcher ABI、resume token 或 crash recovery protocol
4. host telemetry、provider payload、connector credential、deployment metadata 或 operator payload

## Durable Store Import Prototype 与 Future Real Adapter 的升级关系

V0.15 当前冻结的升级关系是：

1. `DurableStoreImportRequest`
   - local / offline machine artifact
   - 只回答“当前 future adapter handoff 是否已准备好”
2. future real durable store adapter / recovery protocol
   - durable / executable protocol layer
   - 才回答“如何写入 durable store、如何生成 receipt、如何恢复、由谁执行”

这意味着：

1. future real adapter / recovery protocol 可以复用 `DurableStoreImportRequest` 的 request identity、requested artifact set、adapter boundary 与 adapter blocker
2. future real adapter / recovery protocol 不应要求 V0.15 当前就携带 durable checkpoint id、resume token、store URI、object path、database key 或 import receipt
3. 若未来需要把这些 durable 字段提升为稳定输入，应通过新的 compatibility contract 明确演进，而不是静默塞回 `DurableStoreImportRequest` / `DurableStoreImportReviewSummary`
