# AHFL V0.20 Roadmap

本文给出 AHFL 在 V0.19 durable adapter receipt persistence response / review 闭环完成之后的下一阶段实施路线。V0.20 的重点是启动 production-adjacent durable store adapter planning：冻结真实持久化执行器、persistence id、store mutation、recovery command 与 provider boundary 的最小产品边界。V0.20 不直接一次性交付完整分布式存储平台，而是先把真实 adapter 能否接入 AHFL artifact chain 的契约、失败归因和测试入口做成可实施 backlog。

基线输入：

- [roadmap-v0.19.zh.md](./roadmap-v0.19.zh.md)（已完成基线）
- [issue-backlog-v0.19.zh.md](./issue-backlog-v0.19.zh.md)（已完成基线）
- [native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md](../design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md)
- [durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md](../reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md)
- [native-consumer-matrix-v0.19.zh.md](../reference/native-consumer-matrix-v0.19.zh.md)
- [contributor-guide-v0.19.zh.md](../reference/contributor-guide-v0.19.zh.md)

当前实现状态：

1. V0.19 已完成 `PersistenceRequest -> Response -> ResponseReviewSummary` 的 local/offline durable adapter receipt persistence response 边界。
2. 当前仓库已有 accepted / rejected CLI golden，并通过 direct model/bootstrap regression 固定 accepted、blocked、deferred、rejected 四类 response 语义。
3. 当前仓库仍没有真实 durable store adapter、persistence executor、persistence id 分配、object storage writer、database writer、recovery command 或 provider connector。
4. V0.20 必须继续复用 V0.19 冻结的 response artifact，不得回退读取 review、trace、AST、host log 或私有 payload 猜 durable adapter state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.20 的交付目标是：

1. 冻结 production-adjacent durable store adapter / persistence executor 的最小 scope、non-goals 与 failure model。
2. 定义 persistence id、store mutation intent、adapter execution receipt 与 recovery command 的分层关系。
3. 明确真实 store backend 引入前的 local fake durable store contract，用于 deterministic regression，而不是直接绑定某个数据库或 object storage provider。
4. 让 future adapter implementation 只能消费 V0.19 `Response` 及其上游 machine artifacts，不能从 reviewer summary 或 CLI 文本反推状态。
5. 输出 issue-ready backlog，使后续可以按模型、validation、bootstrap、backend/CLI、golden、compatibility、CI 的顺序推进。

## 非目标

V0.20 初始阶段仍不直接承诺：

1. 多租户生产数据库 schema、object storage layout、replication、retention、GC 或跨 region failover
2. 真实云 provider SDK、secret 管理、endpoint / tenant / region 配置或 credential lifecycle
3. 分布式 worker restart、crash recovery daemon、operator console、SIEM/event ingestion 或 host telemetry ingestion
4. 向后不兼容地修改 V0.19 `Response` 稳定字段
5. 绕过 AHFL artifact chain 的私有 durable adapter protocol

## 里程碑

### M0 V0.20 Scope 与 Production-Adjacent Boundary 冻结

状态：

- [ ] Issue 01 冻结 V0.20 durable store adapter / persistence executor scope 与 non-goals
- [ ] Issue 02 冻结 persistence id、store mutation intent 与 adapter execution receipt 的最小模型
- [ ] Issue 03 冻结 V0.19 response、future real adapter 与 recovery command 的分层关系

目标：

1. 明确 V0.20 是 production-adjacent adapter contract 阶段，不是完整生产存储平台。
2. 明确真实 adapter 只能从 V0.19 `Response` 和上游 machine artifact 获取输入。
3. 明确哪些字段属于 stable adapter input，哪些字段只能留给 future provider-specific extension。

### M1 Local Fake Durable Store Contract

状态：

- [ ] Issue 04 引入 local fake durable store contract 数据模型
- [ ] Issue 05 增加 store mutation intent validation 与 direct regression
- [ ] Issue 06 增加 deterministic fake store bootstrap

目标：

1. 用本地 deterministic fake store 固定 persistence id、mutation intent 与 execution receipt 语义。
2. 让后续真实 provider adapter 可以替换 fake store，而不是重新定义 artifact chain。
3. 区分 input artifact error、adapter contract error、store mutation error 与 recovery planning error。

### M2 Adapter Execution Receipt 与 Recovery Preview

状态：

- [ ] Issue 07 增加 adapter execution receipt 输出路径
- [ ] Issue 08 增加 recovery command preview / reviewer summary
- [ ] Issue 09 增加 accepted、blocked、deferred、rejected 至 adapter execution 的 golden / direct 回归

目标：

1. 让 contributor 能查看 persistence execution 是否已形成 stable adapter handoff。
2. 让 rejected / blocked / deferred 路径在 production-adjacent 层仍保留可解释 failure attribution。
3. 继续避免把真实 provider payload、credential、host log 或 telemetry 塞进 machine artifact。

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [ ] Issue 10 冻结 V0.20 compatibility contract
- [ ] Issue 11 更新 native consumer matrix 与 contributor guide
- [ ] Issue 12 建立 V0.20 regression、CI 与 migration docs 闭环

目标：

1. 把 production-adjacent durable adapter contract 纳入持续回归。
2. 让 future provider-specific adapter work 有明确的 docs / code / golden / tests 同步流程。
3. 保持 V0.19 response compatibility 不被 V0.20 静默破坏。

## 当前状态

V0.20 当前处于规划启动状态，尚未实现 production-adjacent durable store adapter / persistence executor 代码。下一步应从 `Issue 01-03` 开始冻结 scope、model 和 layering，再进入 local fake durable store contract 的模型实现。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.20.zh.md](./issue-backlog-v0.20.zh.md)
