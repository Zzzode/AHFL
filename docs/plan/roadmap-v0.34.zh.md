# AHFL V0.34 Roadmap

V0.34 接入第一个真实 provider adapter alpha。建议优先选择 local filesystem 或 sqlite 这类低风险 provider，而不是直接接云厂商 SDK。

## 目标

1. 选择第一个 alpha provider，并冻结 provider-specific compatibility note。
2. 实现最小真实 adapter。
3. 将真实 result 映射回统一 response artifact。
4. 保持 fake / mock adapter 仍为默认测试路径。

## 非目标

1. 云 IAM / KMS / secret manager 集成
2. 多 provider registry production rollout
3. 高可用、跨 region、operator console
4. 默认开启真实写入

## 里程碑

- [ ] M0 选择 alpha provider 与安全边界
- [ ] M1 实现真实 adapter alpha
- [ ] M2 定义真实 result normalization
- [ ] M3 增加 opt-in integration tests
- [ ] M4 更新 docs、migration、CI opt-in 说明

## 完成定义

V0.34 完成后，仓库有第一个可选真实 provider adapter alpha，但默认 CI 仍不依赖外部服务。
