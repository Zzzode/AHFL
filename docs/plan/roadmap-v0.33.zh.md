# AHFL V0.33 Roadmap

V0.33 建立 provider SDK mock adapter。目标是用 mock adapter 覆盖真实 provider 会出现的成功、失败、超时、限流、冲突与 schema mismatch，让后续真实 adapter 有完整回归替身。

## 目标

1. 定义 mock provider adapter contract。
2. 定义 mock result fixture 与 provider response normalization。
3. 覆盖 success、failure、timeout、throttle、conflict、schema mismatch。
4. 将 mock result 映射回 SDK adapter response artifact。

## 非目标

1. 真实 provider SDK
2. network、credential、secret manager
3. production retry scheduler
4. provider-specific SLA 或 telemetry sink

## 里程碑

- [ ] M0 冻结 mock adapter fixture schema
- [ ] M1 实现 mock adapter execution
- [ ] M2 定义 result normalization 与 failure attribution
- [ ] M3 覆盖 golden matrix
- [ ] M4 更新 contributor guide 与 CI 标签

## 完成定义

V0.33 完成后，真实 provider adapter 的大部分行为可以先用 mock adapter 回归。
