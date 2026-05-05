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

- [x] M0 冻结 opt-in guard scope
- [x] M1 定义 opt-in request / decision / denial model
- [x] M2 实现 approval、config、policy 与 registry gate 校验
- [x] M3 输出 deterministic allow / deny report
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.48 完成后，AHFL 真实 provider traffic 具备默认拒绝、显式放行、范围限定和可审计拒绝原因，后续 runtime policy 可以在此基础上统一执行。

## 实现

### v0.48.1 Core Model

- `ProviderOptInDecisionReport` - Opt-in 决定报告结构
- `OptInDecision` 枚举 - Allow / Deny / DenyWithOverride
- `OptInDenialReason` 枚举 - 拒绝原因分类
- `OptInGateCheck` - Gate 检查项结构
- `ScopedOverride` - 范围限定覆盖结构

### v0.48.2 Gate 校验

- Gate 1: Approval Receipt - 检查 operator 审批
- Gate 2: Config Bundle Validation - 检查配置有效性
- Gate 3: Registry Selection - 检查 provider 注册匹配
- Gate 4: Security Constraints - 检查敏感操作限制

### v0.48.3 CLI 集成

- 命令: `emit-durable-store-import-provider-opt-in-decision-report`
- 后端打印: `print_durable_store_import_provider_opt_in_decision_report`

### v0.48.4 测试覆盖

- 8 个 direct tests 覆盖: all-gates-pass、deny-no-approval、deny-config-invalid、deny-registry-mismatch、deny-security-constraints、validate-format-version、validate-decision-consistency、default-deny
- CTest 标签: `ahfl-v0.48`
