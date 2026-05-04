# AHFL V0.41 Roadmap

V0.41 建立 multi-provider registry。目标是支持多个 provider adapter 注册、选择、降级和 capability negotiation。

## 目标

1. 定义 multi-provider registry artifact。
2. 定义 provider selection plan。
3. 定义 fallback / degradation policy。
4. 定义 capability negotiation review。

## 非目标

1. production traffic routing
2. multi-region failover
3. 自动成本优化
4. 未审计 provider fallback

## 里程碑

- [ ] M0 冻结 registry / selection scope
- [ ] M1 定义 registry model
- [ ] M2 定义 provider selection / fallback plan
- [ ] M3 覆盖 capability mismatch 与 degradation paths
- [ ] M4 建立 golden、docs、CI 标签

## 完成定义

V0.41 完成后，系统可以稳定选择 provider adapter，并在 capability 不满足时给出可审计 fallback 决策。
