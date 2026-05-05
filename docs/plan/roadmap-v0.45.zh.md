# AHFL V0.45 Roadmap

V0.45 建立 production config bundle validation。目标是校验生产配置包中的 provider selection、secret handle、endpoint shape、environment binding 与 policy reference，而不读取 secret value 或发起真实连接。

## 目标

1. 定义 production config bundle validation artifact。
2. 校验 provider registry selection 与 config bundle 的 provider reference。
3. 校验 secret handle presence、scope 与 redaction policy。
4. 校验 endpoint shape、environment binding 与 policy reference 的结构完整性。

## 非目标

1. 读取 secret value
2. 验证 credential 是否真实有效
3. 发起 network connection
4. 自动生成生产配置

## 里程碑

- [x] M0 冻结 production config validation scope
- [x] M1 定义 config bundle validation input / output model
- [x] M2 实现 provider、secret handle、endpoint 与 env binding 校验
- [x] M3 输出 redacted validation report 与 blocking summary
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.45 完成后，AHFL 可以在不接触敏感值的情况下判断生产配置包是否足够完整，并为后续真实 provider opt-in 提供结构化前置证据。
