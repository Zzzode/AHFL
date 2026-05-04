# AHFL V0.32 Roadmap

V0.32 冻结 SDK payload materialization boundary。目标是把稳定 artifact 转换为 provider SDK request payload 的受控步骤，但默认只对 fake / mock provider 开启。

## 目标

1. 定义 SDK payload materialization plan。
2. 定义 provider request payload schema ref 与 payload digest。
3. 定义 payload redaction / audit summary。
4. 确保 materialized payload 不进入不该持久化的核心 artifact。

## 非目标

1. 真实 provider SDK call
2. secret-bearing payload
3. production endpoint / object path / database table
4. 无审计的 payload dump

## 里程碑

- [ ] M0 冻结 payload materialization scope
- [ ] M1 定义 payload plan / digest / redaction summary
- [ ] M2 接入 fake provider schema
- [ ] M3 覆盖 payload forbidden-field validation
- [ ] M4 建立 golden 与 compatibility docs

## 完成定义

V0.32 完成后，可以生成可审计的 fake SDK request payload，但真实 provider SDK 仍未被调用。
