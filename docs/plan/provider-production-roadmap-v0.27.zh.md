# AHFL Provider Production Roadmap V0.27-V0.42

本文给出 V0.26 之后从 artifact-only provider prototype 走向 production-ready provider execution 的总体路线。当前基线是 [roadmap-v0.26.zh.md](./roadmap-v0.26.zh.md)：仓库已经能稳定表达 provider local host execution receipt，但仍没有真实 SDK、secret、网络、host process 或 durable store 写入。

## 总体原则

1. 每个版本先冻结 stable artifact contract，再引入真实副作用。
2. secret、credential、endpoint、host env、network、filesystem、provider payload 不得倒灌进核心 artifact。
3. 所有真实执行能力必须有 fake / mock / dry-run 替身和 golden regression。
4. 真实 provider adapter 只能消费上游 machine artifact，不能读取 reviewer 文本、CLI 输出、host log 或私有脚本推导状态。

## 阶段 A：真实 SDK 前的契约

| 版本 | 主题 | 交付边界 |
| --- | --- | --- |
| V0.27 | Provider SDK Adapter Request / Response Prototype | 定义 SDK adapter request plan、response placeholder 与 readiness review，不调用 SDK |
| V0.28 | Provider SDK Adapter Interface | 定义 C++ adapter interface、provider registry、capability descriptor 与错误码归一化 |
| V0.29 | Config Loader Boundary | 定义 provider config snapshot / loader plan，不读取 secret value |
| V0.30 | Secret Handle Resolver Boundary | 定义 secret handle、resolver placeholder、credential lifecycle contract |

## 阶段 B：可控真实执行

| 版本 | 主题 | 交付边界 |
| --- | --- | --- |
| V0.31 | Local Host Harness Implementation | 实现 test-only local host harness，默认 sandbox、no-network |
| V0.32 | SDK Payload Materialization | 把 artifact 转成 provider SDK request payload，仍只对 fake provider 开启 |
| V0.33 | Provider SDK Mock Adapter | 建立 mock SDK adapter，覆盖 success、failure、timeout、throttle |
| V0.34 | Provider SDK Real Adapter Alpha | 接入第一个真实 provider adapter alpha，优先 local filesystem 或 sqlite |

## 阶段 C：真实 durable store 能力

| 版本 | 主题 | 交付边界 |
| --- | --- | --- |
| V0.35 | Idempotency / Retry Contract | 定义 idempotency key、retry token 与 duplicate write protection |
| V0.36 | Durable Write Commit Receipt | 定义真实写入后的 commit receipt，不再只是 placeholder |
| V0.37 | Recovery / Resume Contract | 定义 crash recovery、resume token 与 partial write recovery |
| V0.38 | Provider Failure Taxonomy | 统一认证、网络、限流、冲突、schema mismatch 与未知错误 |

## 阶段 D：生产化外壳

| 版本 | 主题 | 交付边界 |
| --- | --- | --- |
| V0.39 | Observability / Audit Event Sink | 输出 structured telemetry、audit event 与 operator review summary |
| V0.40 | Provider Compatibility Suite | 为 provider adapter 建立兼容性矩阵、fixture 与 golden contract |
| V0.41 | Multi-provider Registry | 支持多 provider 注册、选择、降级和 capability negotiation |
| V0.42 | Production Readiness Review | 汇总 security、recovery、compatibility、observability，形成生产准入 review |

## 下一步

立即下一步是 [roadmap-v0.27.zh.md](./roadmap-v0.27.zh.md)。V0.27 仍然是 artifact-only 阶段，禁止真实 SDK 调用、credential、secret、endpoint、network、filesystem write、object path、database table 与 provider response payload。
