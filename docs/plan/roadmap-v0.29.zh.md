# AHFL V0.29 Roadmap

V0.29 冻结 provider config loader boundary。目标是把 provider config snapshot、profile selection 与 config validation 放到稳定 artifact 中，但不读取 secret value、不连接 secret manager、不打开网络。

## 目标

1. 定义 provider config load plan。
2. 定义 provider config snapshot placeholder。
3. 定义 config schema compatibility 与 profile selection failure attribution。
4. 让 V0.28 adapter descriptor 可以声明需要的 config profile。

## 非目标

1. secret value、credential material、token exchange
2. 真实 config service、remote endpoint、network
3. provider SDK payload materialization
4. production tenant provisioning

## 里程碑

- [ ] M0 冻结 config loader scope、输入边界与 forbidden fields
- [ ] M1 定义 config profile / snapshot placeholder model
- [ ] M2 定义 missing / incompatible / unsupported config diagnostics
- [ ] M3 建立 CLI / golden / compatibility / CI

## 完成定义

V0.29 完成后，provider adapter 可以稳定知道“应使用哪个 config profile”，但不能拿到 secret value 或真实 endpoint credential。
