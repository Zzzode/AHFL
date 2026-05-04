# AHFL V0.27 Roadmap

本文给出 AHFL 在 V0.26 provider local host execution receipt prototype 完成后的下一阶段实施路线。V0.27 的重点是冻结 provider SDK adapter request / response artifact boundary：在 `ProviderLocalHostExecutionReceipt` 之后生成 SDK adapter request plan、response placeholder 与 readiness review，但仍不调用真实 SDK、不读取 secret、不打开网络、不写 filesystem、不落 object path 或 database table。

基线输入：

- [roadmap-v0.26.zh.md](./roadmap-v0.26.zh.md)
- [provider-production-roadmap-v0.27.zh.md](./provider-production-roadmap-v0.27.zh.md)
- [native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md](../design/native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md)

## 目标

1. 定义 `ProviderSdkAdapterRequestPlan`，只消费 V0.26 `ProviderLocalHostExecutionReceipt`。
2. 定义 `ProviderSdkAdapterResponsePlaceholder`，表达 future SDK adapter response 的 deterministic placeholder identity。
3. 定义 `ProviderSdkAdapterReadinessReview`，说明是否可以进入 adapter interface implementation。
4. 建立 direct model、validation、CLI/backend、golden、compatibility、CI 标签闭环。

## 非目标

1. 真实 SDK client、provider network call、host process、filesystem write
2. credential、secret value、secret manager response、endpoint URI
3. object path、database table、provider request / response payload
4. 向后不兼容地修改 V0.26 local host execution receipt 稳定字段

## 里程碑

### M0 Adapter Artifact Boundary

- [x] 冻结 request plan / response placeholder / readiness review 的职责分层
- [x] 冻结 V0.26 receipt 到 V0.27 request plan 的输入边界
- [x] 冻结 deterministic SDK adapter request / response placeholder identity namespace

### M1 Model 与 Validation

- [x] 实现 request plan model 与 validation
- [x] 实现 response placeholder model 与 validation
- [x] 实现 readiness review model 与 validation

### M2 CLI、Golden 与 Docs

- [x] 新增 CLI / backend 输出
- [x] 覆盖 single-file、project、workspace golden
- [x] 更新 compatibility、consumer matrix、contributor guide、CI 与 migration docs

## 完成定义

`ctest --preset test-dev --output-on-failure -L ahfl-v0.27` 可用，并且 V0.27 artifact 不包含真实 SDK payload、credential、secret、endpoint、network、filesystem、object path、database table 或 provider response payload。
