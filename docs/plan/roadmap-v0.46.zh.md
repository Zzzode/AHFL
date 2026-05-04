# AHFL V0.46 Roadmap

V0.46 建立 release evidence archive。目标是把 readiness、audit、compatibility、registry、schema 与 config validation evidence 汇总为确定性的 release evidence manifest，方便 operator review、CI 留痕与发布审计。

## 目标

1. 定义 release evidence archive manifest artifact。
2. 汇总 production readiness、audit event、compatibility suite、registry selection 与 schema validation evidence。
3. 记录 artifact digest、source chain、generation time policy 与 redaction boundary。
4. 输出 deterministic archive summary、missing evidence summary 与 `ahfl-v0.46` 测试标签。

## 非目标

1. 上传 release archive 到外部系统
2. 生成组织级发布审批
3. 包含 secret value 或 credential
4. 执行真实 provider mutation

## 里程碑

- [ ] M0 冻结 release evidence archive scope
- [ ] M1 定义 archive manifest input / output model
- [ ] M2 实现 evidence collection 与 digest summary
- [ ] M3 输出 missing / stale / incompatible evidence summary
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.46 完成后，AHFL 可以生成可复现的 provider production release evidence manifest，并明确哪些证据缺失、过期或不兼容。
