# AHFL V0.49 Roadmap

V0.49 建立 provider runtime policy enforcement。目标是在 provider execution 前统一执行 opt-in、config、registry、readiness、approval 与 environment policy，形成一个明确的 runtime permit / deny 决策。

## 目标

1. 定义 runtime policy enforcement artifact。
2. 聚合 opt-in guard、approval receipt、config validation、registry selection 与 readiness evidence。
3. 定义 blocking policy、warning policy 与 scoped override policy。
4. 输出 runtime permit / deny report 与 policy violation summary。

## 非目标

1. 替代组织级 policy engine
2. 自动修复 policy violation
3. 隐式升级 scoped override
4. 执行真实 provider mutation

## 里程碑

- [ ] M0 冻结 runtime policy enforcement scope
- [ ] M1 定义 policy input / decision / violation model
- [ ] M2 实现 opt-in、approval、config、registry 与 readiness policy aggregation
- [ ] M3 输出 permit / deny / warning report
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.49 完成后，AHFL 在真实 provider execution 前具备统一的 runtime policy gate，并能解释每个 permit、deny、warning 与 override 的证据来源。
