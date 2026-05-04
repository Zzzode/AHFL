# AHFL V0.38 Roadmap

V0.38 统一 provider failure taxonomy。目标是把认证、授权、网络、限流、冲突、schema mismatch、provider internal error 与 unknown error 映射成稳定 failure kind。

## 目标

1. 定义 provider failure taxonomy v1。
2. 定义 retryability、operator action、security sensitivity 标记。
3. 将 mock / real adapter failures 映射到统一 taxonomy。
4. 更新 readiness、commit、recovery review 的 failure summary。

## 非目标

1. provider-specific raw error 全量持久化
2. 自动修复策略
3. SIEM / alerting production sink
4. secret-bearing error message 存储

## 里程碑

- [x] M0 冻结 taxonomy scope
- [x] M1 定义 failure kind / category / retryability
- [x] M2 接入 adapter response normalization
- [x] M3 覆盖 mock adapter failure matrix
- [x] M4 更新 compatibility docs 与 golden

## 完成定义

V0.38 完成后，下游 consumer 不需要理解 provider 原始错误码，也能做稳定 retry、review 和 recovery 决策。
