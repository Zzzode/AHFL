# AHFL V0.48 Roadmap

V0.48 建立 real provider opt-in execution guard。目标是在真实 provider traffic 出现前提供显式、可审计、默认拒绝的 opt-in gate，防止误触发生产副作用。

## 目标

1. 定义 real provider opt-in guard artifact。
2. 校验 approval receipt、config validation、runtime policy 与 provider registry selection。
3. 定义默认 deny、explicit allow 与 scoped override 规则。
4. 输出 opt-in decision report 与 denial reason summary。

## 非目标

1. 默认启用真实 provider traffic
2. 执行 provider mutation
3. 读取 secret value
4. 绕过 operator approval

## 里程碑

- [ ] M0 冻结 opt-in guard scope
- [ ] M1 定义 opt-in request / decision / denial model
- [ ] M2 实现 approval、config、policy 与 registry gate 校验
- [ ] M3 输出 deterministic allow / deny report
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.48 完成后，AHFL 真实 provider traffic 具备默认拒绝、显式放行、范围限定和可审计拒绝原因，后续 runtime policy 可以在此基础上统一执行。
