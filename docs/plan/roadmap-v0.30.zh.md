# AHFL V0.30 Roadmap

V0.30 冻结 secret handle resolver boundary。目标是定义 secret handle、resolver policy、credential lifecycle placeholder 与 secret-free review，让后续真实 credential resolution 有稳定入口。

## 目标

1. 定义 secret handle reference model。
2. 定义 resolver request plan 与 resolver response placeholder。
3. 定义 credential lifecycle 状态机的 artifact skeleton。
4. 定义 secret policy review，保证 secret value 不进入核心 artifact。

## 非目标

1. 真实 secret manager 调用
2. 明文 secret、credential value、token value
3. KMS / IAM / tenant provisioning 实现
4. provider SDK network call

## 里程碑

- [ ] M0 冻结 secret handle 与 resolver non-goals
- [ ] M1 定义 resolver request / response placeholder model
- [ ] M2 定义 credential lifecycle placeholder
- [ ] M3 定义 validation，拒绝 secret value 与 credential material
- [ ] M4 建立 docs、golden、CI 标签

## 完成定义

V0.30 完成后，artifact 只能表达“哪个 secret handle 将来会被解析”，不能包含或泄露任何 secret value。
